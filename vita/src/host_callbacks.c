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

/* GH #262: staleness tracker (recv-thread, feeds the presentation-side hold gate in
 * video.c). See host_video_cb() below for the full derivation. Both constants here
 * shape the REFERENCE the tracker follows, not the hold thresholds themselves (those --
 * STALE_HOLD_ENGAGE_MS/RELEASE_MS/MAX_MS -- live in video.c next to FREEZE_MAX_STREAK,
 * the mechanism they parallel). */
// Reference decay rate once actual drift comes back down (post-drain). At this rate a
// benign excursion (well under STALE_HOLD_ENGAGE_MS) is fully reabsorbed in ~1s, and a
// worst-case false offset at the 400ms engage line self-heals in ~13s even with no new
// upward excursion to re-anchor it.
#define STALE_REF_CREEP_MS_PER_FRAME 1
// Mirrors the lib's own VIDEO_GAP_SANITY_MAX_MS rebase point (lib/src/videoreceiver.c:65)
// so the vita-side reference and the lib-side drift series can never disagree about
// whether a gap this wide was a stall (rebase) or in-band data -- both sides restart
// their series at the same arrival.
#define STALE_REF_SNAP_GAP_MS 10000

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

/* GH #262 staleness tracker state -- recv-thread-only (same single-thread rationale as
 * cur_frame_first_packet_ms above: this all runs synchronously inside
 * chiaki_video_receiver_flush_frame(), one thread, program order, no concurrent writer).
 * Plain (non-volatile) file-statics are correct here for the same reason plain locals
 * would be, except they must persist frame-to-frame -- there is exactly one reader/writer
 * and it is always this thread. Reset per-stream by host_video_cb_reset_stale_tracker(),
 * called from host_metrics_reset_stream() (runs on every session start/restart, alongside
 * every other per-stream video counter) -- otherwise a reference/EWMA left over from a
 * PRIOR stream's mid-episode state would compute bogus staleness against the new stream's
 * drift series, which itself rebases to 0 at CHIAKI_EVENT_CONNECTED. */
static int64_t stale_ref_scaled = 0;        // reference, same scaled units as drift_last_scaled
static bool stale_ref_valid = false;        // false until the first usable drift sample lands
static uint64_t stale_prev_arrival_ms = 0;  // previous frame's cur_frame_first_packet_ms
static int32_t stale_gap_ewma_ms = 0;       // inter-arrival EWMA (alpha = 1/4)

void host_video_cb_reset_stale_tracker(void) {
  stale_ref_scaled = 0;
  stale_ref_valid = false;
  stale_prev_arrival_ms = 0;
  stale_gap_ewma_ms = 0;
}

/* GH #262: how encoder-stale is the frame about to be decoded? Reads the lib's existing
 * D5-B delivery-drift accounting (receiver->drift_last_scaled / drift_fps,
 * lib/src/videoreceiver.c:573-622) directly -- same-thread, program-ordered, no lock,
 * exactly the justification already documented above on the cur_frame_first_packet_ms
 * read this function shares a call site with.
 *
 * drift_last_scaled already IS a backlog measure (positive = arrival running behind the
 * frame-index-implied schedule; it climbs through a stall and falls back during the
 * drain that follows). But its absolute level can carry a steady-state offset that has
 * nothing to do with a real episode (VBR pacing, encoder quirks) -- reporting it raw
 * would false-hold on that offset forever. So this tracks EXCURSION instead: a reference
 * that follows drift upward INSTANTLY (a real stall must never be underreported) but
 * creeps back down only STALE_REF_CREEP_MS_PER_FRAME per frame once drift itself falls.
 * The result: backlog_ms sits near 0 in steady state (ref tracks scaled 1:1), spikes
 * during the drain that follows a stall (scaled falls fast as the backlog empties; ref
 * lags behind on purpose), and returns to 0 once the drain finishes and ref catches back
 * down -- exactly the window this frame's content is stale relative to now.
 *
 * Drain-signature gate: a real drain (per the #251/#257/#262 forensics: ~2.4x the
 * negotiated rate) has short inter-arrival gaps; a merely high-fps NEGOTIATED stream
 * (e.g. 60fps) also has short gaps but no backlog to speak of. Comparing gap_ewma against
 * 0.7x the negotiated frame period tells the two apart. Cross-multiplied
 * (gap_ewma_ms * drift_fps < 700) instead of dividing, so this is integer-only and never
 * loses the sub-ms precision a truncated frame-period-ms intermediate would. */
static uint32_t host_video_cb_compute_staleness_ms(ChiakiVideoReceiver *receiver,
                                                   uint64_t arrival_ms) {
  if (receiver == NULL || receiver->drift_fps == 0 || arrival_ms == 0) {
    /* Guard div-by-zero / no-timestamp: drift_fps is 0 only before the receiver's
     * DELIVERY_INIT has run (it is clamped non-zero thereafter -- see
     * lib/src/videoreceiver.c's drift_div comment). Report no staleness rather than
     * touch the tracker state with an unusable sample. */
    return 0;
  }

  int64_t scaled = receiver->drift_last_scaled;
  bool rebase = !stale_ref_valid;
  if (!rebase && stale_prev_arrival_ms != 0 && arrival_ms > stale_prev_arrival_ms &&
      (arrival_ms - stale_prev_arrival_ms) > (uint64_t)STALE_REF_SNAP_GAP_MS) {
    /* A gap this wide is a stall boundary, not in-band data -- mirrors the lib's own
     * VIDEO_GAP_SANITY_MAX_MS rebase (videoreceiver.c) so the two series can't disagree. */
    rebase = true;
  }

  if (rebase) {
    stale_ref_scaled = scaled;
    stale_gap_ewma_ms = 0;
    stale_ref_valid = true;
  } else if (scaled > stale_ref_scaled) {
    /* Instant-follow upward: a real stall must never be underreported by a lagging ref. */
    stale_ref_scaled = scaled;
  } else {
    int64_t creep_scaled =
        (int64_t)STALE_REF_CREEP_MS_PER_FRAME * (int64_t)receiver->drift_fps * 1000LL;
    stale_ref_scaled -= creep_scaled;
    if (stale_ref_scaled < scaled)
      stale_ref_scaled = scaled;  // never creep past the current sample
  }

  if (!rebase && stale_prev_arrival_ms != 0 && arrival_ms >= stale_prev_arrival_ms) {
    int32_t gap_ms = (int32_t)(arrival_ms - stale_prev_arrival_ms);
    stale_gap_ewma_ms += (gap_ms - stale_gap_ewma_ms) / 4;
  }
  stale_prev_arrival_ms = arrival_ms;

  int64_t div = (int64_t)receiver->drift_fps * 1000LL;
  int64_t backlog_scaled = stale_ref_scaled - scaled;
  uint32_t backlog_ms = backlog_scaled > 0 ? (uint32_t)(backlog_scaled / div) : 0;

  uint32_t gap_ewma_nonneg = stale_gap_ewma_ms > 0 ? (uint32_t)stale_gap_ewma_ms : 0;
  bool drain_signature = (gap_ewma_nonneg * receiver->drift_fps) < 700U;

  return drain_signature ? backlog_ms : 0;
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
  ChiakiVideoReceiver *receiver = NULL;
  if (context.stream.session_init) {
    receiver = context.stream.session.stream_connection.video_receiver;
    if (receiver)
      frame_first_packet_ms = receiver->cur_frame_first_packet_ms;
  }

  /* GH #262: backlog-drain staleness for the presentation-side hold gate (video.c). Same
   * receiver pointer, same same-thread justification as the read directly above. */
  uint32_t staleness_ms = host_video_cb_compute_staleness_ms(receiver, frame_first_packet_ms);

  int err =
      vita_h264_decode_frame(buf, buf_size, frame_corrupt, frame_first_packet_ms, staleness_ms);
  if (err != 0) {
    LOGE("Error during video decode: %d", err);
    return false;
  }
  return true;
}
