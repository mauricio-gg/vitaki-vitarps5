#include "context.h"
#include "host_feedback.h"
#include "host_metrics.h"
#include "host_quit.h"
#include "host_callbacks.h"
#include "ui.h"
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
      context.stream.post_stop_guard = false;
      context.stream.retry_holdoff_ms = 0;
      context.stream.retry_holdoff_until_us = 0;
      context.stream.retry_holdoff_active = false;
      // A real successful connect resets the one-shot RP_IN_USE auto-retry
      // budget (Piece C) for the next time RP_IN_USE might occur later in
      // this fresh session.
      context.stream.rp_in_use_retry_pending = false;
      context.stream.rp_in_use_retry_used = false;
      // GH #258: a stale anchor from before this connect (e.g. a suspend that
      // happened while a previous stream was tearing down) must not survive
      // into the fresh session -- fixes the same latent stale-anchor issue
      // as the CHIAKI_EVENT_STREAM_RESTARTING handler below, for the
      // vita-initiated restart path.
      context.stream.suspend_tick_last_us = 0;
      context.stream.restart_handshake_failures = 0;
      context.stream.last_restart_handshake_fail_us = 0;
      context.stream.restart_cooloff_until_us = 0;
      context.stream.last_restart_source[0] = '\0';
      context.stream.restart_source_attempts = 0;
      LOGD("PIPE/SESSION connected gen=%u reconnect_gen=%u post_window_ms=%llu",
           context.stream.session_generation, context.stream.reconnect_generation,
           context.stream.post_reconnect_window_until_us
               ? (unsigned long long)((context.stream.post_reconnect_window_until_us -
                                       context.stream.stream_start_us) /
                                      1000ULL)
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
    case CHIAKI_EVENT_STREAM_RESTARTING:
      // GH #261: the lib self-requests a soft restart when Takion's transport dies mid-
      // stream on its own (console never disconnected us). That restart's bang-wait can
      // take seconds with zero prior visibility -- this event is the only signal we get
      // before it either completes or the ladder exhausts. Runs on the session thread
      // (same as host_handle_quit_event); must not call chiaki_session_* here, only touch
      // vita-side state -- mirrors request_stream_restart()'s state set (host_recovery.c).
      LOGD("EventCB CHIAKI_EVENT_STREAM_RESTARTING");
      if (context.stream.stop_requested)
        break;
      context.stream.fast_restart_active = true;
      context.stream.is_streaming = false;  // lets the reconnect screen draw, ui.c:611-616
      context.stream.reconnect_overlay_active = true;
      context.stream.reconnect_overlay_start_us = sceKernelGetProcessTimeWide();
      context.stream.inputs_ready = true;
      context.stream.inputs_resume_pending = true;
      // Arm the hard-fallback escalation (host_quit.c's existing-but-dead retry path,
      // capped at LOSS_RETRY_MAX_ATTEMPTS): if this soft restart also fails to complete,
      // CHIAKI_EVENT_QUIT sees a nonzero loss_retry_bitrate_kbps and schedules a full
      // session teardown + fresh reconnect instead of leaving the freeze silent forever.
      // Deliberately the SAME bitrate, not a lowered one -- session.c's
      // transport_only_failure comment documents a lowered restart bitrate wedging
      // consoles into repeated "Remote Play crashed" refusals on hardware.
      if (context.stream.session_init) {
        context.stream.loss_retry_bitrate_kbps =
            context.stream.session.connect_info.video_profile.bitrate;
      }
      // Metrics stop ticking the instant is_streaming flips above; a stale anchor left
      // over from before this restart would otherwise fire a spurious #258 detection the
      // moment streaming resumes.
      context.stream.suspend_tick_last_us = 0;
      context.stream.suspend_resync_shots_left = 0;
      break;
  }
}

bool host_video_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered,
                   void *user) {
  if (context.stream.stop_requested)
    return false;
  if (!context.stream.video_first_frame_logged) {
    uint64_t now_us = sceKernelGetProcessTimeWide();
    uint64_t delta_us =
        context.stream.session_start_us ? (now_us - context.stream.session_start_us) : 0;
    LOGD("VIDEO CALLBACK: First frame received (size=%zu)", buf_size);
    LOGD("PIPE/TIME_TO_FIRST_FRAME us=%llu", (unsigned long long)delta_us);
    context.stream.video_first_frame_logged = true;

    if (ui_connection_overlay_active()) {
      ui_connection_complete();
      LOGD("PIPE/OVERLAY_DISMISSED us=%llu", (unsigned long long)delta_us);
    }
  }
  if (frames_lost > 0) {
    host_handle_loss_event(frames_lost, frame_recovered);
    host_handle_unrecovered_frame_loss(frames_lost, frame_recovered);
  }
  context.stream.is_streaming = true;
  context.stream.reset_reconnect_gen = false;  // Streaming started — consume the reset flag
  if (context.stream.reconnect_overlay_active)
    context.stream.reconnect_overlay_active = false;

  /* Pass frame quality with the decode call so the corruption flag and the
   * last-good snapshot are updated atomically under the decode mutex —
   * keeping them consistent with the pixels written to frame_texture.
   * Decode always runs unconditionally to keep the HW decoder DPB reference
   * chain in sync. */
  bool frame_corrupt = (frames_lost > 0) || frame_recovered;

  /* Latency investigation (item 1): read the lib-side first-packet-arrival timestamp
   * directly off the video receiver -- same access pattern host_metrics.c already uses
   * for stream_connection->takion.jitter_stats. Safe to read here: this callback runs
   * synchronously on the recv thread, invoked from inside
   * chiaki_video_receiver_flush_frame() (lib/src/videoreceiver.c) BEFORE that function
   * resets cur_frame_first_packet_ms to 0 for the next frame -- see the call site there.
   * 0 if unavailable (e.g. the profile-header injection call, which runs before
   * cur_frame_first_packet_ms is set for the first frame of a session). */
  uint64_t frame_first_packet_ms = 0;
  if (context.stream.session_init) {
    ChiakiVideoReceiver *receiver = context.stream.session.stream_connection.video_receiver;
    if (receiver)
      frame_first_packet_ms = receiver->cur_frame_first_packet_ms;
  }

  int err = vita_h264_decode_frame(buf, buf_size, frame_corrupt, frame_first_packet_ms);
  if (err != 0) {
    LOGE("Error during video decode: %d", err);
    return false;
  }
  return true;
}
