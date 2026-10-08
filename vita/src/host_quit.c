#include "context.h"
#include "debug_tools.h"
#include "host.h"
#include "host_constants.h"
#include "host_disconnect.h"
#include "host_feedback.h"
#include "host_lifecycle.h"
#include "host_metrics.h"
#include "host_quit.h"

// Defensive include: ChiakiStreamConnectionDisconnectDelivery is already visible
// transitively via context.h -> stream_state.h -> chiaki/session.h, but include
// it explicitly since header guards make this harmless either way.
#include <chiaki/streamconnection.h>

#include <psp2/kernel/processmgr.h>

#define STREAM_RETRY_COOLDOWN_US (3 * 1000 * 1000ULL)
#define RETRY_HOLDOFF_RP_IN_USE_MS 9000
// GH #272: how long after a transport death the console is assumed to still hold the dead
// session. RP_IN_USE holdoff already encodes the measured release time, so recovery waits the
// same 9 s before the connect (a 3 s wait raced the console and drew RP_IN_USE). A resync after
// an acked DISCONNECT skips this wait and uses the release polls below instead.
#define RECOVERY_CONSOLE_RELEASE_DELAY_US ((uint64_t)RETRY_HOLDOFF_RP_IN_USE_MS * 1000ULL)
// GH #277: inside a recovery episode an RP_IN_USE refusal means the console has not released the
// old session yet, and the real release time is unknown (somewhere between ~2.2 s and ~11.3 s
// after the DISCONNECT ack on hardware). Instead of the 9 s holdoff the refused connect is
// re-probed every second (a refused probe itself costs ~250 ms, so ~1.25 s per poll). 8 polls
// after the 2 s post-stop guard cover ~12 s from the stop, past where the old single 9 s holdoff
// succeeded, so the worst case is no slower than before. Polls do not consume the attempt budget.
#define RECOVERY_RELEASE_POLL_INTERVAL_US (1000 * 1000ULL)
#define RECOVERY_RELEASE_POLL_MAX 8
#define RECOVERY_FAILED_MESSAGE "Could not reconnect to console"
#define POST_STOP_GUARD_DISCONNECT_ACKED_US (2 * 1000 * 1000ULL)
#define POST_STOP_GUARD_DISCONNECT_UNACKED_US (8 * 1000 * 1000ULL)
#define RP_IN_USE_AUTO_RETRY_DELAY_US (6 * 1000 * 1000ULL)
#define RESTART_HANDSHAKE_COOLOFF_FIRST_US (8 * 1000 * 1000ULL)
#define RESTART_HANDSHAKE_COOLOFF_REPEAT_US (12 * 1000 * 1000ULL)
#define RETRY_FAIL_DELAY_US (5 * 1000 * 1000ULL)
#define HINT_DURATION_ERROR_US (7 * 1000 * 1000ULL)

void host_handle_quit_event(ChiakiEvent *event) {
  bool user_stop_requested = context.stream.stop_requested || context.stream.stop_requested_by_user;
#if VITARPS5_DEBUG_TOOLS
  // GH #275: a debug-widget resync is a user-style stop that reconnects at once. The marker is
  // consumed here on every path so it can never leak into a later real user stop; it counts only
  // if the stop it was raised for is the one being handled.
  bool resync_quit = context.stream.resync_requested && context.stream.stop_requested;
  context.stream.resync_requested = false;
#else
  const bool resync_quit = false;
#endif
  // Snapshot BEFORE host_shutdown_media_pipeline() (called below) clears
  // is_streaming -- this tells the guard-mapping below whether a real RP
  // session was ever live on the console, or the connect was cancelled
  // before the first video frame arrived. is_streaming has several writers
  // that clear it during teardown, but host_video_cb() (host_callbacks.c) is
  // the only site that ever sets it true, so a snapshot taken here reflects
  // whether streaming genuinely started this session.
  bool had_streamed = context.stream.is_streaming;
  const char *reason_label = host_quit_reason_label(event->quit.reason);
  LOGE("EventCB CHIAKI_EVENT_QUIT (%s | code=%d \"%s\")",
       event->quit.reason_str ? event->quit.reason_str : "unknown", event->quit.reason,
       reason_label);
  LOGD(
      "Quit classification: user_stop=%d, fast_restart=%d, recovery_active=%d, "
      "teardown_in_progress=%d",
      user_stop_requested ? 1 : 0, context.stream.fast_restart_active ? 1 : 0,
      context.stream.recovery_active ? 1 : 0, context.stream.teardown_in_progress ? 1 : 0);
  LOGD("PIPE/SESSION quit gen=%u reconnect_gen=%u fps_low_windows=%u post_reconnect_low=%u",
       context.stream.session_generation, context.stream.reconnect_generation,
       context.stream.fps_under_target_windows, context.stream.post_reconnect_low_fps_windows);
  // Roll back session_generation for failed connections that never streamed.
  // This prevents "RP already in use" failures from inflating reconnect_gen.
  if (!context.stream.is_streaming && !user_stop_requested &&
      context.stream.session_generation > 0) {
    LOGD("PIPE/SESSION failed before streaming, rolling back gen %u -> %u",
         context.stream.session_generation, context.stream.session_generation - 1);
    context.stream.session_generation--;
  }
  ui_connection_cancel();
  // Every snapshot that decides finalize/recovery is taken BEFORE
  // host_shutdown_media_pipeline() below: it clears fast_restart_active and
  // reconnect_overlay_active, so reading them afterwards gives the wrong answer (GH #272).
  bool restart_failed = context.stream.fast_restart_active;
  bool recovery_was_active = context.stream.recovery_active;
  bool restart_context = restart_failed || recovery_was_active;
  // The budget and bitrate are only meaningful inside one recovery episode; a drop after a
  // successful recovery starts a fresh episode with the full budget.
  uint32_t retry_attempts = recovery_was_active ? context.stream.loss_retry_attempts : 0;
  uint32_t release_polls = recovery_was_active ? context.stream.recovery_release_polls : 0;
  // The reconnect always starts at the normal connect bitrate (host_default_video_profile), never
  // the dying session's: after a vita-initiated soft restart that session carries the lowered
  // restart profile, and a lowered renegotiation preceded a console wedging into repeated
  // "Remote Play crashed" refusals on hardware.
  uint32_t retry_holdoff_ms = context.stream.retry_holdoff_ms;
  uint64_t retry_holdoff_until = context.stream.retry_holdoff_until_us;
  bool retry_holdoff_active = context.stream.retry_holdoff_active;
  bool remote_in_use = event->quit.reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE;
  bool remote_crash = event->quit.reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH;
  bool retry_allowed_reason = host_quit_reason_requires_retry(event->quit.reason);
  bool transport_death =
      event->quit.reason == CHIAKI_QUIT_REASON_STREAM_CONNECTION_TRANSPORT_FAILED;
  // Recovery (a full teardown + fresh connect, run from the UI thread) starts or continues when
  // transport died, a vita-initiated soft restart failed, or a fallback connect itself failed.
  bool recovery_trigger = (resync_quit && context.active_host) ||
                          (!user_stop_requested && context.active_host &&
                           (transport_death || (restart_failed && retry_allowed_reason) ||
                            (recovery_was_active && (remote_in_use || retry_allowed_reason))));
  bool schedule_recovery = recovery_trigger && retry_attempts < LOSS_RETRY_MAX_ATTEMPTS;
  bool recovery_exhausted = recovery_trigger && !schedule_recovery;
  // GH #277: a fallback connect refused with RP_IN_USE inside a recovery episode is re-probed
  // after a short interval (a "release poll") until the console lets go or the polls run out.
  bool release_poll = schedule_recovery && recovery_was_active && remote_in_use &&
                      release_polls < RECOVERY_RELEASE_POLL_MAX;
  if (recovery_was_active && user_stop_requested) {
    LOGD("Recovery cancelled by user (attempt %u/%u)", retry_attempts, LOSS_RETRY_MAX_ATTEMPTS);
  } else if (recovery_was_active && !schedule_recovery) {
    LOGD("Recovery ended without another attempt (reason=%d attempts=%u/%u)", event->quit.reason,
         retry_attempts, LOSS_RETRY_MAX_ATTEMPTS);
  }
  // Publishing recovery_active before session_init is cleared keeps host_in_active_use() true
  // for the whole hand-off from the dying session to the pending fallback connect.
  context.stream.recovery_active = schedule_recovery;
  host_shutdown_media_pipeline();
  context.stream.inputs_resume_pending = schedule_recovery;
  if (schedule_recovery) {
    // The shutdown above cleared the overlay; put it back at once so the UI thread never
    // draws the main menu between teardown and the scheduling below (GH #272).
    context.stream.reconnect_overlay_active = true;
    if (!recovery_was_active)
      context.stream.reconnect_overlay_start_us = sceKernelGetProcessTimeWide();
  }
  ui_clear_waking_wait();

  // Always hand the dying session to the UI thread for join + fini. This callback runs on that
  // very session thread, so it can neither join nor reuse it; the UI thread joins only after
  // we return, and starts a fallback connect only after the finalize is done.
  context.stream.input_thread_should_exit = true;
  chiaki_mutex_lock(&context.stream.finalization_mutex);
  context.stream.session_init = false;
  chiaki_mutex_unlock(&context.stream.finalization_mutex);
  context.stream.session_finalize_pending = true;
  uint64_t now_us = sceKernelGetProcessTimeWide();
  uint32_t restart_handshake_failures = context.stream.restart_handshake_failures;
  uint64_t last_restart_handshake_fail_us = context.stream.last_restart_handshake_fail_us;
  uint64_t restart_cooloff_until_us = context.stream.restart_cooloff_until_us;
  char restart_source_snapshot[32];
  sceClibSnprintf(restart_source_snapshot, sizeof(restart_source_snapshot), "%s",
                  context.stream.last_restart_source);
  uint32_t restart_source_attempts = context.stream.restart_source_attempts;
  bool restart_handshake_failure =
      !user_stop_requested && restart_failed && event->quit.reason == CHIAKI_QUIT_REASON_STOPPED;
  if (restart_handshake_failure) {
    bool within_window =
        last_restart_handshake_fail_us &&
        now_us - last_restart_handshake_fail_us <= RESTART_HANDSHAKE_REPEAT_WINDOW_US;
    if (within_window) {
      if (restart_handshake_failures < UINT32_MAX)
        restart_handshake_failures++;
    } else {
      restart_handshake_failures = 1;
      restart_source_attempts = 1;
    }
    last_restart_handshake_fail_us = now_us;
    uint64_t cooloff_us = restart_handshake_failures > 1 ? RESTART_HANDSHAKE_COOLOFF_REPEAT_US
                                                         : RESTART_HANDSHAKE_COOLOFF_FIRST_US;
    restart_cooloff_until_us = now_us + cooloff_us;
    LOGD("PIPE/RESTART_FAIL source=%s classified=handshake_init_ack failures=%u cooloff_ms=%llu",
         (restart_source_snapshot[0] ? restart_source_snapshot : "unknown"),
         restart_handshake_failures, (unsigned long long)(cooloff_us / 1000ULL));
  }
  // A single auto-retry is armed for the *first* RP_IN_USE after a plain user
  // reconnect attempt -- distinct from arm_retry_holdoff below, which only fires
  // after a soft-restart already failed.
  bool arm_rp_in_use_auto_retry = remote_in_use && !user_stop_requested && !restart_context &&
                                  !context.stream.restart_failure_active && context.active_host &&
                                  !context.stream.rp_in_use_retry_used;
  // The arm_rp_in_use_auto_retry hint is emitted later, once retry_at is known
  // (its actual delay can exceed RP_IN_USE_AUTO_RETRY_DELAY_US when a stale
  // cooldown is still active -- see the "take the later of" comment below), so
  // the countdown text always matches the real wait. The remote_in_use/remote_crash
  // error hint has no such dependency and stays here.
  // During recovery an RP_IN_USE is an expected step (the console has not released the old
  // session yet): the Reconnecting overlay stays up and the next attempt is already scheduled,
  // so the "already active" error hint is only for a quit that really ends the attempt.
  if (context.active_host && (remote_in_use || remote_crash) && !arm_rp_in_use_auto_retry &&
      !schedule_recovery) {
    const char *hint = remote_in_use ? "Remote Play already active on console"
                                     : "Console Remote Play crashed - wait a moment";
    host_set_hint(context.active_host, hint, true, HINT_DURATION_ERROR_US);
  }
  uint64_t retry_delay = STREAM_RETRY_COOLDOWN_US;
  if (release_poll) {
    retry_delay = RECOVERY_RELEASE_POLL_INTERVAL_US;
  } else if (!context.stream.stop_requested && (remote_in_use || remote_crash)) {
    retry_delay = RETRY_FAIL_DELAY_US;
  }
  bool arm_retry_holdoff = !release_poll && !context.stream.stop_requested && remote_in_use &&
                           (restart_context || context.stream.restart_failure_active);
  if (arm_retry_holdoff) {
    context.stream.retry_holdoff_ms = RETRY_HOLDOFF_RP_IN_USE_MS;
    context.stream.retry_holdoff_until_us = now_us + (uint64_t)RETRY_HOLDOFF_RP_IN_USE_MS * 1000ULL;
    context.stream.retry_holdoff_active = true;
    retry_holdoff_ms = context.stream.retry_holdoff_ms;
    retry_holdoff_until = context.stream.retry_holdoff_until_us;
    retry_holdoff_active = context.stream.retry_holdoff_active;
    LOGD("Retry holdoff armed reason=rp_in_use_after_soft_restart duration=%u ms",
         context.stream.retry_holdoff_ms);
  }
  uint64_t throttle_until = now_us + retry_delay;
  if (!release_poll && context.stream.retry_holdoff_active &&
      context.stream.retry_holdoff_until_us > throttle_until) {
    throttle_until = context.stream.retry_holdoff_until_us;
  }
  if (context.stream.stop_requested) {
    // User-requested stop: gate the reconnect cooldown on whether — and how
    // far — our own teardown got. Three-way mapping using the full
    // ChiakiStreamConnectionDisconnectDelivery enum (not collapsed to a
    // bool), because "never sent" means two very different things depending
    // on whether a real session was ever live:
    //   - !had_streamed && NOT_SENT: cancelled before the stream ever
    //     established (e.g. during PSN auth/handshake) -- chiaki_takion_connect()
    //     may never have even succeeded, so there's no RP session on the PS5
    //     for a guard to protect. Instant re-press allowed.
    //   - ACKED: the PS5 confirmed it saw our DISCONNECT -- short guard.
    //   - SENT_UNACKED, or NOT_SENT after a real stream was live (transport
    //     died before/during teardown, see GH #208): the PS5 may still
    //     believe the session is live -- full guard.
    context.stream.post_stop_guard = true;
    ChiakiStreamConnectionDisconnectDelivery disconnect_delivery =
        chiaki_stream_connection_disconnect_delivery(&context.stream.session.stream_connection);
    uint64_t guard_us;
    const char *guard_reason;
    if (!had_streamed && disconnect_delivery == CHIAKI_STREAM_CONNECTION_DISCONNECT_NOT_SENT) {
      guard_us = 0;
      guard_reason = "cancelled before stream started, no guard";
    } else if (disconnect_delivery == CHIAKI_STREAM_CONNECTION_DISCONNECT_ACKED) {
      guard_us = POST_STOP_GUARD_DISCONNECT_ACKED_US;
      guard_reason = "DISCONNECT acked";
    } else {
      guard_us = POST_STOP_GUARD_DISCONNECT_UNACKED_US;
      guard_reason = disconnect_delivery == CHIAKI_STREAM_CONNECTION_DISCONNECT_SENT_UNACKED
                         ? "DISCONNECT sent but not acked"
                         : "DISCONNECT never sent after stream was live (transport failure)";
    }
    context.stream.next_stream_allowed_us = guard_us ? now_us + guard_us : 0;
    LOGD("Post-stop reconnect guard: %s, %llu ms before reconnect is allowed", guard_reason,
         (unsigned long long)(guard_us / 1000ULL));
  } else {
    context.stream.post_stop_guard = false;
    context.stream.next_stream_allowed_us = throttle_until;
  }
  if (context.stream.next_stream_allowed_us > now_us) {
    uint64_t wait_ms = (context.stream.next_stream_allowed_us - now_us + 999) / 1000ULL;
    LOGD("Stream cooldown engaged for %llu ms", wait_ms);
  }
  if (arm_rp_in_use_auto_retry) {
    // next_stream_allowed_us is already final at this point (throttle_until, since
    // remote_in_use implies user_stop_requested is false and the stop_requested
    // branch above was not taken) -- take the later of the fixed retry delay and
    // the cooldown gate so the auto-retry never fires while still gated.
    uint64_t retry_at = now_us + RP_IN_USE_AUTO_RETRY_DELAY_US;
    if (context.stream.next_stream_allowed_us > retry_at)
      retry_at = context.stream.next_stream_allowed_us;
    context.stream.rp_in_use_retry_at_us = retry_at;
    context.stream.rp_in_use_retry_pending = true;
    // Snapshot the PSN-vs-LAN choice from the connect attempt that just failed
    // with RP_IN_USE, so the armed retry (ui.c) restores the user's original
    // "Internet" selection instead of silently falling back to LAN. Cannot use
    // context.stream.force_psn_holepunch here -- host_stream() already consumed
    // and cleared it unconditionally at the top of this same connect attempt.
    context.stream.rp_in_use_retry_psn_holepunch = context.stream.last_connect_used_psn_holepunch;
    uint64_t retry_delay_us = retry_at - now_us;
    char hint_msg[64];
    sceClibSnprintf(hint_msg, sizeof(hint_msg), "Console busy - retrying in %llus...",
                    (unsigned long long)((retry_delay_us + 999999ULL) / 1000000ULL));
    host_set_hint(context.active_host, hint_msg, false, retry_delay_us);
    LOGD("RP_IN_USE auto-retry armed: retrying in %llu ms",
         (unsigned long long)((retry_at - now_us) / 1000ULL));
  }
  if (!user_stop_requested && !schedule_recovery) {
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
      banner_reason = (event->quit.reason_str && event->quit.reason_str[0]) ? event->quit.reason_str
                                                                            : reason_label;
    }

    if (recovery_exhausted)
      banner_reason = RECOVERY_FAILED_MESSAGE;
    host_update_disconnect_banner(banner_reason);
  }
  if (recovery_exhausted && context.active_host) {
    LOGE("Recovery exhausted after %u/%u attempts; giving up (last quit reason=%d)", retry_attempts,
         LOSS_RETRY_MAX_ATTEMPTS, event->quit.reason);
    host_set_hint(context.active_host, RECOVERY_FAILED_MESSAGE, true, HINT_DURATION_ERROR_US);
  }
  context.stream.stop_requested = false;
  host_metrics_reset_stream(true);
  if (last_restart_handshake_fail_us &&
      now_us - last_restart_handshake_fail_us > RESTART_HANDSHAKE_REPEAT_WINDOW_US) {
    restart_handshake_failures = 0;
    last_restart_handshake_fail_us = 0;
    restart_cooloff_until_us = 0;
    restart_source_snapshot[0] = '\0';
    restart_source_attempts = 0;
  }
  context.stream.restart_handshake_failures = restart_handshake_failures;
  context.stream.last_restart_handshake_fail_us = last_restart_handshake_fail_us;
  context.stream.restart_cooloff_until_us =
      restart_cooloff_until_us > now_us ? restart_cooloff_until_us : 0;
  sceClibSnprintf(context.stream.last_restart_source, sizeof(context.stream.last_restart_source),
                  "%s", restart_source_snapshot);
  context.stream.restart_source_attempts = restart_source_attempts;
  context.stream.retry_holdoff_ms = retry_holdoff_ms;
  context.stream.retry_holdoff_until_us = retry_holdoff_until;
  context.stream.retry_holdoff_active = retry_holdoff_active && retry_holdoff_until > now_us;
  if (!context.stream.retry_holdoff_active) {
    context.stream.retry_holdoff_ms = 0;
    context.stream.retry_holdoff_until_us = 0;
  }
  context.stream.loss_retry_pending = false;

  if (schedule_recovery) {
    // Nothing connects from this (dying) session thread: the UI thread starts the next attempt
    // (ui.c) once it has joined and finalized this session and loss_retry_ready_us has passed.
    // A resync reconnects as soon as the post-stop guard (set above for the acked/unacked
    // DISCONNECT) allows: the session was ended cleanly, so the console is not holding it.
    // A release poll (GH #277) re-probes one interval from now; next_stream_allowed_us was set to
    // exactly that above (no 5 s RP_IN_USE delay, no holdoff), so nothing delays or blocks it.
    uint64_t ready_us = now_us + RECOVERY_CONSOLE_RELEASE_DELAY_US;
    if (resync_quit)
      ready_us = now_us;
    else if (release_poll)
      ready_us = now_us + RECOVERY_RELEASE_POLL_INTERVAL_US;
    if (context.stream.next_stream_allowed_us > ready_us)
      ready_us = context.stream.next_stream_allowed_us;
    uint64_t since_start_ms =
        recovery_was_active ? (now_us - context.stream.reconnect_overlay_start_us) / 1000ULL : 0ULL;
    if (release_poll) {
      release_polls++;
      LOGD(
          "Release wait: RP_IN_USE poll %u/%u, %llu ms since recovery start, next probe in %llu ms",
          release_polls, RECOVERY_RELEASE_POLL_MAX, (unsigned long long)since_start_ms,
          (unsigned long long)((ready_us - now_us) / 1000ULL));
    } else if (recovery_was_active && remote_in_use) {
      LOGD(
          "Release wait: RP_IN_USE polls exhausted (%u/%u), %llu ms since recovery start, "
          "falling back to %u ms holdoff",
          release_polls, RECOVERY_RELEASE_POLL_MAX, (unsigned long long)since_start_ms,
          RETRY_HOLDOFF_RP_IN_USE_MS);
    }
    context.stream.recovery_release_polls = release_polls;
    // A release poll is not a new attempt: the budget and the Reconnecting screen's "Attempt N"
    // stay where they are.
    context.stream.loss_retry_attempts = release_poll ? retry_attempts : retry_attempts + 1;
    ChiakiConnectVideoProfile reconnect_profile = {};
    host_default_video_profile(&reconnect_profile, context.stream.last_connect_used_psn_holepunch);
    context.stream.recovery_bitrate_kbps = reconnect_profile.bitrate;
    context.stream.recovery_cause = resync_quit           ? "user resync"
                                    : recovery_was_active ? "follow-up after failed reconnect"
                                    : transport_death     ? "transport death"
                                                          : "failed packet-loss soft restart";
    context.stream.loss_retry_ready_us = ready_us;
    LOGD(
        "Recovery attempt %u/%u scheduled in %llu ms at %u kbps (reason=%d fast_restart=%d "
        "continuing=%d)",
        context.stream.loss_retry_attempts, LOSS_RETRY_MAX_ATTEMPTS,
        (unsigned long long)((ready_us - now_us) / 1000ULL), context.stream.recovery_bitrate_kbps,
        event->quit.reason, restart_failed ? 1 : 0, recovery_was_active ? 1 : 0);
    // Compiler/CPU barrier: session_finalize_pending (set above) must be visible to the UI
    // thread before loss_retry_pending, so it can never start the connect before the old
    // session has been finalized.
    __sync_synchronize();
    context.stream.loss_retry_pending = true;
  } else {
    context.stream.recovery_release_polls = 0;
    context.stream.reconnect_overlay_active = false;
#if VITARPS5_DEBUG_TOOLS
    // No further attempt is coming, so no new stream will report the resync figures.
    debug_tools_resync_clear();
#endif
    host_resume_discovery_if_needed();
    if (restart_failed && !retry_allowed_reason)
      LOGD("Skipping hard fallback retry for quit reason %d (%s)", event->quit.reason,
           reason_label);
  }
  context.stream.stop_requested_by_user = false;
  context.stream.teardown_in_progress = false;
}

/* Clears all hard-fallback recovery state (GH #272): the in-progress marker, the scheduled and
 * in-flight connect flags, the attempt budget and the Reconnecting overlay. */
static void recovery_clear_state(void) {
  context.stream.recovery_active = false;
  context.stream.loss_retry_pending = false;
  context.stream.loss_retry_attempts = 0;
  context.stream.recovery_release_polls = 0;
  context.stream.recovery_bitrate_kbps = 0;
  context.stream.recovery_cause = NULL;
  context.stream.loss_retry_ready_us = 0;
  context.stream.reconnect_overlay_active = false;
#if VITARPS5_DEBUG_TOOLS
  debug_tools_resync_clear();
#endif
}

void host_recovery_abort(const char *why) {
  LOGE("Recovery aborted: %s", why);
  recovery_clear_state();
  host_update_disconnect_banner(RECOVERY_FAILED_MESSAGE);
  if (context.active_host)
    host_set_hint(context.active_host, RECOVERY_FAILED_MESSAGE, true, HINT_DURATION_ERROR_US);
}

void host_recovery_cancel_by_user(void) {
  LOGD("Reconnecting cancelled by user (recovery_active=%d)",
       context.stream.recovery_active ? 1 : 0);
  recovery_clear_state();
  // Pairs with the barrier + recovery_active check in host_stream() before session start: a
  // connect that has not reached session_init yet is cancelled there, not by the stop below.
  __sync_synchronize();
  // No-op when no session exists (the wait phase); during a fallback connect this stops it and
  // the resulting user-stop quit never schedules another attempt.
  host_cancel_stream_request();
}
