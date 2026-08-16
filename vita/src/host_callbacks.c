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

/* GH #262 fix round 1: the original tracker read the lib's drift-excursion accounting
 * (reference-follows-drift-up, creeps-down) as a backlog signal. Code review found that
 * signal measures CATCH-UP PROGRESS, not remaining staleness -- it peaks right as content
 * goes fresh, and the 1ms/frame creep made the 120ms release threshold nearly unreachable
 * before the 6s cap (280 frames at 30fps ~= 9.3s of creep needed), so almost every hold
 * would have resolved via the cap instead of a real release. It also went silent on
 * skip-heavy episodes where the console skips frame indices across the stall (the gap and
 * the index jump cancel in the drift math in the same frame). Replaced below with a
 * model-free ARRIVAL-CADENCE state machine: no lib drift internals, just this frame's
 * gap and a cadence EWMA against the negotiated period. Everything downstream of
 * host_video_cb_compute_staleness_ms() -- the plumbed staleness_ms, the video.c hold state
 * machine, the STALE_HOLD_ENGAGE_MS/RELEASE_MS/MAX_MS thresholds -- is unchanged; only the
 * signal source changes. */

// A single inter-arrival gap this large is a real stall, not jitter. Forensics: healthy-
// window cadence_max tops out ~113ms; real stalls in the #262 hardware log ran 509-4400ms.
// 400 sits with clear margin above the former and below the latter.
#define STALE_STALL_TRIGGER_MS 400
// Clamp the latched trigger gap so one pathological outlier gap can't blow the reported
// staleness (and therefore the video.c hold, whose engage threshold reads this value)
// wildly out of proportion -- 5s is already most of the way to the 6s hold hard cap.
#define STALE_STALL_GAP_CLAMP_MS 5000
// After an armed gap, confirm within this many arrivals that a drain (not a skip-past-the-
// backlog or a server-side catch-up) is actually happening. 8 frames at the ~14ms drain
// cadence observed in forensics is ~112ms -- fast enough to confirm a real drain promptly,
// slow enough that one stray short gap right after the trigger doesn't false-negative it.
#define STALE_DRAIN_CONFIRM_FRAMES 8
// Consecutive normal-cadence arrivals required before releasing a hold. The console only
// sends faster than the negotiated period while it still has backlog to clear (that's the
// drain signature itself), so cadence returning to ~period-length gaps IS "caught up" by
// construction -- a few consecutive frames confirm it wasn't a one-frame blip.
#define STALE_RELEASE_CONFIRM_FRAMES 3

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

/* GH #262 staleness tracker: arrival-cadence state machine, recv-thread-only (same
 * single-thread rationale as cur_frame_first_packet_ms above -- this all runs
 * synchronously inside chiaki_video_receiver_flush_frame(), one thread, program order, no
 * concurrent writer). Plain (non-volatile) file-statics are correct here for the same
 * reason plain locals would be, except they must persist frame-to-frame. Reset per-stream
 * by host_video_cb_reset_stale_tracker(), called from host_metrics_reset_stream() (its
 * only caller -- see host.c:450 and host_quit.c:248, the two per-stream-reset call sites)
 * -- otherwise state left over from a PRIOR stream's mid-episode tracking would compute
 * bogus staleness against the new stream's arrivals. */
typedef enum {
  STALE_TRACKER_IDLE,     // no recent stall; reporting 0
  STALE_TRACKER_PENDING,  // a stall-sized gap fired; waiting up to N frames to confirm a drain
  STALE_TRACKER_HOLDING,  // drain confirmed; reporting the latched stall gap until cadence
                          // normalizes
} StaleTrackerState;

static StaleTrackerState stale_tracker_state = STALE_TRACKER_IDLE;
static uint64_t stale_prev_arrival_ms = 0;  // previous frame's cur_frame_first_packet_ms
static int32_t stale_gap_ewma_ms = 0;       // inter-arrival EWMA (alpha = 1/4)
static uint32_t stale_stall_gap_ms = 0;     // latched (clamped) trigger gap, reported while HOLDING
static uint32_t stale_pending_frames = 0;   // arrivals seen since PENDING armed
static uint32_t stale_release_frames = 0;   // consecutive normal-cadence arrivals while HOLDING

void host_video_cb_reset_stale_tracker(void) {
  stale_tracker_state = STALE_TRACKER_IDLE;
  stale_prev_arrival_ms = 0;
  stale_gap_ewma_ms = 0;
  stale_stall_gap_ms = 0;
  stale_pending_frames = 0;
  stale_release_frames = 0;
}

/* GH #262 fix round 1: model-free arrival-cadence gate (replaces the drift-excursion
 * tracker code review flagged as measuring catch-up progress, not remaining staleness --
 * see the constants block above for the full finding). Three states:
 *
 *   IDLE -> PENDING:  a single gap > STALE_STALL_TRIGGER_MS fires unconditionally (no
 *     drift/receiver internals needed -- just this frame's arrival vs the previous one).
 *     The EWMA resets to one period at arm time so the pre-stall cadence can't mask the
 *     post-stall drain check below.
 *   PENDING -> HOLDING (confirm): within STALE_DRAIN_CONFIRM_FRAMES arrivals, if the
 *     cadence EWMA drops under 0.7x the negotiated period -- the ~2.4x drain signature --
 *     the stall is confirmed real and every frame reports the latched stall_gap_ms.
 *   PENDING -> IDLE (disarm): confirmation doesn't arrive within the window -- the console
 *     skipped the backlog itself or caught up server-side, so by definition there is
 *     nothing stale left to hold. Reports 0 the whole time it was PENDING (never holds on
 *     an unconfirmed guess).
 *   HOLDING -> IDLE (release): cadence back at/above 0.9x period for
 *     STALE_RELEASE_CONFIRM_FRAMES consecutive arrivals -- fast delivery only happens while
 *     there is backlog to clear, so normal cadence resuming IS "caught up" by construction.
 *     A fresh qualifying stall while HOLDING re-latches (max of remaining vs new) and resets
 *     the release-confirm streak instead of releasing on stale data.
 *
 * Both the confirm and release checks are integer-only, cross-multiplied against
 * drift_fps (or a default of 30, i.e. ~33ms period, before DELIVERY_INIT/no receiver) to
 * avoid dividing: gap_ewma_ms * fps < 700 <=> gap_ewma_ms < 0.7 * (1000/fps); likewise
 * >= 900 <=> >= 0.9 * (1000/fps). */
static uint32_t host_video_cb_compute_staleness_ms(ChiakiVideoReceiver *receiver,
                                                   uint64_t arrival_ms) {
  if (arrival_ms == 0) {
    /* No valid timestamp for this call (e.g. the profile-header injection call, which runs
     * before cur_frame_first_packet_ms is set for the first frame of a session). Nothing to
     * measure -- and touching stale_prev_arrival_ms with 0 would corrupt the next real
     * frame's gap -- so report 0 without touching tracker state. */
    return 0;
  }

  uint32_t fps_for_calc = (receiver != NULL && receiver->drift_fps != 0) ? receiver->drift_fps : 30;

  if (stale_prev_arrival_ms == 0 || arrival_ms < stale_prev_arrival_ms) {
    /* First arrival this tracker instance (fresh stream -- reset zeroed
     * stale_prev_arrival_ms -- or the very first frame ever) or a backwards clock glitch:
     * nothing to diff against yet. Seed the EWMA at one period ("normal") rather than 0 so
     * a PENDING confirm can't spuriously fire on the very next frame. */
    stale_prev_arrival_ms = arrival_ms;
    stale_gap_ewma_ms = (int32_t)(1000U / fps_for_calc);
    return 0;
  }

  uint32_t gap_ms = (uint32_t)(arrival_ms - stale_prev_arrival_ms);
  stale_prev_arrival_ms = arrival_ms;

  if (gap_ms > STALE_STALL_TRIGGER_MS) {
    /* Arm (or re-latch) unconditionally, then return WITHOUT falling into the confirm/
     * release switch below this same frame -- the EWMA reset a few lines down makes this
     * frame's cadence artificially "normal", so evaluating confirm/release against it here
     * would be spurious (e.g. a fresh re-latch would immediately look like a release, one
     * statement after resetting stale_release_frames to guard against exactly that). */
    uint32_t clamped_gap_ms = gap_ms > STALE_STALL_GAP_CLAMP_MS ? STALE_STALL_GAP_CLAMP_MS : gap_ms;
    stale_gap_ewma_ms = (int32_t)(1000U / fps_for_calc);
    if (stale_tracker_state == STALE_TRACKER_HOLDING) {
      if (clamped_gap_ms > stale_stall_gap_ms)
        stale_stall_gap_ms = clamped_gap_ms;
      stale_release_frames = 0;  // not caught up after all -- a fresh stall just landed
      return stale_stall_gap_ms;
    }
    if (stale_tracker_state == STALE_TRACKER_IDLE)
      LOGD("PIPE/STALE_HOLD armed gap_ms=%u", gap_ms);
    stale_stall_gap_ms = clamped_gap_ms;
    stale_pending_frames = 0;
    stale_tracker_state = STALE_TRACKER_PENDING;
    return 0;  // not confirmed yet
  }

  stale_gap_ewma_ms += ((int32_t)gap_ms - stale_gap_ewma_ms) / 4;
  uint32_t gap_ewma_nonneg = stale_gap_ewma_ms > 0 ? (uint32_t)stale_gap_ewma_ms : 0;

  switch (stale_tracker_state) {
    case STALE_TRACKER_PENDING: {
      stale_pending_frames++;
      bool drain_signature = (gap_ewma_nonneg * fps_for_calc) < 700U;
      if (drain_signature) {
        stale_tracker_state = STALE_TRACKER_HOLDING;
        stale_release_frames = 0;
        return stale_stall_gap_ms;
      }
      if (stale_pending_frames >= STALE_DRAIN_CONFIRM_FRAMES)
        stale_tracker_state = STALE_TRACKER_IDLE;  // never confirmed -- nothing to hold
      return 0;
    }
    case STALE_TRACKER_HOLDING: {
      bool cadence_normal = (gap_ewma_nonneg * fps_for_calc) >= 900U;
      if (cadence_normal) {
        stale_release_frames++;
        if (stale_release_frames >= STALE_RELEASE_CONFIRM_FRAMES) {
          stale_tracker_state = STALE_TRACKER_IDLE;
          return 0;
        }
      } else {
        stale_release_frames = 0;
      }
      return stale_stall_gap_ms;
    }
    case STALE_TRACKER_IDLE:
    default:
      return 0;
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
