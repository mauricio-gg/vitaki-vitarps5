#include "context.h"
#include "host_feedback.h"
#include "host_loss_profile.h"
#include "video.h"

#include <psp2/kernel/processmgr.h>
#include <chiaki/streamconnection.h>

#define LOSS_ALERT_DURATION_US (5 * 1000 * 1000ULL)
#define LOSS_RECOVERY_WINDOW_US (8 * 1000 * 1000ULL)
#define UNRECOVERED_FRAME_THRESHOLD 3
#define LOSS_COUNTER_SATURATED_WINDOW_FRAMES (1u << 0)
#define LOSS_COUNTER_SATURATED_BURST_FRAMES  (1u << 1)
#define HINT_DURATION_KEYFRAME_US (4 * 1000 * 1000ULL)

void host_set_hint(VitaChiakiHost *host, const char *msg, bool is_error, uint64_t duration_us) {
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

void host_request_decoder_resync(const char *reason) {
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

bool host_handle_unrecovered_frame_loss(int32_t frames_lost, bool frame_recovered) {
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
  vitavideo_show_poor_net_indicator();
  context.stream.loss_alert_until_us = now_us + LOSS_ALERT_DURATION_US;
  context.stream.loss_alert_duration_us = LOSS_ALERT_DURATION_US;
  host_request_decoder_resync("unrecovered frame");
  return triggered;
}

void host_handle_takion_overflow(void) {
  LOGD("Takion overflow reported (drop_events=%u, total_packets=%u) — no action taken",
       context.stream.takion_drop_events,
       context.stream.takion_drop_packets);
}

void host_handle_loss_event(int32_t frames_lost, bool frame_recovered) {
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
      host_loss_profile_for_mode(context.config.latency_mode);
  host_adjust_loss_profile_with_metrics(&loss_profile);

  if (context.stream.loss_window_start_us == 0 ||
      now_us - context.stream.loss_window_start_us > loss_profile.window_us) {
    context.stream.loss_window_start_us = now_us;
    context.stream.loss_window_event_count = 0;
    context.stream.loss_window_frame_accum = 0;
    context.stream.loss_counter_saturated_mask = 0;
  }

  context.stream.loss_window_frame_accum =
      host_saturating_add_u32_report(context.stream.loss_window_frame_accum,
                                     (uint32_t)frames_lost,
                                     "loss_window_frame_accum",
                                     LOSS_COUNTER_SATURATED_WINDOW_FRAMES);

  if (frames_lost >= (int32_t)loss_profile.min_frames) {
    context.stream.loss_window_event_count++;
  }

  uint64_t burst_window_us = loss_profile.burst_window_us;
  if (context.stream.loss_burst_start_us == 0 ||
      now_us - context.stream.loss_burst_start_us > burst_window_us) {
    context.stream.loss_burst_start_us = now_us;
    context.stream.loss_burst_frame_accum = 0;
    context.stream.loss_counter_saturated_mask = 0;
  }
  context.stream.loss_burst_frame_accum =
      host_saturating_add_u32_report(context.stream.loss_burst_frame_accum,
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
    host_request_decoder_resync("packet-loss gate");
    if (context.active_host) {
      host_set_hint(context.active_host,
                    "Packet loss burst — requesting keyframe",
                    false,
                    HINT_DURATION_KEYFRAME_US);
    }
    return;
  }

  host_request_decoder_resync("packet-loss follow-up");
  context.stream.loss_recovery_gate_hits = 1;
}
