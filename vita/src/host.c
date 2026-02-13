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
static bool request_stream_restart_coordinated(const char *source,
                                               uint32_t bitrate_kbps,
                                               uint64_t now_us);
static void resume_discovery_if_needed(void);
static void host_set_hint(VitaChiakiHost *host, const char *msg, bool is_error, uint64_t duration_us);
static void handle_loss_event(int32_t frames_lost, bool frame_recovered);
static bool handle_unrecovered_frame_loss(int32_t frames_lost, bool frame_recovered);
static void handle_takion_overflow(void);
static bool auto_downgrade_latency_mode(void);
static const char *latency_mode_label(VitaChiakiLatencyMode mode);
static bool unrecovered_loss_has_av_distress(const char **reason_out);
static void shutdown_media_pipeline(void);
static void finalize_session_resources(void);
static uint32_t clamp_u32(uint32_t value, uint32_t min_value, uint32_t max_value);
static uint64_t remaining_ms_until(uint64_t deadline_us, uint64_t now_us);
static void request_decoder_resync(const char *reason);
static void handle_post_reconnect_degraded_mode(bool av_diag_progressed,
                                                uint32_t incoming_fps,
                                                uint32_t target_fps,
                                                bool low_fps_window,
                                                uint64_t now_us);
static const char *quit_reason_label(ChiakiQuitReason reason);
static bool quit_reason_requires_retry(ChiakiQuitReason reason);
static void update_disconnect_banner(const char *reason);
static void persist_config_or_warn(void);
static const char *restart_source_label(const char *source);
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
#define LOSS_RECOVERY_WINDOW_US (8 * 1000 * 1000ULL)
#define LOSS_RECOVERY_ACTION_COOLDOWN_US (10 * 1000 * 1000ULL)
// Startup can include console wake + decoder warmup. Keep a short grace for
// burst suppression and a longer hard grace for severe unrecovered churn.
#define LOSS_RESTART_STARTUP_SOFT_GRACE_US (2500 * 1000ULL)
#define LOSS_RESTART_STARTUP_HARD_GRACE_US (20 * 1000 * 1000ULL)
#define AV_DIAG_LOG_INTERVAL_US (5 * 1000 * 1000ULL)
#define UNRECOVERED_FRAME_THRESHOLD 3
// Require multiple unrecovered bursts before escalating to restart logic.
#define UNRECOVERED_FRAME_GATE_THRESHOLD 4
// Use a wider gate window so single transient bursts don't immediately escalate.
#define UNRECOVERED_FRAME_GATE_WINDOW_US (2500 * 1000ULL)
#define UNRECOVERED_PERSIST_WINDOW_US (8 * 1000 * 1000ULL)
#define UNRECOVERED_PERSIST_THRESHOLD 6
#define UNRECOVERED_IDR_WINDOW_US (8 * 1000 * 1000ULL)
#define UNRECOVERED_IDR_INEFFECTIVE_THRESHOLD 5
#define RESTART_FAILURE_COOLDOWN_US (5000 * 1000ULL)
#define FAST_RESTART_GRACE_DELAY_US (200 * 1000ULL)
#define FAST_RESTART_RETRY_DELAY_US (250 * 1000ULL)
#define FAST_RESTART_MAX_ATTEMPTS 2
#define LOSS_COUNTER_SATURATED_WINDOW_FRAMES (1u << 0)
#define LOSS_COUNTER_SATURATED_BURST_FRAMES  (1u << 1)
// After this many consecutive missed diagnostics snapshots, low-FPS windows
// are treated as AV distress so recovery won't remain blind under contention.
#define AV_DIAG_STALE_SNAPSHOT_WARN_STREAK 5
// RP_IN_USE can persist briefly after wake/quit; hold retries long enough to
// avoid immediate reconnect churn observed in hardware testing.
#define RETRY_HOLDOFF_RP_IN_USE_MS 9000
#define RECONNECT_RECOVER_LOW_FPS_TRIGGER_WINDOWS 6
#define RECONNECT_RECOVER_ACTION_COOLDOWN_US (2 * 1000 * 1000ULL)
#define RECONNECT_RECOVER_STAGE2_WAIT_US (8 * 1000 * 1000ULL)
// Stage-2 reconnect recovery uses a conservative bitrate that stays below the
// unstable 540p startup envelope on Vita while preserving playability.
#define RECONNECT_RECOVER_TARGET_KBPS 900
#define RECONNECT_RECOVER_MIN_HEALTHY_FPS 27
// Never let soft restarts ask the console for more than ~1.5 Mbps or the Vita
// Wi-Fi path risks oscillating into unsustainable bitrates.
#define FAST_RESTART_BITRATE_CAP_KBPS 1500
#define RESTART_HANDSHAKE_COOLOFF_FIRST_US (8 * 1000 * 1000ULL)
#define RESTART_HANDSHAKE_COOLOFF_REPEAT_US (12 * 1000 * 1000ULL)
#define RESTART_HANDSHAKE_REPEAT_WINDOW_US (60 * 1000 * 1000ULL)
#define SESSION_START_LOW_FPS_WINDOW_US (60 * 1000 * 1000ULL)
#define RETRY_FAIL_DELAY_US (5 * 1000 * 1000ULL)
#define HINT_DURATION_LINK_WAIT_US (3 * 1000 * 1000ULL)
#define HINT_DURATION_KEYFRAME_US (4 * 1000 * 1000ULL)
#define HINT_DURATION_RECOVERY_US (5 * 1000 * 1000ULL)
#define HINT_DURATION_ERROR_US (7 * 1000 * 1000ULL)
#define DISCONNECT_BANNER_DEFAULT_US HINT_DURATION_LINK_WAIT_US
#define LOSS_PROFILE_BURST_BASE_US (200 * 1000ULL)
#define LOSS_PROFILE_BURST_LOW_US (220 * 1000ULL)
#define LOSS_PROFILE_BURST_BALANCED_US (240 * 1000ULL)
#define LOSS_PROFILE_BURST_HIGH_US (260 * 1000ULL)
#define LOSS_PROFILE_BURST_MAX_US (280 * 1000ULL)
#define LOSS_PROFILE_WINDOW_LOW_US (5 * 1000 * 1000ULL)
#define LOSS_PROFILE_WINDOW_BALANCED_US (7 * 1000 * 1000ULL)
#define LOSS_PROFILE_WINDOW_HIGH_US (9 * 1000 * 1000ULL)
#define LOSS_PROFILE_WINDOW_MAX_US (10 * 1000 * 1000ULL)

typedef enum ReconnectRecoveryStage {
  RECONNECT_RECOVER_STAGE_IDLE = 0,
  RECONNECT_RECOVER_STAGE_IDR_REQUESTED = 1,
  RECONNECT_RECOVER_STAGE_SOFT_RESTARTED = 2,
  RECONNECT_RECOVER_STAGE_ESCALATED = 3,
} ReconnectRecoveryStage;

static void persist_config_or_warn(void) {
  if (!config_serialize(&context.config)) {
    LOGE("Failed to persist config changes");
  }
}

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

    persist_config_or_warn();
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
	      context.stream.stream_start_us = sceKernelGetProcessTimeWide();
	      context.stream.loss_restart_soft_grace_until_us =
	          context.stream.stream_start_us + LOSS_RESTART_STARTUP_SOFT_GRACE_US;
	      context.stream.loss_restart_grace_until_us =
	          context.stream.stream_start_us + LOSS_RESTART_STARTUP_HARD_GRACE_US;
      if (context.stream.reconnect_generation > 0) {
        context.stream.post_reconnect_window_until_us =
            context.stream.stream_start_us + SESSION_START_LOW_FPS_WINDOW_US;
      } else {
        context.stream.post_reconnect_window_until_us = 0;
      }
      context.stream.inputs_ready = true;
      context.stream.next_stream_allowed_us = 0;
      context.stream.retry_holdoff_ms = 0;
      context.stream.retry_holdoff_until_us = 0;
      context.stream.retry_holdoff_active = false;
      context.stream.restart_handshake_failures = 0;
      context.stream.last_restart_handshake_fail_us = 0;
      context.stream.restart_cooloff_until_us = 0;
      context.stream.last_restart_source[0] = '\0';
      context.stream.restart_source_attempts = 0;
      LOGD("PIPE/SESSION connected gen=%u reconnect_gen=%u post_window_ms=%llu",
           context.stream.session_generation,
           context.stream.reconnect_generation,
           context.stream.post_reconnect_window_until_us ?
               (unsigned long long)((context.stream.post_reconnect_window_until_us -
                                     context.stream.stream_start_us) / 1000ULL)
               : 0ULL);
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
      bool user_stop_requested =
          context.stream.stop_requested || context.stream.stop_requested_by_user;
      const char *reason_label = quit_reason_label(event->quit.reason);
			LOGE("EventCB CHIAKI_EVENT_QUIT (%s | code=%d \"%s\")",
				 event->quit.reason_str ? event->quit.reason_str : "unknown",
				 event->quit.reason,
				 reason_label);
      LOGD("Quit classification: user_stop=%d, fast_restart=%d, retry_pending=%d, retry_active=%d, teardown_in_progress=%d",
           user_stop_requested ? 1 : 0,
           context.stream.fast_restart_active ? 1 : 0,
           context.stream.loss_retry_pending ? 1 : 0,
           context.stream.loss_retry_active ? 1 : 0,
           context.stream.teardown_in_progress ? 1 : 0);
      LOGD("PIPE/SESSION quit gen=%u reconnect_gen=%u fps_low_windows=%u post_reconnect_low=%u",
           context.stream.session_generation,
           context.stream.reconnect_generation,
           context.stream.fps_under_target_windows,
           context.stream.post_reconnect_low_fps_windows);
      ui_connection_cancel();
      bool restart_failed = context.stream.fast_restart_active;
      bool retry_pending = context.stream.loss_retry_pending;
      bool fallback_active = context.stream.loss_retry_active || retry_pending;
      bool restart_context = context.stream.fast_restart_active || fallback_active;
      uint64_t retry_ready = context.stream.loss_retry_ready_us;
      uint32_t retry_attempts = context.stream.loss_retry_attempts;
      uint32_t retry_bitrate = context.stream.loss_retry_bitrate_kbps;
      uint32_t retry_holdoff_ms = context.stream.retry_holdoff_ms;
      uint64_t retry_holdoff_until = context.stream.retry_holdoff_until_us;
      bool retry_holdoff_active = context.stream.retry_holdoff_active;
      if (retry_pending && !context.active_host)
        retry_pending = false;
      shutdown_media_pipeline();
      context.stream.inputs_resume_pending = fallback_active;
      ui_clear_waking_wait();

      // Only finalize if not retrying/restarting
      bool should_finalize = !fallback_active && !context.stream.fast_restart_active;
      if (should_finalize) {
        finalize_session_resources();
      } else {
        // Manually clear flag when skipping finalization - MUST use mutex
        chiaki_mutex_lock(&context.stream.finalization_mutex);
        context.stream.session_init = false;
        chiaki_mutex_unlock(&context.stream.finalization_mutex);
      }
      uint64_t now_us = sceKernelGetProcessTimeWide();
      uint32_t restart_handshake_failures = context.stream.restart_handshake_failures;
      uint64_t last_restart_handshake_fail_us = context.stream.last_restart_handshake_fail_us;
      uint64_t restart_cooloff_until_us = context.stream.restart_cooloff_until_us;
      char restart_source_snapshot[32];
      sceClibSnprintf(restart_source_snapshot,
                      sizeof(restart_source_snapshot),
                      "%s",
                      context.stream.last_restart_source);
      uint32_t restart_source_attempts = context.stream.restart_source_attempts;
      bool remote_in_use =
          event->quit.reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE;
      bool remote_crash =
          event->quit.reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH;
      bool restart_handshake_failure =
          !user_stop_requested &&
          restart_failed &&
          event->quit.reason == CHIAKI_QUIT_REASON_STOPPED;
      if (restart_handshake_failure) {
        bool within_window = last_restart_handshake_fail_us &&
            now_us - last_restart_handshake_fail_us <=
                RESTART_HANDSHAKE_REPEAT_WINDOW_US;
        if (within_window) {
          if (restart_handshake_failures < UINT32_MAX)
            restart_handshake_failures++;
        } else {
          restart_handshake_failures = 1;
          restart_source_attempts = 1;
        }
        last_restart_handshake_fail_us = now_us;
        uint64_t cooloff_us =
            restart_handshake_failures > 1
                ? RESTART_HANDSHAKE_COOLOFF_REPEAT_US
                : RESTART_HANDSHAKE_COOLOFF_FIRST_US;
        restart_cooloff_until_us = now_us + cooloff_us;
        LOGD("PIPE/RESTART_FAIL source=%s classified=handshake_init_ack failures=%u cooloff_ms=%llu",
             restart_source_label(restart_source_snapshot),
             restart_handshake_failures,
             (unsigned long long)(cooloff_us / 1000ULL));
      }
      if (context.active_host && (remote_in_use || remote_crash)) {
        const char *hint =
            remote_in_use ? "Remote Play already active on console"
                          : "Console Remote Play crashed - wait a moment";
        host_set_hint(context.active_host, hint, true, HINT_DURATION_ERROR_US);
      }
      uint64_t retry_delay = STREAM_RETRY_COOLDOWN_US;
      if (!context.stream.stop_requested && (remote_in_use || remote_crash)) {
        retry_delay = RETRY_FAIL_DELAY_US;
      }
      bool arm_retry_holdoff = !context.stream.stop_requested &&
          remote_in_use &&
          (restart_context || context.stream.restart_failure_active);
      if (arm_retry_holdoff) {
        context.stream.retry_holdoff_ms = RETRY_HOLDOFF_RP_IN_USE_MS;
        context.stream.retry_holdoff_until_us =
            now_us + (uint64_t)RETRY_HOLDOFF_RP_IN_USE_MS * 1000ULL;
        context.stream.retry_holdoff_active = true;
        retry_holdoff_ms = context.stream.retry_holdoff_ms;
        retry_holdoff_until = context.stream.retry_holdoff_until_us;
        retry_holdoff_active = context.stream.retry_holdoff_active;
        LOGD("Retry holdoff armed reason=rp_in_use_after_soft_restart duration=%u ms",
             context.stream.retry_holdoff_ms);
      }
      uint64_t throttle_until = now_us + retry_delay;
      if (context.stream.retry_holdoff_active &&
          context.stream.retry_holdoff_until_us > throttle_until) {
        throttle_until = context.stream.retry_holdoff_until_us;
      }
      if (context.stream.stop_requested) {
        context.stream.next_stream_allowed_us = 0;
      } else {
        context.stream.next_stream_allowed_us = throttle_until;
      }
      if (context.stream.next_stream_allowed_us > now_us) {
        uint64_t wait_ms =
            (context.stream.next_stream_allowed_us - now_us + 999) / 1000ULL;
        LOGD("Stream cooldown engaged for %llu ms", wait_ms);
      }
      if (!user_stop_requested) {
        bool is_error = chiaki_quit_reason_is_error(event->quit.reason);

        const char *banner_reason;
        if (!is_error) {
          // Graceful shutdown - friendly message
          if (event->quit.reason == CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN) {
            banner_reason = "Console entered sleep mode";
          } else {
            banner_reason = "Console disconnected";
          }
        } else {
          // Actual error - show detailed reason
          banner_reason = (event->quit.reason_str && event->quit.reason_str[0])
              ? event->quit.reason_str
              : reason_label;
        }

        update_disconnect_banner(banner_reason);
      }
      context.stream.stop_requested = false;
      bool should_resume_discovery = !retry_pending;
      reset_stream_metrics(true);
      if (last_restart_handshake_fail_us &&
          now_us - last_restart_handshake_fail_us >
              RESTART_HANDSHAKE_REPEAT_WINDOW_US) {
        restart_handshake_failures = 0;
        last_restart_handshake_fail_us = 0;
        restart_cooloff_until_us = 0;
        restart_source_snapshot[0] = '\0';
        restart_source_attempts = 0;
      }
      context.stream.restart_handshake_failures = restart_handshake_failures;
      context.stream.last_restart_handshake_fail_us =
          last_restart_handshake_fail_us;
      context.stream.restart_cooloff_until_us =
          restart_cooloff_until_us > now_us ? restart_cooloff_until_us : 0;
      sceClibSnprintf(context.stream.last_restart_source,
                      sizeof(context.stream.last_restart_source),
                      "%s",
                      restart_source_snapshot);
      context.stream.restart_source_attempts = restart_source_attempts;
      context.stream.loss_retry_attempts = retry_attempts;
      context.stream.loss_retry_bitrate_kbps = retry_bitrate;
      context.stream.loss_retry_ready_us = retry_ready;
      context.stream.retry_holdoff_ms = retry_holdoff_ms;
      context.stream.retry_holdoff_until_us = retry_holdoff_until;
      context.stream.retry_holdoff_active =
          retry_holdoff_active && retry_holdoff_until > now_us;
      if (!context.stream.retry_holdoff_active) {
        context.stream.retry_holdoff_ms = 0;
        context.stream.retry_holdoff_until_us = 0;
      }
      context.stream.loss_retry_pending = false;
      context.stream.loss_retry_active = false;
      context.stream.reconnect_overlay_active = false;

      bool retry_allowed_reason = quit_reason_requires_retry(event->quit.reason);
      bool schedule_retry = restart_failed && context.active_host &&
          retry_allowed_reason &&
          retry_bitrate > 0 && retry_attempts < LOSS_RETRY_MAX_ATTEMPTS;

      if (schedule_retry) {
        context.stream.loss_retry_attempts = retry_attempts + 1;
        context.stream.loss_retry_pending = true;
        uint64_t retry_delay_target = now_us + LOSS_RETRY_DELAY_US;
        uint64_t cooldown_target = context.stream.next_stream_allowed_us;
        uint64_t effective_retry_us = retry_delay_target;
        if (cooldown_target > effective_retry_us)
          effective_retry_us = cooldown_target;
        context.stream.loss_retry_ready_us = effective_retry_us;
        should_resume_discovery = false;
        LOGD("Soft restart failed — scheduling hard fallback retry #%u in %llu ms (cooldown=%llu ms, base_delay=%llu ms)",
             retry_attempts + 1,
             (effective_retry_us - now_us) / 1000ULL,
             cooldown_target > now_us ? (cooldown_target - now_us) / 1000ULL : 0ULL,
             LOSS_RETRY_DELAY_US / 1000ULL);
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
          // Finalize session since retry failed
          finalize_session_resources();
          resume_discovery_if_needed();
        } else {
          context.stream.loss_retry_active = false;
          context.stream.reconnect_overlay_active = false;
          resume_discovery_if_needed();
        }
      } else if (restart_failed && !retry_allowed_reason) {
        LOGD("Skipping hard fallback retry for quit reason %d (%s)",
             event->quit.reason,
             reason_label);
      }
      context.stream.stop_requested_by_user = false;
      context.stream.teardown_in_progress = false;
			break;
    }
	}
}

static void reset_stream_metrics(bool preserve_recovery_state) {
  context.stream.measured_bitrate_mbps = 0.0f;
  context.stream.measured_rtt_ms = 0;
  context.stream.last_rtt_refresh_us = 0;
  context.stream.metrics_last_update_us = 0;
  context.stream.retry_holdoff_ms = 0;
  context.stream.retry_holdoff_until_us = 0;
  context.stream.retry_holdoff_active = false;
  context.stream.video_first_frame_logged = false;
  context.stream.measured_incoming_fps = 0;
  context.stream.fps_under_target_windows = 0;
  context.stream.post_reconnect_low_fps_windows = 0;
  context.stream.post_reconnect_window_until_us = 0;
  context.stream.reconnect.recover_active = false;
  context.stream.reconnect.recover_stage = 0;
  context.stream.reconnect.recover_last_action_us = 0;
  context.stream.reconnect.recover_idr_attempts = 0;
  context.stream.reconnect.recover_restart_attempts = 0;
  context.stream.reconnect.recover_stable_windows = 0;
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
  context.stream.loss_counter_saturated_mask = 0;
  context.stream.loss_burst_start_us = 0;
  context.stream.loss_recovery_gate_hits = 0;
  context.stream.loss_recovery_window_start_us = 0;
  context.stream.last_loss_recovery_action_us = 0;
  context.stream.stream_start_us = 0;
  context.stream.loss_restart_soft_grace_until_us = 0;
  context.stream.loss_restart_grace_until_us = 0;
  context.stream.loss_alert_until_us = 0;
  context.stream.loss_alert_duration_us = 0;
  context.stream.logged_loss_events = 0;
  context.stream.auto_loss_downgrades = 0;
  context.stream.takion_drop_events = 0;
  context.stream.takion_drop_packets = 0;
  context.stream.logged_drop_events = 0;
  context.stream.takion_drop_last_us = 0;
  context.stream.av_diag.missing_ref_count = 0;
  context.stream.av_diag.corrupt_burst_count = 0;
  context.stream.av_diag.fec_fail_count = 0;
  context.stream.av_diag.sendbuf_overflow_count = 0;
  context.stream.av_diag.logged_missing_ref_count = 0;
  context.stream.av_diag.logged_corrupt_burst_count = 0;
  context.stream.av_diag.logged_fec_fail_count = 0;
  context.stream.av_diag.logged_sendbuf_overflow_count = 0;
  context.stream.av_diag.last_log_us = 0;
  context.stream.av_diag.last_corrupt_start = 0;
  context.stream.av_diag.last_corrupt_end = 0;
  context.stream.av_diag_stale_snapshot_streak = 0;
  context.stream.last_restart_failure_us = 0;
  context.stream.restart_handshake_failures = 0;
  context.stream.last_restart_handshake_fail_us = 0;
  context.stream.restart_cooloff_until_us = 0;
  context.stream.last_restart_source[0] = '\0';
  context.stream.restart_source_attempts = 0;
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
  context.stream.unrecovered_persistent_events = 0;
  context.stream.unrecovered_persistent_window_start_us = 0;
  context.stream.unrecovered_idr_requests = 0;
  context.stream.unrecovered_idr_window_start_us = 0;
  context.stream.restart_failure_active = false;
  context.stream.stop_requested_by_user = false;
  context.stream.teardown_in_progress = false;
  vitavideo_hide_poor_net_indicator();
}

static void shutdown_media_pipeline(void) {
  if (!context.stream.media_initialized)
    return;

  // Stop the video decode thread and clear the frame_ready flag BEFORE freeing
  // the texture.  The UI thread renders decoded frames from
  // vita_video_render_latest_frame(); we must ensure it is no longer drawing
  // the texture when we free it.
  context.stream.is_streaming = false;   // UI loop stops entering render branch
  vita_h264_stop();                      // active_video_thread=false, frame_ready=false
  sceKernelDelayThread(2000);            // 2ms — let any in-flight render + swap finish

  chiaki_opus_decoder_fini(&context.stream.opus_decoder);
  vita_h264_cleanup();
  vita_audio_cleanup();
  context.stream.media_initialized = false;
  context.stream.inputs_ready = false;
  context.stream.fast_restart_active = false;
  context.stream.reconnect_overlay_active = false;
}

/**
 * Finalizes session resources in a thread-safe manner.
 *
 * This function can be called from multiple concurrent threads (quit event handler,
 * retry failure path, init failure cleanup). The mutex ensures only one thread
 * performs the actual finalization, preventing double-free and use-after-free bugs.
 */
static void finalize_session_resources(void) {
  // Acquire mutex for atomic check-and-set operation
  chiaki_mutex_lock(&context.stream.finalization_mutex);

  if (!context.stream.session_init) {
    // Already finalized by another thread
    chiaki_mutex_unlock(&context.stream.finalization_mutex);
    return;
  }

  // Mark as finalized immediately while holding mutex
  // This prevents any other thread from getting past the guard check
  context.stream.session_init = false;

  chiaki_mutex_unlock(&context.stream.finalization_mutex);

  // Perform the actual finalization outside the critical section
  // Only one thread can reach here due to the atomic check-and-set above
  LOGD("Finalizing session resources");

  // Signal input thread to exit
  context.stream.input_thread_should_exit = true;

  // Join input thread
  ChiakiErrorCode err = chiaki_thread_join(&context.stream.input_thread, NULL);
  if (err != CHIAKI_ERR_SUCCESS) {
    LOGE("Failed to join input thread: %d", err);
  } else {
    LOGD("Input thread joined successfully");
  }

  /*
   * NOTE: We deliberately DO NOT call chiaki_session_join() here.
   *
   * This function is invoked from the CHIAKI_EVENT_QUIT callback, which runs
   * inside the session thread itself (lib/src/session.c:767, session_thread_func).
   * Attempting to join the session thread from within that thread would cause
   * sceKernelWaitThreadEnd() to fail with error code 3 (attempting to wait for
   * the current thread).
   *
   * The session thread will exit naturally after the event callback returns.
   * chiaki_session_fini() below handles all necessary cleanup without requiring
   * the session thread to be joined first - it tears down network connections,
   * frees buffers, and destroys synchronization primitives.
   */

  // Finalize session
  chiaki_session_fini(&context.stream.session);
  LOGD("Session finalized");

  /*
   * NOTE: We do NOT destroy the finalization_mutex here.
   * The mutex is initialized once in vita_chiaki_init_context() and should persist
   * across multiple streaming sessions. It will be destroyed when the application
   * exits as part of the overall context cleanup.
   */
}

static void update_latency_metrics(void) {
  static const uint64_t RTT_REFRESH_INTERVAL_US = 1000000ULL;

  if (!context.stream.session_init)
    return;

  ChiakiStreamConnection *stream_connection = &context.stream.session.stream_connection;
  ChiakiVideoReceiver *receiver = stream_connection->video_receiver;
  if (!receiver)
    return;

  uint32_t takion_drop_events = context.stream.takion_drop_events;
  uint32_t takion_drop_packets = context.stream.takion_drop_packets;
  uint64_t takion_drop_last_us = context.stream.takion_drop_last_us;
  uint32_t av_diag_missing_ref_count = context.stream.av_diag.missing_ref_count;
  uint32_t av_diag_corrupt_burst_count = context.stream.av_diag.corrupt_burst_count;
  uint32_t av_diag_fec_fail_count = context.stream.av_diag.fec_fail_count;
  uint32_t av_diag_sendbuf_overflow_count = context.stream.av_diag.sendbuf_overflow_count;
  uint32_t av_diag_trylock_failures = 0;
  uint32_t av_diag_last_corrupt_start = context.stream.av_diag.last_corrupt_start;
  uint32_t av_diag_last_corrupt_end = context.stream.av_diag.last_corrupt_end;
  bool diag_snapshot_stale = true;

  // Snapshot diagnostics under dedicated diagnostics mutex so hot packet
  // paths do not contend with stream state transitions.
  if (chiaki_mutex_trylock(&stream_connection->diag_mutex) == CHIAKI_ERR_SUCCESS) {
    takion_drop_events = stream_connection->drop_events;
    takion_drop_packets = stream_connection->drop_packets;
    takion_drop_last_us =
        stream_connection->drop_last_ms ? (stream_connection->drop_last_ms * 1000ULL) : 0;
    av_diag_missing_ref_count = stream_connection->av_missing_ref_events;
    av_diag_corrupt_burst_count = stream_connection->av_corrupt_burst_events;
    av_diag_fec_fail_count = stream_connection->av_fec_fail_events;
    av_diag_sendbuf_overflow_count = stream_connection->av_sendbuf_overflow_events;
    av_diag_trylock_failures = stream_connection->diag_trylock_failures;
    av_diag_last_corrupt_start = stream_connection->av_last_corrupt_start;
    av_diag_last_corrupt_end = stream_connection->av_last_corrupt_end;
    chiaki_mutex_unlock(&stream_connection->diag_mutex);
    diag_snapshot_stale = false;
  }

  if (diag_snapshot_stale) {
    if (context.stream.av_diag_stale_snapshot_streak < UINT32_MAX)
      context.stream.av_diag_stale_snapshot_streak++;
  } else {
    context.stream.av_diag_stale_snapshot_streak = 0;
  }

  context.stream.takion_drop_events = takion_drop_events;
  context.stream.takion_drop_packets = takion_drop_packets;
  context.stream.takion_drop_last_us = takion_drop_last_us;
  context.stream.av_diag.missing_ref_count = av_diag_missing_ref_count;
  context.stream.av_diag.corrupt_burst_count = av_diag_corrupt_burst_count;
  context.stream.av_diag.fec_fail_count = av_diag_fec_fail_count;
  context.stream.av_diag.sendbuf_overflow_count = av_diag_sendbuf_overflow_count;
  context.stream.av_diag.last_corrupt_start = av_diag_last_corrupt_start;
  context.stream.av_diag.last_corrupt_end = av_diag_last_corrupt_end;

  uint32_t fps = context.stream.session.connect_info.video_profile.max_fps;
  if (fps == 0)
    fps = 30;

  ChiakiStreamStats *stats = &receiver->frame_processor.stream_stats;
  uint64_t bitrate_bps = chiaki_stream_stats_bitrate(stats, fps);
  float bitrate_mbps = bitrate_bps > 0 ? ((float)bitrate_bps / 1000000.0f) : 0.0f;
  uint64_t now_us = sceKernelGetProcessTimeWide();

  context.stream.measured_bitrate_mbps = bitrate_mbps;

  uint32_t effective_target_fps =
      context.stream.target_fps ? context.stream.target_fps :
      context.stream.negotiated_fps;
  uint32_t incoming_fps = context.stream.measured_incoming_fps;
  bool low_fps_window = effective_target_fps > 0 && incoming_fps > 0 &&
      incoming_fps + 2 < effective_target_fps;
  bool av_diag_progressed =
      av_diag_missing_ref_count >
          context.stream.av_diag.logged_missing_ref_count ||
      av_diag_corrupt_burst_count >
          context.stream.av_diag.logged_corrupt_burst_count ||
      av_diag_fec_fail_count >
          context.stream.av_diag.logged_fec_fail_count ||
      av_diag_sendbuf_overflow_count >
          context.stream.av_diag.logged_sendbuf_overflow_count;
  if (diag_snapshot_stale) {
    // Don't escalate based on stale snapshots when diagnostics couldn't be sampled.
    av_diag_progressed = false;
    if (context.stream.av_diag_stale_snapshot_streak >= AV_DIAG_STALE_SNAPSHOT_WARN_STREAK &&
        low_fps_window) {
      // Prolonged diagnostics contention plus low FPS is treated as AV distress
      // so recovery does not stay blind under sustained lock pressure.
      av_diag_progressed = true;
    }
  }

  bool refresh_rtt = context.stream.last_rtt_refresh_us == 0 ||
                     (now_us - context.stream.last_rtt_refresh_us) >= RTT_REFRESH_INTERVAL_US;
  if (refresh_rtt) {
    uint64_t base_rtt_ms64 = context.stream.session.rtt_us / 1000ULL;
    uint64_t jitter_us = stream_connection->takion.jitter_stats.jitter_us;
    uint64_t jitter_ms64 = jitter_us / 1000ULL;
    uint64_t effective_rtt_ms64 = base_rtt_ms64 + jitter_ms64;
    if (effective_rtt_ms64 > UINT32_MAX)
      effective_rtt_ms64 = UINT32_MAX;
    if (effective_rtt_ms64 == 0)
      effective_rtt_ms64 = base_rtt_ms64 > UINT32_MAX ? UINT32_MAX : base_rtt_ms64;

    context.stream.measured_rtt_ms = (uint32_t)effective_rtt_ms64;
    context.stream.last_rtt_refresh_us = now_us;
    context.stream.metrics_last_update_us = now_us;

    // Count low-fps health once per metrics window (about 1 second), not per frame.
    if (low_fps_window) {
      context.stream.fps_under_target_windows++;
      if (context.stream.post_reconnect_window_until_us &&
          now_us <= context.stream.post_reconnect_window_until_us) {
        context.stream.post_reconnect_low_fps_windows++;
      }
    }

    handle_post_reconnect_degraded_mode(av_diag_progressed,
                                        incoming_fps,
                                        effective_target_fps,
                                        low_fps_window,
                                        now_us);
  }

  if (!context.config.show_latency)
    return;

  static const uint64_t LOG_INTERVAL_US = 1000000;
  static uint64_t last_log_us = 0;
  if (now_us - last_log_us >= LOG_INTERVAL_US) {
    float target_mbps = context.stream.session.connect_info.video_profile.bitrate / 1000.0f;
    LOGD("Latency metrics — target %.2f Mbps, measured %.2f Mbps, RTT %u ms (base %u ms, jitter %llu us)",
         target_mbps,
         bitrate_mbps,
         context.stream.measured_rtt_ms,
         (uint32_t)(context.stream.session.rtt_us / 1000),
         (unsigned long long)stream_connection->takion.jitter_stats.jitter_us);
    LOGD("PIPE/FPS gen=%u reconnect_gen=%u incoming=%u target=%u low_windows=%u post_reconnect_low=%u post_window_remaining_ms=%llu",
         context.stream.session_generation,
         context.stream.reconnect_generation,
         incoming_fps,
         effective_target_fps,
         context.stream.fps_under_target_windows,
         context.stream.post_reconnect_low_fps_windows,
         context.stream.post_reconnect_window_until_us &&
                 now_us < context.stream.post_reconnect_window_until_us
             ? (unsigned long long)((context.stream.post_reconnect_window_until_us -
                                     now_us) / 1000ULL)
             : 0ULL);
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

  bool av_diag_changed = av_diag_progressed;
  if (av_diag_changed ||
      (context.stream.av_diag.last_log_us == 0 ||
       now_us - context.stream.av_diag.last_log_us >= AV_DIAG_LOG_INTERVAL_US)) {
    LOGD("AV diag — missing_ref=%u, corrupt_bursts=%u, fec_fail=%u, sendbuf_overflow=%u, diag_trylock_failures=%u, stale_diag_streak=%u, last_corrupt=%u-%u",
         context.stream.av_diag.missing_ref_count,
         context.stream.av_diag.corrupt_burst_count,
         context.stream.av_diag.fec_fail_count,
         context.stream.av_diag.sendbuf_overflow_count,
         av_diag_trylock_failures,
         context.stream.av_diag_stale_snapshot_streak,
         context.stream.av_diag.last_corrupt_start,
         context.stream.av_diag.last_corrupt_end);
    context.stream.av_diag.logged_missing_ref_count =
        context.stream.av_diag.missing_ref_count;
    context.stream.av_diag.logged_corrupt_burst_count =
        context.stream.av_diag.corrupt_burst_count;
    context.stream.av_diag.logged_fec_fail_count =
        context.stream.av_diag.fec_fail_count;
    context.stream.av_diag.logged_sendbuf_overflow_count =
        context.stream.av_diag.sendbuf_overflow_count;
    context.stream.av_diag.last_log_us = now_us;
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
  bool user_stop = false;
  if (reason && (strcmp(reason, "user cancel") == 0 ||
                 strcmp(reason, "L+R+Start") == 0)) {
    user_stop = true;
  }
  if (!context.stream.stop_requested) {
    LOGD("Stopping stream (%s)", reason ? reason : "user");
    context.stream.stop_requested = true;
    context.stream.stop_requested_by_user = user_stop;
  }
  context.stream.teardown_in_progress = true;
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
  return true;
}

static const char *restart_source_label(const char *source) {
  return (source && source[0]) ? source : "unknown";
}

static bool request_stream_restart_coordinated(const char *source,
                                               uint32_t bitrate_kbps,
                                               uint64_t now_us) {
  const char *source_label = restart_source_label(source);
  if (context.stream.stop_requested) {
    LOGD("PIPE/RESTART source=%s action=skip reason=stop_requested",
         source_label);
    return false;
  }
  if (context.stream.fast_restart_active) {
    LOGD("PIPE/RESTART source=%s action=skip reason=restart_active",
         source_label);
    return true;
  }
  if (context.stream.restart_cooloff_until_us &&
      now_us < context.stream.restart_cooloff_until_us) {
    uint64_t remaining_ms =
        (context.stream.restart_cooloff_until_us - now_us) / 1000ULL;
    LOGD("PIPE/RESTART source=%s action=blocked_cooloff remaining=%llums",
         source_label,
         (unsigned long long)remaining_ms);
    return false;
  }

  if (context.stream.last_loss_recovery_action_us &&
      now_us - context.stream.last_loss_recovery_action_us <
          LOSS_RECOVERY_ACTION_COOLDOWN_US) {
    uint64_t remaining_ms =
         (LOSS_RECOVERY_ACTION_COOLDOWN_US -
         (now_us - context.stream.last_loss_recovery_action_us)) / 1000ULL;
    LOGD("PIPE/RESTART source=%s action=cooldown_skip remaining=%llums",
         source_label,
         (unsigned long long)remaining_ms);
    return false;
  }

  if (strcmp(context.stream.last_restart_source, source_label) != 0) {
    sceClibSnprintf(context.stream.last_restart_source,
                    sizeof(context.stream.last_restart_source),
                    "%s",
                    source_label);
    context.stream.restart_source_attempts = 1;
  } else if (context.stream.restart_source_attempts < UINT32_MAX) {
    context.stream.restart_source_attempts++;
  }

  bool ok = request_stream_restart(bitrate_kbps);
  if (ok) {
    context.stream.last_loss_recovery_action_us = now_us;
    LOGD("PIPE/RESTART source=%s action=requested bitrate=%u attempt=%u",
         source_label,
         bitrate_kbps,
         context.stream.restart_source_attempts);
  } else {
    LOGE("PIPE/RESTART source=%s action=failed bitrate=%u attempt=%u",
         source_label,
         bitrate_kbps,
         context.stream.restart_source_attempts);
  }
  return ok;
}

static void mark_restart_failure(uint64_t now_us) {
  context.stream.last_restart_failure_us = now_us;
  context.stream.restart_failure_active = true;
}

static void mark_restart_success(uint64_t now_us) {
  context.stream.last_restart_failure_us = 0;
  context.stream.restart_failure_active = false;
  context.stream.last_loss_recovery_action_us = now_us;
}

static bool request_recovery_restart(const char *source,
                                     uint32_t bitrate_kbps,
                                     uint64_t now_us,
                                     const char *failure_resync_reason) {
  bool ok = request_stream_restart_coordinated(source, bitrate_kbps, now_us);
  if (ok) {
    mark_restart_success(now_us);
    return true;
  }
  mark_restart_failure(now_us);
  if (context.stream.restart_cooloff_until_us &&
      now_us < context.stream.restart_cooloff_until_us) {
    request_decoder_resync("restart cooloff");
    return false;
  }
  if (failure_resync_reason) {
    request_decoder_resync(failure_resync_reason);
  }
  return false;
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

static void reset_reconnect_recovery_state(void) {
  context.stream.reconnect.recover_active = false;
  context.stream.reconnect.recover_stage = RECONNECT_RECOVER_STAGE_IDLE;
  context.stream.reconnect.recover_last_action_us = 0;
  context.stream.reconnect.recover_idr_attempts = 0;
  context.stream.reconnect.recover_restart_attempts = 0;
  context.stream.reconnect.recover_stable_windows = 0;
}

static void start_reconnect_recovery_state(void) {
  context.stream.reconnect.recover_active = true;
  context.stream.reconnect.recover_stage = RECONNECT_RECOVER_STAGE_IDLE;
  context.stream.reconnect.recover_idr_attempts = 0;
  context.stream.reconnect.recover_restart_attempts = 0;
  context.stream.reconnect.recover_stable_windows = 0;
}

static void handle_post_reconnect_degraded_mode(bool av_diag_progressed,
                                                uint32_t incoming_fps,
                                                uint32_t target_fps,
                                                bool low_fps_window,
                                                uint64_t now_us) {
  if (context.stream.stop_requested || context.stream.fast_restart_active)
    return;

  bool reconnect_window_active = context.stream.post_reconnect_window_until_us &&
      now_us <= context.stream.post_reconnect_window_until_us;
  if (!reconnect_window_active)
    return;

  bool degraded =
      context.stream.post_reconnect_low_fps_windows >=
          RECONNECT_RECOVER_LOW_FPS_TRIGGER_WINDOWS &&
      av_diag_progressed;

  bool healthy_window = target_fps > 0 &&
      incoming_fps >= RECONNECT_RECOVER_MIN_HEALTHY_FPS &&
      !av_diag_progressed;
  if (context.stream.reconnect.recover_active) {
    if (healthy_window) {
      context.stream.reconnect.recover_stable_windows++;
      if (context.stream.reconnect.recover_stable_windows >= 2) {
        LOGD("PIPE/RECOVER gen=%u reconnect_gen=%u action=stabilized stage=%u fps=%u/%u",
             context.stream.session_generation,
             context.stream.reconnect_generation,
             context.stream.reconnect.recover_stage,
             incoming_fps,
             target_fps);
        reset_reconnect_recovery_state();
      }
    } else if (low_fps_window || av_diag_progressed) {
      context.stream.reconnect.recover_stable_windows = 0;
    }
  }

  if (!degraded)
    return;

  if (context.stream.reconnect.recover_last_action_us &&
      now_us - context.stream.reconnect.recover_last_action_us <
          RECONNECT_RECOVER_ACTION_COOLDOWN_US) {
    return;
  }

  if (!context.stream.reconnect.recover_active) {
    start_reconnect_recovery_state();
    LOGD("PIPE/RECOVER gen=%u reconnect_gen=%u action=trigger low_windows=%u fps=%u/%u",
         context.stream.session_generation,
         context.stream.reconnect_generation,
         context.stream.post_reconnect_low_fps_windows,
         incoming_fps,
         target_fps);
  }

  if (context.stream.reconnect.recover_stage == RECONNECT_RECOVER_STAGE_IDLE) {
    request_decoder_resync("post-reconnect degraded stage1");
    context.stream.reconnect.recover_idr_attempts++;
    context.stream.reconnect.recover_stage = RECONNECT_RECOVER_STAGE_IDR_REQUESTED;
    context.stream.reconnect.recover_last_action_us = now_us;
    if (context.active_host) {
      host_set_hint(context.active_host,
                    "Video references unstable - requesting keyframe",
                    false,
                    HINT_DURATION_KEYFRAME_US);
    }
    LOGD("PIPE/RECOVER gen=%u reconnect_gen=%u action=stage1_idr idr_attempts=%u fps=%u/%u",
         context.stream.session_generation,
         context.stream.reconnect_generation,
         context.stream.reconnect.recover_idr_attempts,
         incoming_fps,
         target_fps);
    return;
  }

  if (context.stream.reconnect.recover_stage == RECONNECT_RECOVER_STAGE_IDR_REQUESTED) {
    bool stage2_av_distress = av_diag_progressed;
    bool restart_cooloff_active = context.stream.restart_cooloff_until_us &&
        now_us < context.stream.restart_cooloff_until_us;
    bool stage2_source_backoff = strcmp(context.stream.last_restart_source,
                                        "post_reconnect_stage2") == 0 &&
        context.stream.restart_source_attempts > 1 &&
        context.stream.last_restart_handshake_fail_us &&
        now_us - context.stream.last_restart_handshake_fail_us <=
            RESTART_HANDSHAKE_REPEAT_WINDOW_US;
    if (!stage2_av_distress || restart_cooloff_active || stage2_source_backoff) {
      const char *reason = !stage2_av_distress ? "no_av_distress"
          : restart_cooloff_active ? "restart_cooloff"
          : "source_backoff";
      request_decoder_resync("post-reconnect stage2 suppressed");
      context.stream.reconnect.recover_last_action_us = now_us;
      LOGD("PIPE/RECOVER gen=%u reconnect_gen=%u action=stage2_suppressed reason=%s attempts=%u",
           context.stream.session_generation,
           context.stream.reconnect_generation,
           reason,
           context.stream.restart_source_attempts);
      return;
    }

    bool restart_ok = request_stream_restart_coordinated(
        "post_reconnect_stage2",
        RECONNECT_RECOVER_TARGET_KBPS,
        now_us);
    if (restart_ok) {
      context.stream.reconnect.recover_last_action_us = now_us;
      context.stream.reconnect.recover_stage = RECONNECT_RECOVER_STAGE_SOFT_RESTARTED;
      if (context.active_host) {
        host_set_hint(context.active_host,
                      "Rebuilding stream at safer bitrate",
                      true,
                      HINT_DURATION_RECOVERY_US);
      }
      LOGD("PIPE/RECOVER gen=%u reconnect_gen=%u action=stage2_soft_restart bitrate=%u fps=%u/%u",
           context.stream.session_generation,
           context.stream.reconnect_generation,
           RECONNECT_RECOVER_TARGET_KBPS,
           incoming_fps,
           target_fps);
    } else {
      LOGE("PIPE/RECOVER gen=%u reconnect_gen=%u action=stage2_soft_restart_failed",
           context.stream.session_generation,
           context.stream.reconnect_generation);
      reset_reconnect_recovery_state();
    }
    return;
  }

  if (context.stream.reconnect.recover_stage == RECONNECT_RECOVER_STAGE_SOFT_RESTARTED) {
    if (now_us - context.stream.reconnect.recover_last_action_us <
        RECONNECT_RECOVER_STAGE2_WAIT_US) {
      return;
    }
    if (context.stream.reconnect.recover_restart_attempts >= 1)
      return;

    bool restart_ok = request_stream_restart_coordinated(
        "post_reconnect_stage3",
        LOSS_RETRY_BITRATE_KBPS,
        now_us);
    if (restart_ok) {
      context.stream.reconnect.recover_last_action_us = now_us;
      context.stream.reconnect.recover_restart_attempts++;
      context.stream.reconnect.recover_stage = RECONNECT_RECOVER_STAGE_ESCALATED;
      if (context.active_host) {
        host_set_hint(context.active_host,
                      "Persistent video desync - rebuilding session",
                      true,
                      HINT_DURATION_RECOVERY_US);
      }
      LOGD("PIPE/RECOVER gen=%u reconnect_gen=%u action=stage3_guarded_restart bitrate=%u fps=%u/%u",
           context.stream.session_generation,
           context.stream.reconnect_generation,
           LOSS_RETRY_BITRATE_KBPS,
           incoming_fps,
           target_fps);
    } else {
      LOGE("PIPE/RECOVER gen=%u reconnect_gen=%u action=stage3_guarded_restart_failed",
           context.stream.session_generation,
           context.stream.reconnect_generation);
      reset_reconnect_recovery_state();
    }
    return;
  }

  // Defensive guard: valid stages are 0..3. Reset if memory corruption or
  // future wiring mistakes push this state out of range.
  if (context.stream.reconnect.recover_stage > RECONNECT_RECOVER_STAGE_ESCALATED) {
    LOGE("PIPE/RECOVER gen=%u reconnect_gen=%u action=invalid_stage_reset stage=%u",
         context.stream.session_generation,
         context.stream.reconnect_generation,
         context.stream.reconnect.recover_stage);
    reset_reconnect_recovery_state();
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
  if (context.stream.unrecovered_persistent_window_start_us == 0 ||
      now_us - context.stream.unrecovered_persistent_window_start_us >
          UNRECOVERED_PERSIST_WINDOW_US) {
    context.stream.unrecovered_persistent_window_start_us = now_us;
    context.stream.unrecovered_persistent_events = 0;
  }
  context.stream.unrecovered_persistent_events++;

  if (context.stream.unrecovered_idr_window_start_us == 0 ||
      now_us - context.stream.unrecovered_idr_window_start_us >
          UNRECOVERED_IDR_WINDOW_US) {
    context.stream.unrecovered_idr_window_start_us = now_us;
    context.stream.unrecovered_idr_requests = 0;
  }

  bool idr_ineffective =
      context.stream.unrecovered_idr_requests >=
      UNRECOVERED_IDR_INEFFECTIVE_THRESHOLD;
  bool persistent_distress =
      context.stream.unrecovered_persistent_events >=
      UNRECOVERED_PERSIST_THRESHOLD;
  bool startup_hard_grace_active =
      context.stream.loss_restart_grace_until_us &&
      now_us < context.stream.loss_restart_grace_until_us;
  bool startup_soft_grace_active =
      context.stream.loss_restart_soft_grace_until_us &&
      now_us < context.stream.loss_restart_soft_grace_until_us;
  const char *unrecovered_distress_reason = NULL;
  bool av_distress =
      unrecovered_loss_has_av_distress(&unrecovered_distress_reason);

  if (persistent_distress && idr_ineffective) {
    if (startup_hard_grace_active) {
      uint64_t remaining_ms =
          remaining_ms_until(context.stream.loss_restart_grace_until_us, now_us);
      LOGD("Unrecovered loss action=restart_suppressed_startup_hard_grace remaining=%llums",
           (unsigned long long)remaining_ms);
      context.stream.unrecovered_idr_requests++;
      request_decoder_resync("unrecovered persistent startup grace");
      return triggered;
    }
    if (!av_distress) {
      LOGD("Unrecovered loss action=restart_suppressed_no_av_distress events=%u idr=%u",
           context.stream.unrecovered_persistent_events,
           context.stream.unrecovered_idr_requests);
      context.stream.unrecovered_idr_requests++;
      request_decoder_resync("unrecovered persistent no av distress");
      return triggered;
    }
    if (context.stream.last_loss_recovery_action_us &&
        now_us - context.stream.last_loss_recovery_action_us <
            LOSS_RECOVERY_ACTION_COOLDOWN_US) {
      uint64_t remaining_ms =
          (LOSS_RECOVERY_ACTION_COOLDOWN_US -
           (now_us - context.stream.last_loss_recovery_action_us)) / 1000ULL;
      LOGD("Unrecovered loss escalation cooled down (%llums remaining)",
           (unsigned long long)remaining_ms);
    } else {
      LOGD("Unrecovered loss persistent (%u events/%llums, %u IDRs/%llums) — escalating to soft restart",
           context.stream.unrecovered_persistent_events,
           (unsigned long long)(UNRECOVERED_PERSIST_WINDOW_US / 1000ULL),
           context.stream.unrecovered_idr_requests,
           (unsigned long long)(UNRECOVERED_IDR_WINDOW_US / 1000ULL));
      if (!request_recovery_restart("unrecovered_persistent",
                                    LOSS_RETRY_BITRATE_KBPS,
                                    now_us,
                                    "unrecovered persistent restart failed")) {
        LOGE("Soft restart request failed after persistent unrecovered frames; keeping stream alive (reason=%s)",
             unrecovered_distress_reason ? unrecovered_distress_reason : "unknown");
      } else if (context.active_host) {
        host_set_hint(context.active_host,
                      "Video desync — rebuilding stream",
                      true,
                      HINT_DURATION_RECOVERY_US);
        triggered = true;
        context.stream.unrecovered_persistent_events = 0;
        context.stream.unrecovered_persistent_window_start_us = now_us;
        context.stream.unrecovered_idr_requests = 0;
        context.stream.unrecovered_idr_window_start_us = now_us;
      }
      return triggered;
    }
  }

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
    context.stream.unrecovered_idr_requests++;
    request_decoder_resync("unrecovered frame gate");
    return triggered;
  }

  context.stream.unrecovered_gate_events = 0;
  context.stream.unrecovered_gate_window_start_us = now_us;
  context.stream.unrecovered_idr_requests++;
  if (startup_soft_grace_active) {
    uint64_t remaining_ms =
        remaining_ms_until(context.stream.loss_restart_soft_grace_until_us, now_us);
    LOGD("Unrecovered streak action=restart_suppressed_startup_soft_grace remaining=%llums",
         (unsigned long long)remaining_ms);
    request_decoder_resync("unrecovered streak startup grace");
    return triggered;
  }
  if (!av_distress) {
    LOGD("Unrecovered streak action=restart_suppressed_no_av_distress");
    request_decoder_resync("unrecovered streak no av distress");
    return triggered;
  }
  request_decoder_resync("unrecovered frame streak");
  LOGD("Unrecovered frame streak detected — requesting soft restart (reason=%s)",
       unrecovered_distress_reason ? unrecovered_distress_reason : "unknown");
  if (!request_recovery_restart("unrecovered_streak",
                                LOSS_RETRY_BITRATE_KBPS,
                                now_us,
                                "unrecovered streak restart failed")) {
    LOGE("Soft restart request failed after unrecovered frames; keeping stream alive");
  } else if (context.active_host) {
    host_set_hint(context.active_host,
                  "Video desync — retrying stream",
                  true,
                  HINT_DURATION_RECOVERY_US);
    triggered = true;
    context.stream.unrecovered_persistent_events = 0;
    context.stream.unrecovered_persistent_window_start_us = now_us;
    context.stream.unrecovered_idr_requests = 0;
    context.stream.unrecovered_idr_window_start_us = now_us;
  }
  return triggered;
}

static bool unrecovered_loss_has_av_distress(const char **reason_out) {
  if (context.stream.av_diag.missing_ref_count >= 2) {
    if (reason_out)
      *reason_out = "missing_ref";
    return true;
  }
  if (context.stream.av_diag.corrupt_burst_count >= 4) {
    if (reason_out)
      *reason_out = "corrupt_burst";
    return true;
  }
  if (context.stream.av_diag.fec_fail_count > 0) {
    if (reason_out)
      *reason_out = "fec_fail";
    return true;
  }
  if (context.stream.av_diag.sendbuf_overflow_count > 0) {
    if (reason_out)
      *reason_out = "sendbuf_overflow";
    return true;
  }

  uint32_t target_fps = context.stream.target_fps ?
      context.stream.target_fps : context.stream.negotiated_fps;
  uint32_t incoming_fps = context.stream.measured_incoming_fps;
  // Persistent-loss path allows a slightly lower floor before escalation.
  // Here we require stronger evidence (70% FPS + stricter counters) to avoid
  // restart oscillation from short-lived loss bursts.
  if (target_fps && incoming_fps &&
      (uint64_t)incoming_fps * 100ULL < (uint64_t)target_fps * 70ULL) {
    if (reason_out)
      *reason_out = "fps_drop";
    return true;
  }

  if (reason_out)
    *reason_out = "av_healthy";
  return false;
}

static void handle_takion_overflow(void) {
  LOGD("Takion overflow reported (drop_events=%u, total_packets=%u) — no action taken",
       context.stream.takion_drop_events,
       context.stream.takion_drop_packets);
}

static uint32_t clamp_u32(uint32_t value, uint32_t min_value, uint32_t max_value) {
  if (value < min_value)
    return min_value;
  if (value > max_value)
    return max_value;
  return value;
}

static uint32_t saturating_add_u32(uint32_t lhs, uint32_t rhs) {
  if (lhs > UINT32_MAX - rhs)
    return UINT32_MAX;
  return lhs + rhs;
}

static uint32_t saturating_add_u32_report(uint32_t lhs,
                                          uint32_t rhs,
                                          const char *counter_name,
                                          uint32_t counter_mask_bit) {
  uint32_t sum = saturating_add_u32(lhs, rhs);
  if (sum == UINT32_MAX && lhs != UINT32_MAX &&
      !(context.stream.loss_counter_saturated_mask & counter_mask_bit)) {
    LOGE("Loss accumulator '%s' saturated at UINT32_MAX; forcing recovery reset path",
         counter_name ? counter_name : "unknown");
    context.stream.loss_counter_saturated_mask |= counter_mask_bit;
  }
  return sum;
}

static uint64_t remaining_ms_until(uint64_t deadline_us, uint64_t now_us) {
  if (deadline_us == 0 || now_us >= deadline_us)
    return 0;
  return (deadline_us - now_us) / 1000ULL;
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

static bool quit_reason_requires_retry(ChiakiQuitReason reason) {
  switch (reason) {
    case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE:
    case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH:
      return false;
    default:
      return true;
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
    until = now_us + DISCONNECT_BANNER_DEFAULT_US;
  context.stream.disconnect_banner_until_us = until;
}

static LossDetectionProfile loss_profile_for_mode(VitaChiakiLatencyMode mode) {
  LossDetectionProfile profile = {
      .window_us = LOSS_EVENT_WINDOW_DEFAULT_US,
      .min_frames = LOSS_EVENT_MIN_FRAMES_DEFAULT,
      .event_threshold = LOSS_EVENT_THRESHOLD_DEFAULT,
      .frame_threshold = 10,
      .burst_window_us = LOSS_PROFILE_BURST_BASE_US,
      .burst_frame_threshold = 4};

  switch (mode) {
    case VITA_LATENCY_MODE_ULTRA_LOW:
      profile.window_us = LOSS_PROFILE_WINDOW_LOW_US;
      profile.min_frames = 4;
      profile.event_threshold = 2;
      profile.frame_threshold = 6;
      profile.burst_window_us = LOSS_PROFILE_BURST_LOW_US;
      profile.burst_frame_threshold = 6;
      break;
    case VITA_LATENCY_MODE_LOW:
      profile.window_us = LOSS_PROFILE_WINDOW_BALANCED_US;
      profile.min_frames = 4;
      profile.event_threshold = 3;
      profile.frame_threshold = 8;
      profile.burst_window_us = LOSS_PROFILE_BURST_BALANCED_US;
      profile.burst_frame_threshold = 5;
      break;
    case VITA_LATENCY_MODE_BALANCED:
    default:
      profile.window_us = LOSS_EVENT_WINDOW_DEFAULT_US;
      profile.min_frames = LOSS_EVENT_MIN_FRAMES_DEFAULT;
      profile.event_threshold = LOSS_EVENT_THRESHOLD_DEFAULT;
      profile.frame_threshold = 9;
      profile.burst_window_us = LOSS_PROFILE_BURST_LOW_US;
      profile.burst_frame_threshold = 5;
      break;
    case VITA_LATENCY_MODE_HIGH:
      profile.window_us = LOSS_PROFILE_WINDOW_HIGH_US;
      profile.min_frames = 5;
      profile.event_threshold = 3;
      profile.frame_threshold = 11;
      profile.burst_window_us = LOSS_PROFILE_BURST_HIGH_US;
      profile.burst_frame_threshold = 6;
      break;
    case VITA_LATENCY_MODE_MAX:
      profile.window_us = LOSS_PROFILE_WINDOW_MAX_US;
      profile.min_frames = 6;
      profile.event_threshold = 4;
      profile.frame_threshold = 13;
      profile.burst_window_us = LOSS_PROFILE_BURST_MAX_US;
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
    context.stream.loss_counter_saturated_mask = 0;
  }

  context.stream.loss_window_frame_accum =
      saturating_add_u32_report(context.stream.loss_window_frame_accum,
                                (uint32_t)frames_lost,
                                "loss_window_frame_accum",
                                LOSS_COUNTER_SATURATED_WINDOW_FRAMES);

  if (frames_lost >= (int32_t)loss_profile.min_frames) {
    context.stream.loss_window_event_count++;
  }

  // Short-term burst tracking
  uint64_t burst_window_us = loss_profile.burst_window_us;
  if (context.stream.loss_burst_start_us == 0 ||
      now_us - context.stream.loss_burst_start_us > burst_window_us) {
    context.stream.loss_burst_start_us = now_us;
    context.stream.loss_burst_frame_accum = 0;
    context.stream.loss_counter_saturated_mask = 0;
  }
  context.stream.loss_burst_frame_accum =
      saturating_add_u32_report(context.stream.loss_burst_frame_accum,
                                (uint32_t)frames_lost,
                                "loss_burst_frame_accum",
                                LOSS_COUNTER_SATURATED_BURST_FRAMES);
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
  bool sustained_loss =
      hit_burst_threshold || (hit_event_threshold && hit_frame_threshold);

  if (!sustained_loss) {
    // Ignore sub-threshold hiccups; they're common on Vita Wi-Fi.
    // Keep accumulating so repeated drops can still trip the gate.
    return;
  }

  context.stream.loss_window_event_count = 0;
  context.stream.loss_window_start_us = now_us;
  context.stream.loss_window_frame_accum = 0;
  context.stream.loss_burst_frame_accum = 0;
  context.stream.loss_counter_saturated_mask = 0;
  context.stream.loss_burst_start_us = 0;

  const char *trigger = hit_burst_threshold ? "burst threshold" :
      (hit_frame_threshold ? "frame threshold" : "event threshold");
  if (context.config.show_latency) {
    float window_s = (float)loss_profile.window_us / 1000000.0f;
    LOGD("Loss gate reached (%s, %u events / %u frames in %.1fs)",
         trigger,
         window_events,
         window_frames,
         window_s);
  }

  if (context.stream.stop_requested || context.stream.fast_restart_active)
    return;

  if (context.stream.loss_recovery_window_start_us == 0 ||
      now_us - context.stream.loss_recovery_window_start_us > LOSS_RECOVERY_WINDOW_US) {
    context.stream.loss_recovery_window_start_us = now_us;
    context.stream.loss_recovery_gate_hits = 0;
  }

  context.stream.loss_recovery_gate_hits++;
  if (context.config.show_latency) {
    LOGD("Loss recovery gate stage=%u trigger=%s action=inspect",
         context.stream.loss_recovery_gate_hits,
         trigger);
  }
  if (context.stream.loss_recovery_gate_hits == 1) {
    if (context.config.show_latency) {
      LOGD("Loss recovery action=idr_only trigger=%s", trigger);
    }
    request_decoder_resync("packet-loss gate");
    if (context.active_host) {
      host_set_hint(context.active_host,
                    "Packet loss burst — requesting keyframe",
                    false,
                    HINT_DURATION_KEYFRAME_US);
    }
    return;
  }

  bool startup_soft_grace_active =
      context.stream.loss_restart_soft_grace_until_us &&
      now_us < context.stream.loss_restart_soft_grace_until_us;
  if (startup_soft_grace_active) {
    uint64_t remaining_ms =
        remaining_ms_until(context.stream.loss_restart_soft_grace_until_us, now_us);
    LOGD("Loss recovery action=restart_suppressed_startup_soft_grace trigger=%s remaining=%llums",
         trigger,
         (unsigned long long)remaining_ms);
    // Keep stage pinned at 1 while startup grace is active so the next
    // non-grace hit resumes from "post-IDR" behavior instead of resetting.
    context.stream.loss_recovery_gate_hits = 1;
    request_decoder_resync("packet-loss startup grace");
    return;
  }

  if (context.stream.last_loss_recovery_action_us &&
      now_us - context.stream.last_loss_recovery_action_us <
          LOSS_RECOVERY_ACTION_COOLDOWN_US) {
    if (context.config.show_latency) {
      uint64_t remaining_ms =
          (LOSS_RECOVERY_ACTION_COOLDOWN_US -
           (now_us - context.stream.last_loss_recovery_action_us)) /
          1000ULL;
      LOGD("Loss recovery action=cooldown_skip trigger=%s remaining=%llums",
           trigger,
           (unsigned long long)remaining_ms);
    }
    request_decoder_resync("packet-loss cooldown");
    return;
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
      bool restart_ok = request_recovery_restart(
          "loss_recovery_gate",
          LOSS_RETRY_BITRATE_KBPS,
          now_us,
          NULL);
      if (restart_ok) {
        uint32_t recovery_stage = context.stream.loss_recovery_gate_hits;
        context.stream.loss_retry_attempts++;
        context.stream.loss_retry_bitrate_kbps = LOSS_RETRY_BITRATE_KBPS;
        context.stream.loss_retry_active = true;
        context.stream.loss_recovery_gate_hits = 0;
        if (context.config.show_latency) {
          LOGD("Loss recovery action=restart trigger=%s stage=%u av_diag={missing_ref=%u,corrupt=%u,fec_fail=%u,sendbuf_overflow=%u,last=%u-%u}",
               trigger,
               recovery_stage,
               context.stream.av_diag.missing_ref_count,
               context.stream.av_diag.corrupt_burst_count,
               context.stream.av_diag.fec_fail_count,
               context.stream.av_diag.sendbuf_overflow_count,
               context.stream.av_diag.last_corrupt_start,
               context.stream.av_diag.last_corrupt_end);
        }
        LOGD("Packet loss fallback scheduled (attempt %u, target %u kbps)",
             context.stream.loss_retry_attempts,
             context.stream.loss_retry_bitrate_kbps);
        if (context.active_host) {
          host_set_hint(context.active_host, hint, true, HINT_DURATION_ERROR_US);
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
    host_set_hint(context.active_host, hint, true, HINT_DURATION_ERROR_US);
  }
  context.stream.last_loss_recovery_action_us = now_us;
  context.stream.loss_recovery_gate_hits = 0;
  context.stream.inputs_resume_pending = true;
  request_stream_stop("packet loss");
}

static bool video_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user) {
  if (context.stream.stop_requested)
    return false;
  if (!context.stream.video_first_frame_logged) {
    LOGD("VIDEO CALLBACK: First frame received (size=%zu)", buf_size);
    context.stream.video_first_frame_logged = true;
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

/**
 * Convert rear touchpad coordinates to a grid-based input.
 *
 * Maps rear touch (x,y) coordinates to a specific grid cell index based on the
 * configured rear touch grid dimensions (VITAKI_REAR_TOUCH_GRID_COLS x VITAKI_REAR_TOUCH_GRID_ROWS).
 * This allows flexible, user-configurable rear panel input mapping.
 *
 * @param x Horizontal touch coordinate
 * @param y Vertical touch coordinate
 * @param max_w Maximum width of the touch area
 * @param max_h Maximum height of the touch area
 * @return Grid-based VitakiCtrlIn enum for the touched cell, or VITAKI_CTRL_IN_NONE if invalid
 */
static VitakiCtrlIn rear_grid_input_from_touch(int x, int y, int max_w, int max_h) {
  if (x < 0 || y < 0)
    return VITAKI_CTRL_IN_NONE;
  if (x >= max_w)
    x = max_w - 1;
  if (y >= max_h)
    y = max_h - 1;
  int col = (x * VITAKI_REAR_TOUCH_GRID_COLS) / max_w;
  int row = (y * VITAKI_REAR_TOUCH_GRID_ROWS) / max_h;
  if (col < 0)
    col = 0;
  if (col >= VITAKI_REAR_TOUCH_GRID_COLS)
    col = VITAKI_REAR_TOUCH_GRID_COLS - 1;
  if (row < 0)
    row = 0;
  if (row >= VITAKI_REAR_TOUCH_GRID_ROWS)
    row = VITAKI_REAR_TOUCH_GRID_ROWS - 1;
  return (VitakiCtrlIn)(VITAKI_CTRL_IN_REARTOUCH_GRID_START + row * VITAKI_REAR_TOUCH_GRID_COLS + col);
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
  while (!stream->input_thread_should_exit) {

    // Trigger inputs can be remapped via custom controller mappings (L1/R1/L2/R2/L3/R3/PS/etc.).
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

      // Process rear touchpad input with grid-based mapping
      for (int touch_i = 0; touch_i < touch[SCE_TOUCH_PORT_BACK].reportNum; touch_i++) {
        int x = touch[SCE_TOUCH_PORT_BACK].report[touch_i].x;
        int y = touch[SCE_TOUCH_PORT_BACK].report[touch_i].y;

        stream->controller_state.buttons |= vcmi.in_out_btn[VITAKI_CTRL_IN_REARTOUCH_ANY];

        // Track left/right zones for L1+rear / R1+rear combo mappings
        // (used at lines 1540-1551 for REARTOUCH_LEFT_L1 and REARTOUCH_RIGHT_R1)
        // Note: Center line (x == TOUCH_MAX_WIDTH_BY_2) intentionally excluded from both zones
        if (x > TOUCH_MAX_WIDTH_BY_2) {
          reartouch_right = true;
        } else if (x < TOUCH_MAX_WIDTH_BY_2) {
          reartouch_left = true;
        }

        // Map touch coordinates to user-configured grid cell
        VitakiCtrlIn grid_input = rear_grid_input_from_touch(x, y, TOUCH_MAX_WIDTH, TOUCH_MAX_HEIGHT);
        if (grid_input != VITAKI_CTRL_IN_NONE && grid_input < VITAKI_CTRL_IN_COUNT) {
          VitakiCtrlOut mapped = vcmi.in_out_btn[grid_input];
          // Note: L2/R2 treated as digital (full press = 0xff) not analog
          // Multi-touch will set to 0xff - no pressure accumulation
          if (mapped == VITAKI_CTRL_OUT_L2) {
            stream->controller_state.l2_state = 0xff;
          } else if (mapped == VITAKI_CTRL_OUT_R2) {
            stream->controller_state.r2_state = 0xff;
          } else if (mapped != VITAKI_CTRL_OUT_NONE) {
            stream->controller_state.buttons |= mapped;
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
  context.stream.stop_requested_by_user = false;
  context.stream.teardown_in_progress = false;
  context.stream.inputs_ready = false;
  context.stream.is_streaming = false;
  context.stream.media_initialized = false;

  uint64_t now_us = sceKernelGetProcessTimeWide();
  if (context.stream.retry_holdoff_active &&
      now_us >= context.stream.retry_holdoff_until_us) {
    LOGD("Retry holdoff expired (duration=%u ms)", context.stream.retry_holdoff_ms);
    context.stream.retry_holdoff_active = false;
    context.stream.retry_holdoff_ms = 0;
    context.stream.retry_holdoff_until_us = 0;
  }
  if (context.stream.next_stream_allowed_us &&
      now_us < context.stream.next_stream_allowed_us) {
    uint64_t remaining_ms =
        (context.stream.next_stream_allowed_us - now_us + 999) / 1000;
    if (context.stream.retry_holdoff_active &&
        now_us < context.stream.retry_holdoff_until_us) {
      uint64_t holdoff_remaining_ms =
          (context.stream.retry_holdoff_until_us - now_us + 999) / 1000;
      LOGD("Stream start blocked by adaptive holdoff for %llu ms (total cooldown %llu ms)",
           holdoff_remaining_ms, remaining_ms);
    } else {
      LOGD("Stream start blocked for %llu ms to let console recover", remaining_ms);
    }
    goto cleanup;
  }

  ChiakiVideoResolutionPreset requested_resolution = context.config.resolution;
  // Defensive guardrail: config/UI path should already normalize unsupported values,
  // but force a safe profile here to preserve stream startup reliability.
  if (requested_resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_720p ||
      requested_resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_1080p) {
    LOGD("Requested legacy unsupported %s profile; forcing 540p fallback",
         requested_resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_1080p ? "1080p" : "720p");
    requested_resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
  }

  ChiakiConnectVideoProfile profile = {};
	chiaki_connect_video_profile_preset(&profile,
			requested_resolution, context.config.fps);
  apply_latency_mode(&profile, context.config.latency_mode);
  if (context.stream.loss_retry_active && context.stream.loss_retry_bitrate_kbps > 0) {
    profile.bitrate = context.stream.loss_retry_bitrate_kbps;
    LOGD("Applying packet-loss fallback bitrate: %u kbps", profile.bitrate);
    context.stream.loss_retry_active = false;
  }
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
      host_set_hint(host, "Waiting for console network link...", false, HINT_DURATION_LINK_WAIT_US);
    } else {
		  LOGE("Error during stream setup: %s", chiaki_error_string(err));
    }
    goto cleanup;
  }
  if (resume_inputs && err == CHIAKI_ERR_SUCCESS) {
    context.stream.inputs_ready = true;
    context.stream.inputs_resume_pending = false;
  }

  uint32_t new_generation = context.stream.session_generation + 1;
  context.stream.reconnect_generation =
      context.stream.session_generation > 0 ? context.stream.session_generation : 0;
  context.stream.session_generation = new_generation;
  LOGD("PIPE/SESSION start gen=%u reconnect_gen=%u host=%s",
       context.stream.session_generation,
       context.stream.reconnect_generation,
       host->hostname ? host->hostname : "<null>");

  if (discovery_was_running) {
    LOGD("Suspending discovery during stream");
    stop_discovery(true);
    context.discovery_resume_after_stream = true;
  }
	init_controller_map(&(context.stream.vcmi), context.config.controller_map_id);
	// Mark session as initialized - MUST use mutex
	chiaki_mutex_lock(&context.stream.finalization_mutex);
 	context.stream.session_init = true;
	chiaki_mutex_unlock(&context.stream.finalization_mutex);
	reset_stream_metrics(false);
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
		LOGE("Error during video start: %d (0x%08x), profile=%ux%u@%u",
         err, (unsigned int)err, profile.width, profile.height, profile.max_fps);
    goto cleanup;
  }
  vita_h264_start();

  err = chiaki_session_start(&context.stream.session);
  if(err != CHIAKI_ERR_SUCCESS) {
		LOGE("Error during stream start: %s", chiaki_error_string(err));
    goto cleanup;
  }

	context.stream.input_thread_should_exit = false;
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
    // Finalize if session was partially initialized
    if (context.stream.session_init) {
      finalize_session_resources();
    }
    // No else needed - flag is already false or will be cleared by finalize
    context.stream.fast_restart_active = false;
    context.stream.reconnect_overlay_active = false;
    context.stream.loss_retry_active = false;
    context.stream.loss_retry_pending = false;
    context.stream.is_streaming = false;
    context.stream.inputs_ready = false;
    context.stream.teardown_in_progress = false;
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
  persist_config_or_warn();

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
  persist_config_or_warn();

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
