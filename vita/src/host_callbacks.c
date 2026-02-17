#include "context.h"
#include "host_feedback.h"
#include "host_metrics.h"
#include "host_quit.h"
#include "host_callbacks.h"
#include "video.h"

#include <psp2/kernel/processmgr.h>

// Startup can include console wake + decoder warmup. Keep a short grace for
// burst suppression and a longer hard grace for severe unrecovered churn.
#define LOSS_RESTART_STARTUP_SOFT_GRACE_US (2500 * 1000ULL)
#define LOSS_RESTART_STARTUP_HARD_GRACE_US (20 * 1000 * 1000ULL)

void host_event_cb(ChiakiEvent *event, void *user) {
  switch (event->type) {
    case CHIAKI_EVENT_CONNECTED:
      LOGD("EventCB CHIAKI_EVENT_CONNECTED");
      context.stream.stream_start_us = sceKernelGetProcessTimeWide();
      context.stream.loss_restart_soft_grace_until_us =
          context.stream.stream_start_us + LOSS_RESTART_STARTUP_SOFT_GRACE_US;
      context.stream.loss_restart_grace_until_us =
          context.stream.stream_start_us + LOSS_RESTART_STARTUP_HARD_GRACE_US;
      context.stream.post_reconnect_window_until_us = 0;
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
    case CHIAKI_EVENT_QUIT:
      host_handle_quit_event(event);
      break;
  }
}

bool host_video_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user) {
  if (context.stream.stop_requested)
    return false;
  if (!context.stream.video_first_frame_logged) {
    LOGD("VIDEO CALLBACK: First frame received (size=%zu)", buf_size);
    context.stream.video_first_frame_logged = true;
  }
  if (frames_lost > 0) {
    host_handle_loss_event(frames_lost, frame_recovered);
    host_handle_unrecovered_frame_loss(frames_lost, frame_recovered);
  }
  context.stream.is_streaming = true;
  context.stream.reset_reconnect_gen = false;  // Streaming started — consume the reset flag
  if (ui_connection_overlay_active())
    ui_connection_complete();
  if (context.stream.reconnect_overlay_active)
    context.stream.reconnect_overlay_active = false;
  int err = vita_h264_decode_frame(buf, buf_size);
  if (err != 0) {
    LOGE("Error during video decode: %d", err);
    return false;
  }
  host_metrics_update_latency();
  return true;
}
