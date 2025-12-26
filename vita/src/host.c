#include "context.h"
#include "config.h"
#include "controller.h"
#include "host.h"
#include "discovery.h"
#include "audio.h"
#include "video.h"
#include "string.h"
#include <stdio.h>
#include <psp2/ctrl.h>
#include <psp2/motion.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <chiaki/base64.h>
#include <chiaki/session.h>
#include <chiaki/streamconnection.h>
#include <chiaki/videoreceiver.h>
#include <chiaki/frameprocessor.h>

static void reset_stream_metrics(bool preserve_recovery_state);
static void update_latency_metrics(void);
static unsigned int latency_mode_target_kbps(VitaChiakiLatencyMode mode);
static void apply_latency_mode(ChiakiConnectVideoProfile *profile, VitaChiakiLatencyMode mode);
static void request_stream_stop(const char *reason);
static bool request_stream_restart(uint32_t bitrate_kbps);
static void resume_discovery_if_needed(void);
static void host_set_hint(VitaChiakiHost *host, const char *msg, bool is_error, uint64_t duration_us);
static void handle_loss_event(int32_t frames_lost, bool frame_recovered);
static bool handle_unrecovered_frame_loss(int32_t frames_lost, bool frame_recovered);
static void handle_takion_overflow(void);
static bool auto_downgrade_latency_mode(void);
static const char *latency_mode_label(VitaChiakiLatencyMode mode);
static void shutdown_media_pipeline(void);
static uint32_t clamp_u32(uint32_t value, uint32_t min_value, uint32_t max_value);
static void request_decoder_resync(const char *reason);
static const char *quit_reason_label(ChiakiQuitReason reason);
static void update_disconnect_banner(const char *reason);
typedef struct {
  uint64_t window_us;
  uint32_t min_frames;
  uint32_t event_threshold;
  uint32_t frame_threshold;
  uint64_t burst_window_us;
  uint32_t burst_frame_threshold;
} LossDetectionProfile;
static LossDetectionProfile loss_profile_for_mode(VitaChiakiLatencyMode mode);
static void adjust_loss_profile_with_metrics(LossDetectionProfile *profile);

#define STREAM_RETRY_COOLDOWN_US (3 * 1000 * 1000ULL)
#define LOSS_ALERT_DURATION_US (5 * 1000 * 1000ULL)
#define LOSS_EVENT_WINDOW_DEFAULT_US (8 * 1000 * 1000ULL)
#define LOSS_EVENT_MIN_FRAMES_DEFAULT 4
#define LOSS_EVENT_THRESHOLD_DEFAULT 3
#define LOSS_RETRY_DELAY_US (2 * 1000 * 1000ULL)
#define LOSS_RETRY_BITRATE_KBPS 800
#define LOSS_RETRY_MAX_ATTEMPTS 2
#define UNRECOVERED_FRAME_THRESHOLD 3
#define UNRECOVERED_FRAME_GATE_THRESHOLD 2
#define UNRECOVERED_FRAME_GATE_WINDOW_US (800 * 1000ULL)
#define TAKION_OVERFLOW_RESTART_DELAY_US (1500 * 1000ULL)
#define TAKION_OVERFLOW_SOFT_RECOVERY_WINDOW_US (12 * 1000 * 1000ULL)
#define TAKION_OVERFLOW_SOFT_RECOVERY_MAX 2
#define TAKION_OVERFLOW_RECOVERY_BACKOFF_US (6 * 1000 * 1000ULL)
#define TAKION_OVERFLOW_RECOVERY_BITRATE_KBPS LOSS_RETRY_BITRATE_KBPS
#define TAKION_OVERFLOW_IGNORE_THRESHOLD 2
#define TAKION_OVERFLOW_IGNORE_WINDOW_US (400 * 1000ULL)
#define RESTART_FAILURE_COOLDOWN_US (5000 * 1000ULL)
#define FAST_RESTART_GRACE_DELAY_US (200 * 1000ULL)
#define FAST_RESTART_RETRY_DELAY_US (250 * 1000ULL)
#define FAST_RESTART_MAX_ATTEMPTS 2
// Never let soft restarts ask the console for more than ~1.5 Mbps or the Vita
// Wi-Fi path risks oscillating into unsustainable bitrates.
#define FAST_RESTART_BITRATE_CAP_KBPS 1500

void host_free(VitaChiakiHost *host) {
  if (host) {
    if (host->discovery_state) {
      destroy_discovery_host(host->discovery_state);
      host->discovery_state = NULL;
    }
    if (host->registered_state) {
      free(host->registered_state);
    }
    if (host->hostname) {
      free(host->hostname);
    }
  }
}

ChiakiRegist regist = {};
static void regist_cb(ChiakiRegistEvent *event, void *user) {
  LOGD("regist event %d", event->type);
  if (event->type == CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS) {
    context.active_host->type |= REGISTERED;

    if (context.active_host->registered_state != NULL) {
      free(context.active_host->registered_state);
      context.active_host->registered_state = event->registered_host;
      memcpy(&context.active_host->server_mac, &(event->registered_host->server_mac), 6);
      printf("FOUND HOST TO UPDATE\n");
      for (int rhost_idx = 0; rhost_idx < context.config.num_registered_hosts; rhost_idx++) {
        VitaChiakiHost* rhost =
            context.config.registered_hosts[rhost_idx];
        if (rhost == NULL) {
          continue;
        }

        printf("NAME1 %s\n", rhost->registered_state->server_nickname);
        printf("NAME2 %s\n", context.active_host->registered_state->server_nickname);
        if ((rhost->server_mac) && (context.active_host->server_mac) && mac_addrs_match(&(rhost->server_mac), &(context.active_host->server_mac))) {
          printf("FOUND MATCH\n");
          context.config.registered_hosts[rhost_idx] = context.active_host;
          break;
        }
      }
    } else {
      context.active_host->registered_state = event->registered_host;
      memcpy(&context.active_host->server_mac, &(event->registered_host->server_mac), 6);
      context.config.registered_hosts[context.config.num_registered_hosts++] = context.active_host;
    }

    config_serialize(&context.config);
  }

  chiaki_regist_stop(&regist);
	chiaki_regist_fini(&regist);
}

int host_register(VitaChiakiHost* host, int pin) {
  if (!host->hostname || !host->discovery_state) {
    return 1;
  }
  ChiakiRegistInfo regist_info = {};
  regist_info.target = host->target;
  size_t account_id_size = sizeof(uint8_t[CHIAKI_PSN_ACCOUNT_ID_SIZE]);
  chiaki_base64_decode(context.config.psn_account_id, /*sizeof(context.config.psn_account_id)*/12, regist_info.psn_account_id, &(account_id_size));
  regist_info.psn_online_id = NULL;
	regist_info.pin = pin;
  regist_info.host = host->hostname;
  regist_info.broadcast = false;
	chiaki_regist_start(&regist, &context.log, &regist_info, regist_cb, NULL);
  return 0;
}

int host_wakeup(VitaChiakiHost* host) {
  if (!host->hostname) {
    LOGE("Missing hostname. Cannot send wakeup signal.");
    return 1;
  }
  LOGD("Attempting to send wakeup signal....");
	uint64_t credential = (uint64_t)strtoull(host->registered_state->rp_regist_key, NULL, 16);
  chiaki_discovery_wakeup(&context.log,
                          context.discovery_enabled ? &context.discovery.discovery : NULL,
                          host->hostname, credential,
                          chiaki_target_is_ps5(host->target));
  return 0;
}

static void event_cb(ChiakiEvent *event, void *user) {
	switch(event->type)
	{
		case CHIAKI_EVENT_CONNECTED:
			LOGD("EventCB CHIAKI_EVENT_CONNECTED");
      context.stream.inputs_ready = true;
      context.stream.next_stream_allowed_us = 0;
      ui_connection_set_stage(UI_CONNECTION_STAGE_STARTING_STREAM);
      if (context.stream.fast_restart_active) {
        context.stream.fast_restart_active = false;
        context.stream.reconnect_overlay_active = false;
      }
			break;
		case CHIAKI_EVENT_LOGIN_PIN_REQUEST:
			LOGD("EventCB CHIAKI_EVENT_LOGIN_PIN_REQUEST");
			break;
		case CHIAKI_EVENT_RUMBLE:
			LOGD("EventCB CHIAKI_EVENT_RUMBLE");
			break;
	case CHIAKI_EVENT_QUIT: {
      bool user_stop_requested = context.stream.stop_requested;
      const char *reason_label = quit_reason_label(event->quit.reason);
			LOGE("EventCB CHIAKI_EVENT_QUIT (%s | code=%d \"%s\")",
				 event->quit.reason_str ? event->quit.reason_str : "unknown",
				 event->quit.reason,
				 reason_label);
      ui_connection_cancel();
      bool restart_failed = context.stream.fast_restart_active;
      bool retry_pending = context.stream.loss_retry_pending;
      bool fallback_active = context.stream.loss_retry_active || retry_pending;
      uint64_t retry_ready = context.stream.loss_retry_ready_us;
      uint32_t retry_attempts = context.stream.loss_retry_attempts;
      uint32_t retry_bitrate = context.stream.loss_retry_bitrate_kbps;
      if (retry_pending && !context.active_host)
        retry_pending = false;
      shutdown_media_pipeline();
      context.stream.inputs_resume_pending = fallback_active;
      ui_clear_waking_wait();
      context.stream.session_init = false;
      uint64_t now_us = sceKernelGetProcessTimeWide();
      uint64_t takion_backoff_until = context.stream.takion_overflow_backoff_until_us;
      bool takion_cooldown_active = context.stream.takion_cooldown_overlay_active;
      bool remote_in_use =
          event->quit.reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE;
      bool remote_crash =
          event->quit.reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH;
      if (context.active_host && (remote_in_use || remote_crash)) {
        const char *hint =
            remote_in_use ? "Remote Play already active on console"
                          : "Console Remote Play crashed - wait a moment";
        host_set_hint(context.active_host, hint, true, 7 * 1000 * 1000ULL);
      }
      uint64_t retry_delay = STREAM_RETRY_COOLDOWN_US;
      if (!context.stream.stop_requested && (remote_in_use || remote_crash)) {
        retry_delay = 5 * 1000 * 1000ULL;
      }
      uint64_t throttle_until = now_us + retry_delay;
      if (takion_backoff_until && takion_backoff_until > throttle_until)
        throttle_until = takion_backoff_until;
      if (context.stream.stop_requested) {
        context.stream.next_stream_allowed_us =
            (takion_backoff_until && takion_backoff_until > now_us)
                ? takion_backoff_until
                : 0;
      } else {
        context.stream.next_stream_allowed_us = throttle_until;
      }
      if (context.stream.next_stream_allowed_us > now_us) {
        uint64_t wait_ms =
            (context.stream.next_stream_allowed_us - now_us + 999) / 1000ULL;
        LOGD("Stream cooldown engaged for %llu ms", wait_ms);
      }
      if (!user_stop_requested) {
        const char *banner_reason =
            (event->quit.reason_str && event->quit.reason_str[0])
                ? event->quit.reason_str
                : reason_label;
        update_disconnect_banner(banner_reason);
      }
      context.stream.stop_requested = false;
      bool should_resume_discovery = !retry_pending;
      reset_stream_metrics(true);
      context.stream.takion_overflow_backoff_until_us = takion_backoff_until;
      context.stream.takion_cooldown_overlay_active =
          takion_cooldown_active &&
          takion_backoff_until && takion_backoff_until > now_us;
      context.stream.loss_retry_attempts = retry_attempts;
      context.stream.loss_retry_bitrate_kbps = retry_bitrate;
      context.stream.loss_retry_ready_us = retry_ready;
      context.stream.loss_retry_pending = false;
      context.stream.loss_retry_active = false;
      context.stream.reconnect_overlay_active = false;

      bool schedule_retry = restart_failed && context.active_host &&
          retry_bitrate > 0 && retry_attempts < LOSS_RETRY_MAX_ATTEMPTS;

      if (schedule_retry) {
        context.stream.loss_retry_attempts = retry_attempts + 1;
        context.stream.loss_retry_pending = true;
        context.stream.loss_retry_ready_us =
            now_us + (context.stream.loss_retry_ready_us ? 0 : LOSS_RETRY_DELAY_US);
        should_resume_discovery = false;
        LOGD("Soft restart failed — scheduling hard fallback retry #%u",
             retry_attempts + 1);
      }

      if (should_resume_discovery)
        resume_discovery_if_needed();

      if (schedule_retry && context.active_host) {
        uint64_t now_retry = sceKernelGetProcessTimeWide();
        uint64_t desired = context.stream.loss_retry_ready_us ?
            context.stream.loss_retry_ready_us : now_retry;
        if (desired < now_retry)
          desired = now_retry;
        if (desired > now_retry) {
          uint64_t wait = desired - now_retry;
          sceKernelDelayThread((unsigned int)wait);
        }
        context.stream.loss_retry_pending = false;
        context.stream.loss_retry_active = true;
        context.stream.loss_retry_ready_us = 0;
        context.stream.reconnect_overlay_active = true;
        context.stream.reconnect_overlay_start_us = sceKernelGetProcessTimeWide();
        LOGD("Restarting stream after packet loss fallback (%u kbps)",
             context.stream.loss_retry_bitrate_kbps ?
             context.stream.loss_retry_bitrate_kbps : LOSS_RETRY_BITRATE_KBPS);
        int restart_result = host_stream(context.active_host);
        if (restart_result != 0) {
          LOGE("Fallback restart failed (%d)", restart_result);
          context.stream.loss_retry_active = false;
          context.stream.reconnect_overlay_active = false;
          context.stream.last_restart_failure_us = sceKernelGetProcessTimeWide();
          context.stream.restart_failure_active = true;
          resume_discovery_if_needed();
        } else {
          context.stream.loss_retry_active = false;
          context.stream.reconnect_overlay_active = false;
          resume_discovery_if_needed();
        }
      }
			break;
    }
	}
}

static void reset_stream_metrics(bool preserve_recovery_state) {
  context.stream.measured_bitrate_mbps = 0.0f;
  context.stream.measured_rtt_ms = 0;
  context.stream.metrics_last_update_us = 0;
  context.stream.measured_incoming_fps = 0;
  context.stream.fps_window_start_us = 0;
  context.stream.fps_window_frame_count = 0;
  context.stream.negotiated_fps = 0;
  context.stream.target_fps = 0;
  context.stream.pacing_accumulator = 0;
  context.stream.frame_loss_events = 0;
  context.stream.total_frames_lost = 0;
  context.stream.loss_window_start_us = 0;
  context.stream.loss_window_event_count = 0;
  context.stream.loss_window_frame_accum = 0;
  context.stream.loss_burst_frame_accum = 0;
  context.stream.loss_burst_start_us = 0;
  context.stream.loss_alert_until_us = 0;
  context.stream.loss_alert_duration_us = 0;
  context.stream.logged_loss_events = 0;
  context.stream.auto_loss_downgrades = 0;
  context.stream.takion_drop_events = 0;
  context.stream.takion_drop_packets = 0;
  context.stream.logged_drop_events = 0;
  context.stream.takion_drop_last_us = 0;
  context.stream.last_takion_overflow_restart_us = 0;
  if (!preserve_recovery_state) {
  context.stream.takion_overflow_soft_attempts = 0;
  context.stream.takion_overflow_window_start_us = 0;
  context.stream.takion_overflow_backoff_until_us = 0;
  context.stream.takion_cooldown_overlay_active = false;
  context.stream.takion_overflow_drop_window_start_us = 0;
  context.stream.takion_overflow_recent_drops = 0;
  }
  context.stream.last_restart_failure_us = 0;
  context.stream.disconnect_reason[0] = '\0';
  context.stream.disconnect_banner_until_us = 0;
  context.stream.loss_retry_pending = false;
  context.stream.loss_retry_active = false;
  context.stream.loss_retry_attempts = 0;
  context.stream.loss_retry_bitrate_kbps = 0;
  context.stream.loss_retry_ready_us = 0;
  context.stream.reconnect_overlay_active = false;
  context.stream.reconnect_overlay_start_us = 0;
  context.stream.fast_restart_active = false;
  context.stream.cached_controller_valid = false;
  context.stream.last_input_packet_us = 0;
  context.stream.last_input_stall_log_us = 0;
  context.stream.inputs_blocked_since_us = 0;
  context.stream.inputs_resume_pending = false;
  context.stream.unrecovered_frame_streak = 0;
  context.stream.unrecovered_gate_events = 0;
  context.stream.unrecovered_gate_window_start_us = 0;
  context.stream.restart_failure_active = false;
  vitavideo_hide_poor_net_indicator();
}

static void shutdown_media_pipeline(void) {
  if (!context.stream.media_initialized)
    return;

  chiaki_opus_decoder_fini(&context.stream.opus_decoder);
  vita_h264_cleanup();
  vita_audio_cleanup();
  context.stream.media_initialized = false;
  context.stream.is_streaming = false;
  context.stream.inputs_ready = false;
  context.stream.fast_restart_active = false;
  context.stream.reconnect_overlay_active = false;
}

static void update_latency_metrics(void) {
  if (!context.stream.session_init)
    return;

  ChiakiStreamConnection *stream_connection = &context.stream.session.stream_connection;
  ChiakiVideoReceiver *receiver = stream_connection->video_receiver;
  if (!receiver)
    return;

  context.stream.takion_drop_events = stream_connection->drop_events;
  context.stream.takion_drop_packets = stream_connection->drop_packets;
  context.stream.takion_drop_last_us =
      stream_connection->drop_last_ms ? (stream_connection->drop_last_ms * 1000ULL) : 0;

  uint32_t fps = context.stream.session.connect_info.video_profile.max_fps;
  if (fps == 0)
    fps = 30;

  ChiakiStreamStats *stats = &receiver->frame_processor.stream_stats;
  uint64_t bitrate_bps = chiaki_stream_stats_bitrate(stats, fps);
  float bitrate_mbps = bitrate_bps > 0 ? ((float)bitrate_bps / 1000000.0f) : 0.0f;
  uint32_t rtt_ms = (uint32_t)(context.stream.session.rtt_us / 1000);
  uint64_t now_us = sceKernelGetProcessTimeWide();

  context.stream.measured_bitrate_mbps = bitrate_mbps;
  context.stream.measured_rtt_ms = rtt_ms;
  context.stream.metrics_last_update_us = now_us;

  if (!context.config.show_latency)
    return;

  static const uint64_t LOG_INTERVAL_US = 1000000;
  static uint64_t last_log_us = 0;
  if (now_us - last_log_us >= LOG_INTERVAL_US) {
    float target_mbps = context.stream.session.connect_info.video_profile.bitrate / 1000.0f;
    LOGD("Latency metrics — target %.2f Mbps, measured %.2f Mbps, RTT %u ms",
         target_mbps, bitrate_mbps, rtt_ms);
    last_log_us = now_us;
  }

  if (context.stream.takion_drop_events != context.stream.logged_drop_events) {
    uint32_t delta = context.stream.takion_drop_events - context.stream.logged_drop_events;
    LOGD("Packet loss — Takion dropped %u packet(s), total %u",
         delta,
         context.stream.takion_drop_packets);
    context.stream.logged_drop_events = context.stream.takion_drop_events;
    handle_takion_overflow();
  }
}

static unsigned int latency_mode_target_kbps(VitaChiakiLatencyMode mode) {
  switch (mode) {
    case VITA_LATENCY_MODE_ULTRA_LOW: return 1200;
    case VITA_LATENCY_MODE_LOW: return 1800;
    case VITA_LATENCY_MODE_HIGH: return 3200;
    case VITA_LATENCY_MODE_MAX: return 3800;
    case VITA_LATENCY_MODE_BALANCED:
    default:
      return 2600;
  }
}

static void apply_latency_mode(ChiakiConnectVideoProfile *profile, VitaChiakiLatencyMode mode) {
  unsigned int target = latency_mode_target_kbps(mode);
  profile->bitrate = target;
  LOGD("Latency mode applied: %u kbps @ %ux%u", target, profile->width, profile->height);
}

static void request_stream_stop(const char *reason) {
  if (!context.stream.session_init)
    return;
  if (!context.stream.stop_requested) {
    LOGD("Stopping stream (%s)", reason ? reason : "user");
    context.stream.stop_requested = true;
  }
  context.stream.next_stream_allowed_us = 0;
  chiaki_session_stop(&context.stream.session);
}

void host_cancel_stream_request(void) {
  request_stream_stop("user cancel");
}

static bool request_stream_restart(uint32_t bitrate_kbps) {
  if (!context.stream.session_init) {
    LOGE("Cannot restart stream — session not initialized");
    return false;
  }
  uint64_t now_us = sceKernelGetProcessTimeWide();
  if (context.stream.last_restart_failure_us &&
      now_us - context.stream.last_restart_failure_us < RESTART_FAILURE_COOLDOWN_US) {
    uint64_t remaining = RESTART_FAILURE_COOLDOWN_US -
        (now_us - context.stream.last_restart_failure_us);
    LOGD("Restart cooldown active — delaying %llu ms", remaining / 1000ULL);
    return false;
  }
  if (context.stream.fast_restart_active) {
    LOGD("Soft restart already active; ignoring duplicate request");
    return true;
  }

  ChiakiConnectVideoProfile profile = context.stream.session.connect_info.video_profile;
  if (bitrate_kbps > 0) {
    profile.bitrate = bitrate_kbps;
  } else {
    profile.bitrate = LOSS_RETRY_BITRATE_KBPS;
  }

  if (context.config.clamp_soft_restart_bitrate &&
      profile.bitrate > FAST_RESTART_BITRATE_CAP_KBPS) {
    LOGD("Soft restart bitrate %u kbps exceeds cap %u kbps — clamping",
         profile.bitrate, FAST_RESTART_BITRATE_CAP_KBPS);
    profile.bitrate = FAST_RESTART_BITRATE_CAP_KBPS;
  }

  ChiakiErrorCode err = CHIAKI_ERR_UNKNOWN;
  for (uint32_t attempt = 0; attempt < FAST_RESTART_MAX_ATTEMPTS; ++attempt) {
    err =
        chiaki_session_request_stream_restart(&context.stream.session, &profile);
    if (err == CHIAKI_ERR_SUCCESS) {
      if (attempt > 0) {
        LOGD("Soft restart request succeeded on retry %u", attempt + 1);
      }
      break;
    }
    LOGE("Soft restart request attempt %u failed: %s",
         attempt + 1,
         chiaki_error_string(err));
    if (attempt + 1 < FAST_RESTART_MAX_ATTEMPTS) {
      sceKernelDelayThread(FAST_RESTART_RETRY_DELAY_US);
    }
  }
  if (err != CHIAKI_ERR_SUCCESS) {
    LOGE("Failed to request soft stream restart after %u attempt(s)",
         FAST_RESTART_MAX_ATTEMPTS);
    return false;
  }

  context.stream.fast_restart_active = true;
  context.stream.is_streaming = false;
  context.stream.reconnect_overlay_active = true;
  context.stream.reconnect_overlay_start_us = sceKernelGetProcessTimeWide();
  context.stream.inputs_ready = true;
  context.stream.inputs_resume_pending = true;
  context.stream.restart_failure_active = false;
  context.stream.takion_cooldown_overlay_active = false;
  return true;
}

static void request_decoder_resync(const char *reason) {
  if (!context.stream.session_init)
    return;
  ChiakiStreamConnection *stream_connection =
      &context.stream.session.stream_connection;
  ChiakiErrorCode err =
      chiaki_stream_connection_request_idr(stream_connection);
  if (err == CHIAKI_ERR_SUCCESS) {
    LOGD("Decoder resync requested (%s)", reason ? reason : "unspecified");
  } else {
    LOGE("Failed to request decoder resync (%s): %s",
         reason ? reason : "unspecified",
         chiaki_error_string(err));
  }
}

static void resume_discovery_if_needed(void) {
  if (context.discovery_resume_after_stream) {
    LOGD("Resuming discovery after stream");
    start_discovery(NULL, NULL);
    context.discovery_resume_after_stream = false;
  }
}

static void host_set_hint(VitaChiakiHost *host, const char *msg, bool is_error, uint64_t duration_us) {
  if (!host)
    return;

  if (msg && msg[0]) {
    sceClibSnprintf(host->status_hint, sizeof(host->status_hint), "%s", msg);
    host->status_hint_is_error = is_error;
    uint64_t now_us = sceKernelGetProcessTimeWide();
    host->status_hint_expire_us = duration_us ? (now_us + duration_us) : 0;
    if (is_error) {
      context.ui_state.error_popup_active = true;
      sceClibSnprintf(context.ui_state.error_popup_text,
                      sizeof(context.ui_state.error_popup_text), "%s", msg);
    }
  } else {
    host->status_hint[0] = '\0';
    host->status_hint_is_error = false;
    host->status_hint_expire_us = 0;
    if (is_error) {
      context.ui_state.error_popup_active = false;
      context.ui_state.error_popup_text[0] = '\0';
    }
  }
}

static const char *latency_mode_label(VitaChiakiLatencyMode mode) {
  switch (mode) {
    case VITA_LATENCY_MODE_ULTRA_LOW: return "Ultra Low";
    case VITA_LATENCY_MODE_LOW: return "Low";
    case VITA_LATENCY_MODE_BALANCED: return "Balanced";
    case VITA_LATENCY_MODE_HIGH: return "High";
    case VITA_LATENCY_MODE_MAX: return "Max";
    default: return "Unknown";
  }
}

static bool auto_downgrade_latency_mode(void) {
  if (context.config.latency_mode == VITA_LATENCY_MODE_ULTRA_LOW)
    return false;
  context.config.latency_mode =
      (VitaChiakiLatencyMode)(context.config.latency_mode - 1);
  context.stream.auto_loss_downgrades++;
  LOGD("Auto latency mode downgrade triggered (%s)",
       latency_mode_label(context.config.latency_mode));
  return true;
}

static bool handle_unrecovered_frame_loss(int32_t frames_lost, bool frame_recovered) {
  bool triggered = false;
  if (frames_lost <= 0) {
    context.stream.unrecovered_frame_streak = 0;
    return triggered;
  }

  if (frame_recovered) {
    context.stream.unrecovered_frame_streak = 0;
    return triggered;
  }

  context.stream.unrecovered_frame_streak += (uint32_t)frames_lost;
  if (context.stream.unrecovered_frame_streak < UNRECOVERED_FRAME_THRESHOLD)
    return triggered;

  context.stream.unrecovered_frame_streak = 0;
  if (context.stream.fast_restart_active || context.stream.stop_requested)
    return triggered;

  uint64_t now_us = sceKernelGetProcessTimeWide();
  if (context.stream.unrecovered_gate_window_start_us == 0 ||
      now_us - context.stream.unrecovered_gate_window_start_us >
          UNRECOVERED_FRAME_GATE_WINDOW_US) {
    context.stream.unrecovered_gate_window_start_us = now_us;
    context.stream.unrecovered_gate_events = 0;
  }

  context.stream.unrecovered_gate_events++;
  if (context.stream.unrecovered_gate_events <=
      UNRECOVERED_FRAME_GATE_THRESHOLD) {
    if (context.config.show_latency) {
      uint32_t remaining = UNRECOVERED_FRAME_GATE_THRESHOLD -
          context.stream.unrecovered_gate_events + 1;
      LOGD("Unrecovered frame gate tolerated (%u event(s) remaining)",
           remaining);
    }
    vitavideo_show_poor_net_indicator();
    context.stream.loss_alert_until_us =
        now_us + LOSS_ALERT_DURATION_US;
    context.stream.loss_alert_duration_us = LOSS_ALERT_DURATION_US;
    request_decoder_resync("unrecovered frame gate");
    return triggered;
  }

  context.stream.unrecovered_gate_events = 0;
  context.stream.unrecovered_gate_window_start_us = now_us;
  request_decoder_resync("unrecovered frame streak");
  LOGD("Unrecovered frame streak detected — requesting soft restart");
  if (!request_stream_restart(LOSS_RETRY_BITRATE_KBPS)) {
    LOGE("Soft restart request failed after unrecovered frames; pausing stream");
    request_stream_stop("decoder desync");
    triggered = true;
    context.stream.last_restart_failure_us = sceKernelGetProcessTimeWide();
    context.stream.restart_failure_active = true;
  } else if (context.active_host) {
    host_set_hint(context.active_host,
                  "Video desync — retrying stream",
                  true,
                  5 * 1000 * 1000ULL);
    triggered = true;
    context.stream.last_restart_failure_us = 0;
  }
  return triggered;
}

static void handle_takion_overflow(void) {
  if (context.stream.stop_requested)
    return;

  uint64_t now_us = sceKernelGetProcessTimeWide();

  if (context.stream.takion_cooldown_overlay_active &&
      context.stream.takion_overflow_backoff_until_us &&
      now_us >= context.stream.takion_overflow_backoff_until_us) {
    context.stream.takion_cooldown_overlay_active = false;
    context.stream.takion_overflow_backoff_until_us = 0;
  }

  if (context.stream.fast_restart_active) {
    LOGD("Takion overflow reported while restart active; ignoring");
    return;
  }

  if (context.stream.takion_overflow_drop_window_start_us == 0 ||
      now_us - context.stream.takion_overflow_drop_window_start_us >
          TAKION_OVERFLOW_IGNORE_WINDOW_US) {
    context.stream.takion_overflow_drop_window_start_us = now_us;
    context.stream.takion_overflow_recent_drops = 0;
  }

  context.stream.takion_overflow_recent_drops++;
  if (context.stream.takion_overflow_recent_drops <=
      TAKION_OVERFLOW_IGNORE_THRESHOLD) {
    if (context.config.show_latency) {
      uint64_t window_elapsed_us =
          now_us - context.stream.takion_overflow_drop_window_start_us;
      uint64_t window_ms = window_elapsed_us / 1000ULL;
      LOGD("Takion overflow tolerated (%u/%u within %llums)",
           context.stream.takion_overflow_recent_drops,
           TAKION_OVERFLOW_IGNORE_THRESHOLD,
           (unsigned long long)window_ms);
    }
    return;
  }

  if (context.config.show_latency &&
      context.stream.takion_overflow_recent_drops ==
          (TAKION_OVERFLOW_IGNORE_THRESHOLD + 1)) {
    uint64_t window_elapsed_us =
        now_us - context.stream.takion_overflow_drop_window_start_us;
    uint64_t window_ms = window_elapsed_us / 1000ULL;
    LOGD("Takion overflow gate tripped after %u drops in %llums",
         context.stream.takion_overflow_recent_drops,
         (unsigned long long)window_ms);
    uint32_t flushed_seq = chiaki_takion_drop_data_queue(
        &context.stream.session.stream_connection.takion);
    if (flushed_seq) {
      LOGD("Takion overflow gate flushed reorder queue (ack %#x)", flushed_seq);
    }
    request_decoder_resync("takion overflow gate");
  }

  if (context.stream.takion_overflow_backoff_until_us &&
      now_us < context.stream.takion_overflow_backoff_until_us) {
    uint64_t remaining =
        context.stream.takion_overflow_backoff_until_us - now_us;
    LOGD("Takion overflow mitigation cooling down (%llu ms remaining)",
         remaining / 1000ULL);
    context.stream.takion_cooldown_overlay_active = true;
    return;
  }

  if (context.stream.takion_overflow_window_start_us == 0 ||
      now_us - context.stream.takion_overflow_window_start_us >
          TAKION_OVERFLOW_SOFT_RECOVERY_WINDOW_US) {
    context.stream.takion_overflow_window_start_us = now_us;
    context.stream.takion_overflow_soft_attempts = 0;
  }

  if (context.stream.takion_overflow_soft_attempts <
      TAKION_OVERFLOW_SOFT_RECOVERY_MAX) {
    uint32_t attempt = context.stream.takion_overflow_soft_attempts + 1;
    LOGD("Takion overflow — soft recovery %u/%u at %u kbps",
         attempt,
         TAKION_OVERFLOW_SOFT_RECOVERY_MAX,
         TAKION_OVERFLOW_RECOVERY_BITRATE_KBPS);
    if (FAST_RESTART_GRACE_DELAY_US)
      sceKernelDelayThread(FAST_RESTART_GRACE_DELAY_US);
    bool restart_ok =
        request_stream_restart(TAKION_OVERFLOW_RECOVERY_BITRATE_KBPS);
    context.stream.takion_overflow_backoff_until_us =
        now_us + TAKION_OVERFLOW_RECOVERY_BACKOFF_US;
    if (context.stream.next_stream_allowed_us <
        context.stream.takion_overflow_backoff_until_us) {
      context.stream.next_stream_allowed_us =
          context.stream.takion_overflow_backoff_until_us;
    }
    if (restart_ok) {
      context.stream.takion_overflow_soft_attempts++;
      context.stream.last_takion_overflow_restart_us = now_us;
      context.stream.takion_cooldown_overlay_active = true;
      if (context.active_host) {
        host_set_hint(context.active_host,
                      "Network congestion — reducing bitrate",
                      false,
                      5 * 1000 * 1000ULL);
      }
      return;
    }
    LOGE("Takion overflow soft recovery request failed");
    context.stream.last_restart_failure_us = now_us;
    context.stream.restart_failure_active = true;
    context.stream.takion_cooldown_overlay_active = true;
    return;
  }

  if (context.stream.last_takion_overflow_restart_us &&
      now_us - context.stream.last_takion_overflow_restart_us <
          TAKION_OVERFLOW_RESTART_DELAY_US) {
    uint64_t remaining =
        TAKION_OVERFLOW_RESTART_DELAY_US -
        (now_us - context.stream.last_takion_overflow_restart_us);
    LOGD("Takion overflow restart throttled (%llu ms remaining)",
         remaining / 1000ULL);
    return;
  }

  context.stream.last_takion_overflow_restart_us = now_us;
  context.stream.takion_overflow_backoff_until_us =
      now_us + TAKION_OVERFLOW_RECOVERY_BACKOFF_US;
  context.stream.takion_cooldown_overlay_active = true;
  if (context.stream.next_stream_allowed_us <
      context.stream.takion_overflow_backoff_until_us) {
    context.stream.next_stream_allowed_us =
        context.stream.takion_overflow_backoff_until_us;
  }
  LOGD("Takion overflow threshold reached — requesting guarded restart");
  if (!request_stream_restart(LOSS_RETRY_BITRATE_KBPS)) {
    LOGE("Takion overflow restart failed; pausing stream");
    request_stream_stop("takion overflow");
  } else if (context.active_host) {
    host_set_hint(context.active_host,
                  "Network congestion — rebuilding stream",
                  true,
                  5 * 1000 * 1000ULL);
  }
}

static uint32_t clamp_u32(uint32_t value, uint32_t min_value, uint32_t max_value) {
  if (value < min_value)
    return min_value;
  if (value > max_value)
    return max_value;
  return value;
}

static const char *quit_reason_label(ChiakiQuitReason reason) {
  switch (reason) {
    case CHIAKI_QUIT_REASON_NONE: return "No quit";
    case CHIAKI_QUIT_REASON_STOPPED: return "User stopped";
    case CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN: return "Session request failed";
    case CHIAKI_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED: return "Connection refused";
    case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE: return "Remote Play already in use";
    case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH: return "Remote Play crashed";
    case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_VERSION_MISMATCH: return "Remote Play version mismatch";
    case CHIAKI_QUIT_REASON_CTRL_UNKNOWN: return "Control channel failure";
    case CHIAKI_QUIT_REASON_CTRL_CONNECT_FAILED: return "Control connection failed";
    case CHIAKI_QUIT_REASON_CTRL_CONNECTION_REFUSED: return "Control connection refused";
    case CHIAKI_QUIT_REASON_STREAM_CONNECTION_UNKNOWN: return "Stream connection failure";
    case CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_DISCONNECTED: return "Console disconnected";
    case CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN: return "Console shutdown";
    case CHIAKI_QUIT_REASON_PSN_REGIST_FAILED: return "PSN registration failed";
    default:
      return "Unspecified";
  }
}

static void update_disconnect_banner(const char *reason) {
  if (!reason || !reason[0])
    return;

  sceClibSnprintf(context.stream.disconnect_reason,
                  sizeof(context.stream.disconnect_reason),
                  "%s",
                  reason);
  uint64_t now_us = sceKernelGetProcessTimeWide();
  uint64_t until = context.stream.next_stream_allowed_us;
  if (!until)
    until = now_us + 3 * 1000 * 1000ULL;
  context.stream.disconnect_banner_until_us = until;
}

static LossDetectionProfile loss_profile_for_mode(VitaChiakiLatencyMode mode) {
  LossDetectionProfile profile = {
      .window_us = LOSS_EVENT_WINDOW_DEFAULT_US,
      .min_frames = LOSS_EVENT_MIN_FRAMES_DEFAULT,
      .event_threshold = LOSS_EVENT_THRESHOLD_DEFAULT,
      .frame_threshold = 10,
      .burst_window_us = 200 * 1000ULL,
      .burst_frame_threshold = 4};

  switch (mode) {
    case VITA_LATENCY_MODE_ULTRA_LOW:
      profile.window_us = 5 * 1000 * 1000ULL;
      profile.min_frames = 4;
      profile.event_threshold = 2;
      profile.frame_threshold = 6;
      profile.burst_window_us = 220 * 1000ULL;
      profile.burst_frame_threshold = 6;
      break;
    case VITA_LATENCY_MODE_LOW:
      profile.window_us = 7 * 1000 * 1000ULL;
      profile.min_frames = 4;
      profile.event_threshold = 3;
      profile.frame_threshold = 8;
      profile.burst_window_us = 240 * 1000ULL;
      profile.burst_frame_threshold = 5;
      break;
    case VITA_LATENCY_MODE_BALANCED:
    default:
      profile.window_us = LOSS_EVENT_WINDOW_DEFAULT_US;
      profile.min_frames = LOSS_EVENT_MIN_FRAMES_DEFAULT;
      profile.event_threshold = LOSS_EVENT_THRESHOLD_DEFAULT;
      profile.frame_threshold = 9;
      profile.burst_window_us = 220 * 1000ULL;
      profile.burst_frame_threshold = 5;
      break;
    case VITA_LATENCY_MODE_HIGH:
      profile.window_us = 9 * 1000 * 1000ULL;
      profile.min_frames = 5;
      profile.event_threshold = 3;
      profile.frame_threshold = 11;
      profile.burst_window_us = 260 * 1000ULL;
      profile.burst_frame_threshold = 6;
      break;
    case VITA_LATENCY_MODE_MAX:
      profile.window_us = 10 * 1000 * 1000ULL;
      profile.min_frames = 6;
      profile.event_threshold = 4;
      profile.frame_threshold = 13;
      profile.burst_window_us = 280 * 1000ULL;
      profile.burst_frame_threshold = 7;
      break;
  }

  return profile;
}

static void adjust_loss_profile_with_metrics(LossDetectionProfile *profile) {
  if (!profile)
    return;

  if (context.config.latency_mode == VITA_LATENCY_MODE_ULTRA_LOW &&
      context.stream.loss_retry_attempts == 0 && profile->event_threshold > 1) {
    profile->event_threshold--;
  }

  float target_mbps =
      (float)latency_mode_target_kbps(context.config.latency_mode) / 1000.0f;
  float measured_mbps = context.stream.measured_bitrate_mbps;
  bool bitrate_known = measured_mbps > 0.01f && target_mbps > 0.0f;
  const uint64_t window_step = 2 * 1000 * 1000ULL;

  if (bitrate_known) {
    if (measured_mbps <= target_mbps * 0.85f) {
      profile->event_threshold =
          clamp_u32(profile->event_threshold + 1, 1, 6);
      profile->min_frames = clamp_u32(profile->min_frames + 1, 2, 8);
      profile->frame_threshold =
          clamp_u32(profile->frame_threshold + 2, 4, 24);
      profile->burst_frame_threshold =
          clamp_u32(profile->burst_frame_threshold + 1, 3, 16);
      profile->window_us += window_step;
    } else if (measured_mbps >= target_mbps * 1.2f) {
      if (profile->event_threshold > 1)
        profile->event_threshold--;
      if (profile->min_frames > 2)
        profile->min_frames--;
      if (profile->frame_threshold > 4)
        profile->frame_threshold -= 2;
      if (profile->burst_frame_threshold > 3)
        profile->burst_frame_threshold--;
      if (profile->window_us > window_step)
        profile->window_us -= window_step;
      if (profile->burst_window_us > 100 * 1000ULL)
        profile->burst_window_us -= 50 * 1000ULL;
    }
  }

  uint32_t measured_fps = context.stream.measured_incoming_fps ?
      context.stream.measured_incoming_fps : context.stream.negotiated_fps;
  uint32_t clamp_target = context.stream.target_fps ?
      context.stream.target_fps : context.stream.negotiated_fps;
  if (measured_fps && clamp_target && measured_fps <= clamp_target) {
    profile->event_threshold =
        clamp_u32(profile->event_threshold + 1, 1, 6);
    profile->frame_threshold =
        clamp_u32(profile->frame_threshold + 1, 4, 24);
    profile->burst_frame_threshold =
        clamp_u32(profile->burst_frame_threshold + 1, 3, 16);
  }
}

static void handle_loss_event(int32_t frames_lost, bool frame_recovered) {
  if (frames_lost <= 0)
    return;

  uint64_t now_us = sceKernelGetProcessTimeWide();
  context.stream.frame_loss_events++;
  context.stream.total_frames_lost += (uint32_t)frames_lost;
  context.stream.loss_alert_until_us = now_us + LOSS_ALERT_DURATION_US;
  context.stream.loss_alert_duration_us = LOSS_ALERT_DURATION_US;
  vitavideo_show_poor_net_indicator();

  if (context.config.show_latency &&
      context.stream.frame_loss_events != context.stream.logged_loss_events) {
    LOGD("Frame loss — %d frame(s) dropped (recovered=%s)",
         frames_lost,
         frame_recovered ? "yes" : "no");
    context.stream.logged_loss_events = context.stream.frame_loss_events;
  }

  LossDetectionProfile loss_profile =
      loss_profile_for_mode(context.config.latency_mode);
  adjust_loss_profile_with_metrics(&loss_profile);

  if (context.stream.loss_window_start_us == 0 ||
      now_us - context.stream.loss_window_start_us > loss_profile.window_us) {
    context.stream.loss_window_start_us = now_us;
    context.stream.loss_window_event_count = 0;
    context.stream.loss_window_frame_accum = 0;
  }

  context.stream.loss_window_frame_accum += (uint32_t)frames_lost;

  if (frames_lost >= (int32_t)loss_profile.min_frames) {
    context.stream.loss_window_event_count++;
  }

  // Short-term burst tracking
  uint64_t burst_window_us = loss_profile.burst_window_us;
  if (context.stream.loss_burst_start_us == 0 ||
      now_us - context.stream.loss_burst_start_us > burst_window_us) {
    context.stream.loss_burst_start_us = now_us;
    context.stream.loss_burst_frame_accum = 0;
  }
  context.stream.loss_burst_frame_accum += (uint32_t)frames_lost;
  uint32_t burst_frames = context.stream.loss_burst_frame_accum;
  uint32_t window_frames = context.stream.loss_window_frame_accum;
  uint32_t window_events = context.stream.loss_window_event_count;
  uint64_t burst_elapsed_us = context.stream.loss_burst_start_us ?
      (now_us - context.stream.loss_burst_start_us) : 0;

  if (context.config.show_latency) {
    float burst_ms = burst_elapsed_us ? ((float)burst_elapsed_us / 1000.0f) : 0.0f;
    LOGD("Loss accumulators — drop=%d, window_frames=%u, events=%u, burst_frames=%u (%.1f ms)",
         frames_lost,
         window_frames,
         window_events,
         burst_frames,
         burst_ms);
  }

  bool hit_burst_threshold =
      context.stream.loss_burst_frame_accum >= loss_profile.burst_frame_threshold;

  bool hit_frame_threshold =
      context.stream.loss_window_frame_accum >= loss_profile.frame_threshold;
  bool hit_event_threshold =
      context.stream.loss_window_event_count >= loss_profile.event_threshold;

  if (!hit_event_threshold && !hit_frame_threshold && !hit_burst_threshold) {
    // Ignore sub-threshold hiccups; they're common on Vita Wi-Fi.
    // Keep accumulating so repeated drops can still trip the gate.
    return;
  }

  context.stream.loss_window_event_count = 0;
  context.stream.loss_window_start_us = now_us;
  context.stream.loss_window_frame_accum = 0;
  context.stream.loss_burst_frame_accum = 0;
  context.stream.loss_burst_start_us = 0;

  if (context.config.show_latency) {
    float window_s = (float)loss_profile.window_us / 1000000.0f;
    const char *trigger = hit_burst_threshold ? "burst threshold" :
        (hit_frame_threshold ? "frame threshold" : "event threshold");
    LOGD("Loss gate reached (%s, %u events / %u frames in %.1fs)",
         trigger,
         window_events,
         window_frames,
         window_s);
  }

  bool downgraded = false;
  char hint[96];
  if (context.config.latency_mode == VITA_LATENCY_MODE_ULTRA_LOW) {
    if (context.stream.loss_retry_attempts < 1 && !context.stream.fast_restart_active) {
      sceClibSnprintf(
          hint,
          sizeof(hint),
          "Network unstable — retrying at %.1f Mbps",
          (float)LOSS_RETRY_BITRATE_KBPS / 1000.0f);
      bool restart_ok = request_stream_restart(LOSS_RETRY_BITRATE_KBPS);
      if (restart_ok) {
        context.stream.loss_retry_attempts++;
        context.stream.loss_retry_bitrate_kbps = LOSS_RETRY_BITRATE_KBPS;
        context.stream.loss_retry_active = true;
        LOGD("Packet loss fallback scheduled (attempt %u, target %u kbps)",
             context.stream.loss_retry_attempts,
             context.stream.loss_retry_bitrate_kbps);
        if (context.active_host) {
          host_set_hint(context.active_host, hint, true, 7 * 1000 * 1000ULL);
        }
        return;
      } else {
        LOGE("Soft restart request failed; falling back to full reconnect");
      }
    }
    if (context.stream.loss_retry_attempts >= 1 || context.stream.fast_restart_active) {
      sceClibSnprintf(
          hint,
          sizeof(hint),
          "Severe packet loss — pausing stream");
      context.stream.reconnect_overlay_active = false;
    }
  } else if (auto_downgrade_latency_mode()) {
    downgraded = true;
    sceClibSnprintf(
        hint,
        sizeof(hint),
        "Network unstable — switching to %s preset",
        latency_mode_label(context.config.latency_mode));
    context.stream.reconnect_overlay_active = false;
  } else {
    sceClibSnprintf(
        hint,
        sizeof(hint),
        "Severe packet loss — pausing stream");
    context.stream.reconnect_overlay_active = false;
  }
  if (context.active_host) {
    host_set_hint(context.active_host, hint, true, 7 * 1000 * 1000ULL);
  }
  context.stream.inputs_resume_pending = true;
  request_stream_stop("packet loss");
}

static bool video_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user) {
  static bool first_frame = true;
  if (context.stream.stop_requested)
    return false;
  if (first_frame) {
    LOGD("VIDEO CALLBACK: First frame received (size=%zu)", buf_size);
    first_frame = false;
  }
  if (frames_lost > 0) {
    handle_loss_event(frames_lost, frame_recovered);
    bool restart_pending = handle_unrecovered_frame_loss(frames_lost, frame_recovered);
    if (restart_pending) {
      context.stream.is_streaming = false;
      return true;
    }
  }
  context.stream.is_streaming = true;
  if (ui_connection_overlay_active())
    ui_connection_complete();
  if (context.stream.reconnect_overlay_active)
    context.stream.reconnect_overlay_active = false;
  int err = vita_h264_decode_frame(buf, buf_size);
  if (err != 0) {
		LOGE("Error during video decode: %d", err);
    return false;
  }
  update_latency_metrics();
  return true;
}

static void set_ctrl_out(VitaChiakiStream *stream, VitakiCtrlOut ctrl_out) {
  if (ctrl_out == VITAKI_CTRL_OUT_L2) {
    stream->controller_state.l2_state = 0xff;
  } else if (ctrl_out == VITAKI_CTRL_OUT_R2) {
    stream->controller_state.r2_state = 0xff;
  } else {
    // in this case ctrl_out is a controller button mask
    //if (ctrl_out == VITAKI_CTRL_OUT_NONE) {
    //// do nothing
    //} else {
    stream->controller_state.buttons |= ctrl_out;
    //}
  }
}

void set_ctrl_l2pos(VitaChiakiStream *stream, VitakiCtrlIn ctrl_in) {
  // check if ctrl_in should be l2; if not, hit corresponding mapped button
  VitakiCtrlMapInfo vcmi = stream->vcmi;
  if (vcmi.in_l2 == ctrl_in) {
    stream->controller_state.l2_state = 0xff;
  } else {
    stream->controller_state.buttons |= vcmi.in_out_btn[ctrl_in];
  }
}
void set_ctrl_r2pos(VitaChiakiStream *stream, VitakiCtrlIn ctrl_in) {
  // check if ctrl_in should be r2; if not, hit corresponding mapped button
  VitakiCtrlMapInfo vcmi = stream->vcmi;
  if (vcmi.in_r2 == ctrl_in) {
    stream->controller_state.r2_state = 0xff;
  } else {
    stream->controller_state.buttons |= vcmi.in_out_btn[ctrl_in];
  }
}

static VitakiCtrlIn front_grid_input_from_touch(int x, int y, int max_w, int max_h) {
  if (x < 0 || y < 0)
    return VITAKI_CTRL_IN_NONE;
  if (x >= max_w)
    x = max_w - 1;
  if (y >= max_h)
    y = max_h - 1;
  int col = (x * VITAKI_FRONT_TOUCH_GRID_COLS) / max_w;
  int row = (y * VITAKI_FRONT_TOUCH_GRID_ROWS) / max_h;
  if (col < 0)
    col = 0;
  if (col >= VITAKI_FRONT_TOUCH_GRID_COLS)
    col = VITAKI_FRONT_TOUCH_GRID_COLS - 1;
  if (row < 0)
    row = 0;
  if (row >= VITAKI_FRONT_TOUCH_GRID_ROWS)
    row = VITAKI_FRONT_TOUCH_GRID_ROWS - 1;
  return (VitakiCtrlIn)(VITAKI_CTRL_IN_FRONTTOUCH_GRID_START + row * VITAKI_FRONT_TOUCH_GRID_COLS + col);
}

static void *input_thread_func(void* user) {
  // Set input thread to highest priority for lowest input lag
  // Pin to CPU1 to avoid contention with video/audio threads on CPU0
  // Priority 96 is higher than video (64) so input takes precedence
  sceKernelChangeThreadPriority(SCE_KERNEL_THREAD_ID_SELF, 96);
  sceKernelChangeThreadCpuAffinityMask(SCE_KERNEL_THREAD_ID_SELF, 0);

  sceMotionStartSampling();
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
  sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
  SceCtrlData ctrl;
  SceMotionState motion;
	VitaChiakiStream *stream = user;
  int ms_per_loop = 2;

  VitakiCtrlMapInfo vcmi = stream->vcmi;

  if (!vcmi.did_init) init_controller_map(&vcmi, context.config.controller_map_id);

  // Touchscreen setup
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
	sceTouchEnableTouchForce(SCE_TOUCH_PORT_FRONT);
	sceTouchEnableTouchForce(SCE_TOUCH_PORT_BACK);
	SceTouchData touch[SCE_TOUCH_PORT_MAX_NUM];
  int TOUCH_MAX_WIDTH = 1919;
  int TOUCH_MAX_HEIGHT = 1087;
  int TOUCH_MAX_WIDTH_BY_2 = TOUCH_MAX_WIDTH/2;
  int TOUCH_MAX_HEIGHT_BY_2 = TOUCH_MAX_HEIGHT/2;
  int TOUCH_MAX_WIDTH_BY_4 = TOUCH_MAX_WIDTH/4;
  int TOUCH_MAX_HEIGHT_BY_4 = TOUCH_MAX_HEIGHT/4;
  int FRONT_ARC_RADIUS = TOUCH_MAX_HEIGHT/3;
  int FRONT_ARC_RADIUS_2 = FRONT_ARC_RADIUS*FRONT_ARC_RADIUS;
  // note: rear touch may be active in Y only from 108 to 889? (see Vita3K code)

  // manually create bools for each combo (since there's only 5)
  bool vitaki_reartouch_left_l1_mapped = (vcmi.in_out_btn[VITAKI_CTRL_IN_REARTOUCH_LEFT_L1] != 0) || (vcmi.in_l2 == VITAKI_CTRL_IN_REARTOUCH_LEFT_L1);
  bool vitaki_reartouch_right_r1_mapped = (vcmi.in_out_btn[VITAKI_CTRL_IN_REARTOUCH_RIGHT_R1] != 0) || (vcmi.in_r2 == VITAKI_CTRL_IN_REARTOUCH_RIGHT_R1);
  bool vitaki_select_start_mapped = (vcmi.in_out_btn[VITAKI_CTRL_IN_SELECT_START] != 0);
  bool vitaki_left_square_mapped = (vcmi.in_out_btn[VITAKI_CTRL_IN_LEFT_SQUARE] != 0) || (vcmi.in_l2 == VITAKI_CTRL_IN_LEFT_SQUARE);
  bool vitaki_right_circle_mapped = (vcmi.in_out_btn[VITAKI_CTRL_IN_RIGHT_CIRCLE] != 0) || (vcmi.in_r2 == VITAKI_CTRL_IN_RIGHT_CIRCLE);

  static int exit_combo_hold = 0;
  const int EXIT_COMBO_THRESHOLD = 500;  // ~1s with 2ms loop
  static uint32_t controller_seq_counter = 0;
  const uint64_t INPUT_STALL_THRESHOLD_US = 300000; // 0.3s without send
  const uint64_t INPUT_STALL_LOG_INTERVAL_US = 1000000;

  if (context.stream.cached_controller_valid) {
    stream->controller_state = context.stream.cached_controller_state;
    context.stream.cached_controller_valid = false;
  }
  while (true) {

    // TODO enable using triggers as L2, R2
    // TODO enable home button, with long hold sent back to Vita?


    // Keep controller packets flowing when Chiaki performs a fast restart so
    // packet-loss fallbacks do not stall local inputs.
    bool controller_gate_open =
        stream->inputs_ready ||
        (stream->fast_restart_active && stream->session_init && !stream->stop_requested);

    if (!controller_gate_open) {
      uint64_t now_us = sceKernelGetProcessTimeWide();
      if (!context.stream.inputs_blocked_since_us)
        context.stream.inputs_blocked_since_us = now_us;
      uint64_t delta_since_block =
          now_us - context.stream.inputs_blocked_since_us;
      uint64_t delta_since_send =
          context.stream.last_input_packet_us ?
          (now_us - context.stream.last_input_packet_us) : 0;
      uint64_t observed = delta_since_send ? delta_since_send : delta_since_block;
      if (observed >= INPUT_STALL_THRESHOLD_US) {
        if (!context.stream.last_input_stall_log_us ||
            now_us - context.stream.last_input_stall_log_us >= INPUT_STALL_LOG_INTERVAL_US) {
          float ms = (float)observed / 1000.0f;
          LOGD("INPUT THREAD: controller packets waiting for Chiaki (%.2f ms since last activity)", ms);
          context.stream.last_input_stall_log_us = now_us;
        }
      }
    } else {
      context.stream.inputs_blocked_since_us = 0;
    }

    if (controller_gate_open) {
      int start_time_us = sceKernelGetProcessTimeWide();

      // get button state
      sceCtrlPeekBufferPositive(0, &ctrl, 1);

      bool exit_combo = (ctrl.buttons & SCE_CTRL_LTRIGGER) &&
                        (ctrl.buttons & SCE_CTRL_RTRIGGER) &&
                        (ctrl.buttons & SCE_CTRL_START);
      if (exit_combo && stream->session_init && !stream->stop_requested) {
        exit_combo_hold++;
        if (exit_combo_hold >= EXIT_COMBO_THRESHOLD) {
          request_stream_stop("L+R+Start");
          exit_combo_hold = 0;
          continue;
        }
      } else {
        exit_combo_hold = 0;
      }

      if (stream->stop_requested) {
        usleep(ms_per_loop * 1000);
        continue;
      }

      // get touchscreen state
      for(int port = 0; port < SCE_TOUCH_PORT_MAX_NUM; port++) {
        sceTouchPeek(port, &touch[port], 1);
      }

      // get gyro/accel state
      sceMotionGetState(&motion);
      stream->controller_state.accel_x = motion.acceleration.x;
      stream->controller_state.accel_y = motion.acceleration.y;
      stream->controller_state.accel_z = motion.acceleration.z;

      stream->controller_state.orient_x = motion.deviceQuat.x;
      stream->controller_state.orient_y = motion.deviceQuat.y;
      stream->controller_state.orient_z = motion.deviceQuat.z;
      stream->controller_state.orient_w = motion.deviceQuat.w;

      stream->controller_state.gyro_x = motion.angularVelocity.x;
      stream->controller_state.gyro_y = motion.angularVelocity.y;
      stream->controller_state.gyro_z = motion.angularVelocity.z;

      // 0-255 conversion
      stream->controller_state.left_x = (ctrl.lx - 128) * 2 * 0x7F/*.FF*/;
      stream->controller_state.left_y = (ctrl.ly - 128) * 2 * 0x7F/*.FF*/;
      stream->controller_state.right_x = (ctrl.rx - 128) * 2 * 0x7F/*.FF*/;
      stream->controller_state.right_y = (ctrl.ry - 128) * 2 * 0x7F/*.FF*/;

      stream->controller_state.buttons = 0x00;
      stream->controller_state.l2_state = 0x00;
      stream->controller_state.r2_state = 0x00;

      bool reartouch_right = false;
      bool reartouch_left = false;

      for (int touch_i = 0; touch_i < touch[SCE_TOUCH_PORT_BACK].reportNum; touch_i++) {
        int x = touch[SCE_TOUCH_PORT_BACK].report[touch_i].x;
        int y = touch[SCE_TOUCH_PORT_BACK].report[touch_i].y;

        stream->controller_state.buttons |= vcmi.in_out_btn[VITAKI_CTRL_IN_REARTOUCH_ANY];

        if (x > TOUCH_MAX_WIDTH_BY_2) {
          set_ctrl_r2pos(stream, VITAKI_CTRL_IN_REARTOUCH_RIGHT);
          reartouch_right = true;
          if (y > TOUCH_MAX_HEIGHT_BY_2) {
            set_ctrl_r2pos(stream, VITAKI_CTRL_IN_REARTOUCH_LR);
          } else {
            set_ctrl_r2pos(stream, VITAKI_CTRL_IN_REARTOUCH_UR);
          }
        } else if (x < TOUCH_MAX_WIDTH_BY_2) {
          set_ctrl_l2pos(stream, VITAKI_CTRL_IN_REARTOUCH_LEFT);
          reartouch_left = true;
          if (y > TOUCH_MAX_HEIGHT_BY_2) {
            set_ctrl_l2pos(stream, VITAKI_CTRL_IN_REARTOUCH_LL);
          } else {
            set_ctrl_l2pos(stream, VITAKI_CTRL_IN_REARTOUCH_UL);
          }
        }
      }

      for (int touch_i = 0; touch_i < touch[SCE_TOUCH_PORT_FRONT].reportNum; touch_i++) {
        int x = touch[SCE_TOUCH_PORT_FRONT].report[touch_i].x;
        int y = touch[SCE_TOUCH_PORT_FRONT].report[touch_i].y;
        stream->controller_state.buttons |= vcmi.in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_ANY];

        VitakiCtrlIn grid_input = front_grid_input_from_touch(x, y, TOUCH_MAX_WIDTH, TOUCH_MAX_HEIGHT);
        if (grid_input != VITAKI_CTRL_IN_NONE) {
          VitakiCtrlOut mapped = vcmi.in_out_btn[grid_input];
          if (mapped == VITAKI_CTRL_OUT_L2) {
            stream->controller_state.l2_state = 0xff;
          } else if (mapped == VITAKI_CTRL_OUT_R2) {
            stream->controller_state.r2_state = 0xff;
          } else if (mapped != VITAKI_CTRL_OUT_NONE) {
            stream->controller_state.buttons |= mapped;
          }
        }

        if (x > TOUCH_MAX_WIDTH_BY_2) {
          set_ctrl_r2pos(stream, VITAKI_CTRL_IN_FRONTTOUCH_RIGHT);

          if (y*y + (x-TOUCH_MAX_WIDTH)*(x-TOUCH_MAX_WIDTH) <= FRONT_ARC_RADIUS_2) {
            set_ctrl_r2pos(stream, VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC);
          } else if ((y-TOUCH_MAX_HEIGHT)*(y-TOUCH_MAX_HEIGHT) + (x-TOUCH_MAX_WIDTH)*(x-TOUCH_MAX_WIDTH) <= FRONT_ARC_RADIUS_2) {
            set_ctrl_r2pos(stream, VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC);
          }
        } else if (x < TOUCH_MAX_WIDTH_BY_2) {
          set_ctrl_l2pos(stream, VITAKI_CTRL_IN_FRONTTOUCH_LEFT);

          if (y*y + x*x <= FRONT_ARC_RADIUS_2) {
            set_ctrl_l2pos(stream, VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC);
          } else if ((y-TOUCH_MAX_HEIGHT)*(y-TOUCH_MAX_HEIGHT) + x*x <= FRONT_ARC_RADIUS_2) {
            set_ctrl_l2pos(stream, VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC);
          }
        }

        if ((x >= TOUCH_MAX_WIDTH_BY_4) && (x <= TOUCH_MAX_WIDTH - TOUCH_MAX_WIDTH_BY_4)
            && (y >= TOUCH_MAX_HEIGHT_BY_4) && (y <= TOUCH_MAX_HEIGHT - TOUCH_MAX_HEIGHT_BY_4)
            ) {
          stream->controller_state.buttons |= vcmi.in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_CENTER];
        }
      }

      // cursed conversion
      if (ctrl.buttons & SCE_CTRL_SELECT)   stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_SHARE;
      if (ctrl.buttons & SCE_CTRL_START)    stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_OPTIONS;
      if (ctrl.buttons & SCE_CTRL_UP)       stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
      if (ctrl.buttons & SCE_CTRL_RIGHT)    stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
      if (ctrl.buttons & SCE_CTRL_DOWN)     stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
      if (ctrl.buttons & SCE_CTRL_LEFT)     stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
      if (ctrl.buttons & SCE_CTRL_TRIANGLE) stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_PYRAMID;
      if (ctrl.buttons & SCE_CTRL_CIRCLE)   stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_MOON;
      if (ctrl.buttons & SCE_CTRL_CROSS)    stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_CROSS;
      if (ctrl.buttons & SCE_CTRL_SQUARE)   stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_BOX;
      // what is L3??
      if (ctrl.buttons & SCE_CTRL_L3)       stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_L3;
      if (ctrl.buttons & SCE_CTRL_R3)       stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_R3;

      if (ctrl.buttons & SCE_CTRL_LTRIGGER) {
        if (reartouch_left && vitaki_reartouch_left_l1_mapped) {
          set_ctrl_l2pos(stream, VITAKI_CTRL_IN_REARTOUCH_LEFT_L1);
        } else {
          set_ctrl_l2pos(stream, VITAKI_CTRL_IN_L1);
        }
      }
      if (ctrl.buttons & SCE_CTRL_RTRIGGER) {
        if (reartouch_right && vitaki_reartouch_right_r1_mapped) {
          set_ctrl_r2pos(stream, VITAKI_CTRL_IN_REARTOUCH_RIGHT_R1);
        } else {
          set_ctrl_r2pos(stream, VITAKI_CTRL_IN_R1);
        }
      }

      // Select + Start
      if (vitaki_select_start_mapped) {
        if ((ctrl.buttons & SCE_CTRL_SELECT) && (ctrl.buttons & SCE_CTRL_START)) {
          stream->controller_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_SHARE;
          stream->controller_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_OPTIONS;
          stream->controller_state.buttons |= vcmi.in_out_btn[VITAKI_CTRL_IN_SELECT_START];
        }
      }

      // Dpad-left + Square
      if (vitaki_left_square_mapped) {
        if ((ctrl.buttons & SCE_CTRL_LEFT) && (ctrl.buttons & SCE_CTRL_SQUARE)) {
          stream->controller_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
          stream->controller_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_BOX;
          set_ctrl_l2pos(stream, VITAKI_CTRL_IN_LEFT_SQUARE);
        }
      }
      // Dpad-right + Circle
      if (vitaki_right_circle_mapped) {
        if ((ctrl.buttons & SCE_CTRL_RIGHT) && (ctrl.buttons & SCE_CTRL_CIRCLE)) {
          stream->controller_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
          stream->controller_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_MOON;
          set_ctrl_r2pos(stream, VITAKI_CTRL_IN_RIGHT_CIRCLE);
        }
      }

      chiaki_session_set_controller_state(&stream->session, &stream->controller_state);
      context.stream.cached_controller_state = stream->controller_state;
      context.stream.cached_controller_valid = true;
      context.stream.last_input_packet_us = sceKernelGetProcessTimeWide();
      context.stream.last_input_stall_log_us = 0;
      controller_seq_counter++;
      if ((controller_seq_counter % 500) == 0) {
        LOGD("Controller send seq %u (Vita)", controller_seq_counter);
      }
      // LOGD("ly 0x%x %d", ctrl.ly, ctrl.ly);

      // Adjust sleep time to account for calculations above
      int diff_time_us = sceKernelGetProcessTimeWide() - start_time_us;
      if (diff_time_us < ms_per_loop*1000)
        usleep(ms_per_loop*1000 - diff_time_us);

    } else {
      usleep(1000);  // Sleep 1ms to avoid tight spin
    }
  }

  return 0;
}

int host_stream(VitaChiakiHost* host) {
  LOGD("Preparing to start host_stream");
  if (!host->hostname || !host->registered_state) {
    return 1;
  }
  if (context.stream.session_init) {
    LOGD("Stream already initialized; ignoring duplicate start request");
    return 1;
  }
  bool discovery_was_running = context.discovery_enabled;
  context.discovery_resume_after_stream = false;
  host_set_hint(host, NULL, false, 0);

  int result = 1;
  bool resume_inputs = context.stream.inputs_resume_pending;
  context.stream.stop_requested = false;
  context.stream.inputs_ready = false;
  context.stream.is_streaming = false;
  context.stream.media_initialized = false;

  uint64_t now_us = sceKernelGetProcessTimeWide();
  if (context.stream.next_stream_allowed_us &&
      now_us < context.stream.next_stream_allowed_us) {
    uint64_t remaining_ms =
        (context.stream.next_stream_allowed_us - now_us + 999) / 1000;
    LOGD("Stream start blocked for %llu ms to let console recover", remaining_ms);
    goto cleanup;
  }

  ChiakiConnectVideoProfile profile = {};
	chiaki_connect_video_profile_preset(&profile,
		context.config.resolution, context.config.fps);
  apply_latency_mode(&profile, context.config.latency_mode);
  if (context.stream.loss_retry_active && context.stream.loss_retry_bitrate_kbps > 0) {
    profile.bitrate = context.stream.loss_retry_bitrate_kbps;
    LOGD("Applying packet-loss fallback bitrate: %u kbps", profile.bitrate);
    context.stream.loss_retry_active = false;
  }
  uint32_t negotiated = profile.max_fps;
  if (negotiated == 0)
    negotiated = 60;
  context.stream.negotiated_fps = negotiated;
  uint32_t clamp_fps = negotiated;
  if (context.config.force_30fps)
    clamp_fps = clamp_fps > 30 ? 30 : clamp_fps;
  context.stream.target_fps = clamp_fps;
  context.stream.measured_incoming_fps = 0;
  context.stream.fps_window_start_us = 0;
  context.stream.fps_window_frame_count = 0;
  context.stream.pacing_accumulator = 0;

  ui_connection_set_stage(UI_CONNECTION_STAGE_CONNECTING);
  ChiakiConnectInfo chiaki_connect_info = {};
	chiaki_connect_info.host = host->hostname;
	chiaki_connect_info.video_profile = profile;
	chiaki_connect_info.video_profile_auto_downgrade = true;
	chiaki_connect_info.send_actual_start_bitrate = context.config.send_actual_start_bitrate;
	chiaki_connect_info.ps5 = chiaki_target_is_ps5(host->target);
	memcpy(chiaki_connect_info.regist_key, host->registered_state->rp_regist_key, sizeof(chiaki_connect_info.regist_key));
	memcpy(chiaki_connect_info.morning, host->registered_state->rp_key, sizeof(chiaki_connect_info.morning));
  if (context.stream.cached_controller_valid) {
    chiaki_connect_info.cached_controller_state = context.stream.cached_controller_state;
    chiaki_connect_info.cached_controller_state_valid = true;
  } else {
	chiaki_controller_state_set_idle(&chiaki_connect_info.cached_controller_state);
	chiaki_connect_info.cached_controller_state_valid = false;
	}

	LOGD("Initializing Chiaki session (host=%s, bitrate=%u kbps, fps=%u)",
	     host->hostname ? host->hostname : "<null>",
	     profile.bitrate,
	     profile.max_fps);

	ChiakiErrorCode err = chiaki_session_init(&context.stream.session, &chiaki_connect_info, &context.log);
	if(err != CHIAKI_ERR_SUCCESS) {
    if (err == CHIAKI_ERR_PARSE_ADDR) {
      LOGE("Error during stream setup: console address unresolved; keeping discovery active");
      host_set_hint(host, "Waiting for console network link...", false, 3 * 1000 * 1000ULL);
    } else {
		  LOGE("Error during stream setup: %s", chiaki_error_string(err));
    }
    goto cleanup;
  }
  if (resume_inputs && err == CHIAKI_ERR_SUCCESS) {
    context.stream.inputs_ready = true;
    context.stream.inputs_resume_pending = false;
  }

  if (discovery_was_running) {
    LOGD("Suspending discovery during stream");
    stop_discovery(true);
    context.discovery_resume_after_stream = true;
  }
	init_controller_map(&(context.stream.vcmi), context.config.controller_map_id);
 	context.stream.session_init = true;
	reset_stream_metrics(false);
	LOGD("Chiaki session initialized successfully, starting media pipeline");
	ChiakiAudioSink audio_sink;
	chiaki_opus_decoder_init(&context.stream.opus_decoder, &context.log);
	chiaki_opus_decoder_set_cb(&context.stream.opus_decoder, vita_audio_init, vita_audio_cb, NULL);
	chiaki_opus_decoder_get_sink(&context.stream.opus_decoder, &audio_sink);
	chiaki_session_set_audio_sink(&context.stream.session, &audio_sink);
  context.stream.media_initialized = true;
	chiaki_session_set_video_sample_cb(&context.stream.session, video_cb, NULL);
	chiaki_session_set_event_cb(&context.stream.session, event_cb, NULL);
	chiaki_controller_state_set_idle(&context.stream.controller_state);

  err = vita_h264_setup(profile.width, profile.height);
  if (err != 0) {
		LOGE("Error during video start: %d", err);
    goto cleanup;
  }
  vita_h264_start();

  err = chiaki_session_start(&context.stream.session);
  if(err != CHIAKI_ERR_SUCCESS) {
		LOGE("Error during stream start: %s", chiaki_error_string(err));
    goto cleanup;
  }

	err = chiaki_thread_create(&context.stream.input_thread, input_thread_func, &context.stream);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		LOGE("Failed to create input thread");
	}

  result = 0;

cleanup:
  if (result != 0) {
    context.stream.inputs_resume_pending = false;
    shutdown_media_pipeline();
    context.stream.session_init = false;
    context.stream.fast_restart_active = false;
    context.stream.reconnect_overlay_active = false;
    context.stream.loss_retry_active = false;
    context.stream.loss_retry_pending = false;
    context.stream.is_streaming = false;
    context.stream.inputs_ready = false;
    resume_discovery_if_needed();
    ui_connection_cancel();
  } else if (resume_inputs) {
    context.stream.inputs_ready = true;
    context.stream.inputs_resume_pending = false;
  }
  return result;
}

/// Check if two MAC addresses match
bool mac_addrs_match(MacAddr* a, MacAddr* b) {
  for (int j = 0; j < 6; j++) {
    if ((*a)[j] != (*b)[j]) {
      return false;
    }
  }
  return true;
}

/// Save a new manual host into the context, given existing registered host and new remote ip ("hostname")
void save_manual_host(VitaChiakiHost* rhost, char* new_hostname) {
  if ((!rhost->server_mac)) {
    CHIAKI_LOGE(&(context.log), "Failed to get registered host mac; could not save.");
  }

  for (int i = 0; i < context.config.num_manual_hosts; i++) {
    VitaChiakiHost* h = context.config.manual_hosts[i];
    if (mac_addrs_match(&(h->server_mac), &(rhost->server_mac))) {
      if (strcmp(h->hostname, new_hostname) == 0) {
        // this manual host already exists (same mac addr and hostname)
        CHIAKI_LOGW(&(context.log), "Duplicate manual host. Not saving.");
        return;
      }
    }
  }

  VitaChiakiHost* newhost = (VitaChiakiHost*)malloc(sizeof(VitaChiakiHost));
  copy_host(newhost, rhost, false);
  newhost->hostname = strdup(new_hostname);
  newhost->type = REGISTERED | MANUALLY_ADDED;

  CHIAKI_LOGI(&(context.log), "--");
  CHIAKI_LOGI(&(context.log), "Adding manual host:");

  if(newhost->hostname)
    CHIAKI_LOGI(&(context.log), "Host Name (address):               %s", newhost->hostname);
  if(newhost->server_mac) {
    CHIAKI_LOGI(&(context.log), "Host MAC:                          %X%X%X%X%X%X\n", newhost->server_mac[0], newhost->server_mac[1], newhost->server_mac[2], newhost->server_mac[3], newhost->server_mac[4], newhost->server_mac[5]);
  }
  CHIAKI_LOGI(&(context.log),   "Is PS5:                            %s", chiaki_target_is_ps5(newhost->target) ? "true" : "false");

  CHIAKI_LOGI(&(context.log), "--");

  if (context.config.num_manual_hosts >= MAX_NUM_HOSTS) { // TODO change to manual max
    CHIAKI_LOGE(&(context.log), "Max manual hosts reached; could not save.");
    return;
  }

  // Save to manual hosts
  context.config.manual_hosts[context.config.num_manual_hosts++] = newhost;

  // Save config
  config_serialize(&context.config);

  LOGD("> UPDATE CONTEXT...");
  // update hosts in context
  update_context_hosts();
  LOGD("> UPDATE CONTEXT DONE");
}


void delete_manual_host(VitaChiakiHost* mhost) {

  for (int i = 0; i < context.config.num_manual_hosts; i++) {
    VitaChiakiHost* h = context.config.manual_hosts[i];
    if (h == mhost) { // same object
      context.config.manual_hosts[i] = NULL;
    }
  }
  host_free(mhost);

  // reorder manual hosts
  for (int i = 0; i < context.config.num_manual_hosts; i++) {
    VitaChiakiHost* h = context.config.manual_hosts[i];
    if (!h) {
      for (int j = i+1; j < context.config.num_manual_hosts; j++) {
        context.config.manual_hosts[j-1] = context.config.manual_hosts[j];
      }
      context.config.manual_hosts[context.config.num_manual_hosts-1] = NULL;
      context.config.num_manual_hosts--;
    }
  }

  // Save config
  config_serialize(&context.config);

  // update hosts in context
  update_context_hosts();

}

int count_nonnull_context_hosts() {
  int sum = 0;
  for (int host_idx = 0; host_idx < MAX_NUM_HOSTS; host_idx++) {
    VitaChiakiHost *h = context.hosts[host_idx];
    if (h) {
      sum += 1;
    }
  }
  return sum;
}

void update_context_hosts() {
  bool hide_remote_if_discovered = true;

  // Remove any no-longer-existent manual hosts
  for (int host_idx = 0; host_idx < MAX_NUM_HOSTS; host_idx++) {
    VitaChiakiHost* h = context.hosts[host_idx];
    if (h && (h->type & MANUALLY_ADDED)) {

      // check if this host still exists
      bool host_exists = false;
      for (int i = 0; i < context.config.num_manual_hosts; i++) {
        if (context.config.manual_hosts[i] == h) {
          host_exists = true;
          break;
        }
      }
      if (!host_exists) {
        context.hosts[host_idx] = NULL;
      }
    }
  }

  // Remove any manual hosts matching discovered hosts
  if (hide_remote_if_discovered) {
    for (int i = 0; i < MAX_NUM_HOSTS; i++) {
      VitaChiakiHost* mhost = context.hosts[i];
      if (!(mhost && mhost->server_mac && (mhost->type & MANUALLY_ADDED))) continue;
      for (int j = 0; j < MAX_NUM_HOSTS; j++) {
        if (j == i) continue;
        VitaChiakiHost* h = context.hosts[j];
        if (!(h && h->server_mac && (h->type & DISCOVERED) && !(h->type & MANUALLY_ADDED))) continue;
        if (mac_addrs_match(&(h->server_mac), &(mhost->server_mac))) {
          context.hosts[i] = NULL;
        }
      }
    }
  }


  // Remove any empty slots
  for (int host_idx = 0; host_idx < MAX_NUM_HOSTS; host_idx++) {
      VitaChiakiHost* h = context.hosts[host_idx];
      if (!h) {
        // slide all hosts back one slot
        for (int j = host_idx+1; j < MAX_NUM_HOSTS; j++) {
          context.hosts[j-1] = context.hosts[j];
        }
        context.hosts[MAX_NUM_HOSTS-1] = NULL;
      }
  }

  // Add in manual hosts
  for (int i = 0; i < context.config.num_manual_hosts; i++) {
    VitaChiakiHost* mhost = context.config.manual_hosts[i];

    // first, check if it (or the local discovered version of the same console) is already in context
    bool already_in_context = false;
    for (int host_idx = 0; host_idx < MAX_NUM_HOSTS; host_idx++) {
      VitaChiakiHost* h = context.hosts[host_idx];
      if (!h) continue;
      if ((!h->server_mac) || (!h->hostname)) continue;
      if (mac_addrs_match(&(h->server_mac), &(mhost->server_mac))) {
        // it's the same console

        if ((h->type & DISCOVERED) && hide_remote_if_discovered) {
          // found matching discovered console
          already_in_context = true;
          break;
        }

        if ((h->type & MANUALLY_ADDED) && (strcmp(h->hostname, mhost->hostname) == 0)) {
          // found identical manual console
          already_in_context = true;
          break;
        }
      }
    }

    if (already_in_context) {
      continue;
    }

    // the host is not in the context yet. Find an empty spot for it, if possible.
    bool added_to_context = false;
    for (int host_idx = 0; host_idx < MAX_NUM_HOSTS; host_idx++) {
      VitaChiakiHost* h = context.hosts[host_idx];
      if (h == NULL) {
        // empty spot
        context.hosts[host_idx] = mhost;
        added_to_context = true;
        break;
      }
    }

    if (!added_to_context) {
      CHIAKI_LOGE(&(context.log), "Max # of hosts reached; could not add manual host %d to context.", i);
    }

  }

  // Update num_hosts
  context.num_hosts = count_nonnull_context_hosts();
}

int count_manual_hosts_of_console(VitaChiakiHost* host) {
  if (!host) return 0;
  if (!host->server_mac) return 0;
  int sum = 0;
  for (int i = 0; i < context.config.num_manual_hosts; i++) {
    VitaChiakiHost* mhost = context.config.manual_hosts[i];
    if (!mhost) continue;
    if (!mhost->server_mac) continue;
    /*LOGD("CHECKING %X%X%X%X%X%X vs %X%X%X%X%X%X",
         host->server_mac[0], host->server_mac[1], host->server_mac[2], host->server_mac[3], host->server_mac[4], host->server_mac[5],
         mhost->server_mac[0], mhost->server_mac[1], mhost->server_mac[2], mhost->server_mac[3], mhost->server_mac[4], mhost->server_mac[5]
         );*/
    if (mac_addrs_match(&(host->server_mac), &(mhost->server_mac))) {
      sum++;
    }
  }
  return sum;
}

void copy_host(VitaChiakiHost* h_dest, VitaChiakiHost* h_src, bool copy_hostname) {
        h_dest->type = h_src->type;
        h_dest->target = h_src->target;
        if (h_src->server_mac) {
          memcpy(&h_dest->server_mac, &(h_src->server_mac), 6);
        }

        h_dest->hostname = NULL;
        if ((h_src->hostname) && copy_hostname) {
          h_dest->hostname = strdup(h_src->hostname);
        }

        // copy registered state
        h_dest->registered_state = NULL;
        ChiakiRegisteredHost* rstate_src = h_src->registered_state;
        if (rstate_src) {
          ChiakiRegisteredHost* rstate_dest = malloc(sizeof(ChiakiRegisteredHost));
          h_dest->registered_state = rstate_dest;
          copy_host_registered_state(rstate_dest, rstate_src);
        }

        // don't copy discovery state
        h_dest->discovery_state = NULL;
        if (h_src->status_hint[0]) {
          sceClibSnprintf(h_dest->status_hint, sizeof(h_dest->status_hint), "%s", h_src->status_hint);
        } else {
          h_dest->status_hint[0] = '\0';
        }
        h_dest->status_hint_is_error = h_src->status_hint_is_error;
        h_dest->status_hint_expire_us = h_src->status_hint_expire_us;
}

void copy_host_registered_state(ChiakiRegisteredHost* rstate_dest, ChiakiRegisteredHost* rstate_src) {
  if (rstate_src) {
    if (rstate_src->server_nickname) {
      strncpy(rstate_dest->server_nickname, rstate_src->server_nickname, sizeof(rstate_dest->server_nickname));
    }
    rstate_dest->target = rstate_src->target;
    memcpy(rstate_dest->rp_key, rstate_src->rp_key, sizeof(rstate_dest->rp_key));
    rstate_dest->rp_key_type = rstate_src->rp_key_type;
    memcpy(rstate_dest->rp_regist_key, rstate_src->rp_regist_key, sizeof(rstate_dest->rp_regist_key));
  }
}
