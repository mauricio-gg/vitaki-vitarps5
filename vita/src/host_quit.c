#include "context.h"
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
#define LOSS_RETRY_DELAY_US (2 * 1000 * 1000ULL)
#define LOSS_RETRY_MAX_ATTEMPTS 2
#define RETRY_HOLDOFF_RP_IN_USE_MS 9000
#define POST_STOP_GUARD_DISCONNECT_ACKED_US (2 * 1000 * 1000ULL)
#define POST_STOP_GUARD_DISCONNECT_UNACKED_US (8 * 1000 * 1000ULL)
#define RP_IN_USE_AUTO_RETRY_DELAY_US (6 * 1000 * 1000ULL)
#define RESTART_HANDSHAKE_COOLOFF_FIRST_US (8 * 1000 * 1000ULL)
#define RESTART_HANDSHAKE_COOLOFF_REPEAT_US (12 * 1000 * 1000ULL)
#define RETRY_FAIL_DELAY_US (5 * 1000 * 1000ULL)
#define HINT_DURATION_ERROR_US (7 * 1000 * 1000ULL)

void host_handle_quit_event(ChiakiEvent *event) {
  bool user_stop_requested = context.stream.stop_requested || context.stream.stop_requested_by_user;
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
      "Quit classification: user_stop=%d, fast_restart=%d, retry_pending=%d, retry_active=%d, "
      "teardown_in_progress=%d",
      user_stop_requested ? 1 : 0, context.stream.fast_restart_active ? 1 : 0,
      context.stream.loss_retry_pending ? 1 : 0, context.stream.loss_retry_active ? 1 : 0,
      context.stream.teardown_in_progress ? 1 : 0);
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
  host_shutdown_media_pipeline();
  context.stream.inputs_resume_pending = fallback_active;
  ui_clear_waking_wait();

  // Only finalize if not retrying/restarting
  bool should_finalize = !fallback_active && !context.stream.fast_restart_active;
  if (should_finalize) {
    context.stream.input_thread_should_exit = true;
    // Clear session_init so host_stream() doesn't block on the stale flag.
    // The actual join+fini is deferred to the UI thread.
    chiaki_mutex_lock(&context.stream.finalization_mutex);
    context.stream.session_init = false;
    chiaki_mutex_unlock(&context.stream.finalization_mutex);
    context.stream.session_finalize_pending = true;
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
  sceClibSnprintf(restart_source_snapshot, sizeof(restart_source_snapshot), "%s",
                  context.stream.last_restart_source);
  uint32_t restart_source_attempts = context.stream.restart_source_attempts;
  bool remote_in_use = event->quit.reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE;
  bool remote_crash = event->quit.reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH;
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
  if (context.active_host && (remote_in_use || remote_crash) && !arm_rp_in_use_auto_retry) {
    const char *hint = remote_in_use ? "Remote Play already active on console"
                                     : "Console Remote Play crashed - wait a moment";
    host_set_hint(context.active_host, hint, true, HINT_DURATION_ERROR_US);
  }
  uint64_t retry_delay = STREAM_RETRY_COOLDOWN_US;
  if (!context.stream.stop_requested && (remote_in_use || remote_crash)) {
    retry_delay = RETRY_FAIL_DELAY_US;
  }
  bool arm_retry_holdoff = !context.stream.stop_requested && remote_in_use &&
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
  if (context.stream.retry_holdoff_active &&
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
      banner_reason = (event->quit.reason_str && event->quit.reason_str[0]) ? event->quit.reason_str
                                                                            : reason_label;
    }

    host_update_disconnect_banner(banner_reason);
  }
  context.stream.stop_requested = false;
  bool should_resume_discovery = !retry_pending;
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
  context.stream.loss_retry_attempts = retry_attempts;
  context.stream.loss_retry_bitrate_kbps = retry_bitrate;
  context.stream.loss_retry_ready_us = retry_ready;
  context.stream.retry_holdoff_ms = retry_holdoff_ms;
  context.stream.retry_holdoff_until_us = retry_holdoff_until;
  context.stream.retry_holdoff_active = retry_holdoff_active && retry_holdoff_until > now_us;
  if (!context.stream.retry_holdoff_active) {
    context.stream.retry_holdoff_ms = 0;
    context.stream.retry_holdoff_until_us = 0;
  }
  context.stream.loss_retry_pending = false;
  context.stream.loss_retry_active = false;
  context.stream.reconnect_overlay_active = false;

  bool retry_allowed_reason = host_quit_reason_requires_retry(event->quit.reason);
  bool schedule_retry = restart_failed && context.active_host && retry_allowed_reason &&
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
    LOGD(
        "Soft restart failed — scheduling hard fallback retry #%u in %llu ms (cooldown=%llu ms, "
        "base_delay=%llu ms)",
        retry_attempts + 1, (effective_retry_us - now_us) / 1000ULL,
        cooldown_target > now_us ? (cooldown_target - now_us) / 1000ULL : 0ULL,
        LOSS_RETRY_DELAY_US / 1000ULL);
  }

  if (should_resume_discovery)
    host_resume_discovery_if_needed();

  if (schedule_retry && context.active_host) {
    uint64_t now_retry = sceKernelGetProcessTimeWide();
    uint64_t desired =
        context.stream.loss_retry_ready_us ? context.stream.loss_retry_ready_us : now_retry;
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
         context.stream.loss_retry_bitrate_kbps ? context.stream.loss_retry_bitrate_kbps
                                                : LOSS_RETRY_BITRATE_KBPS);
    int restart_result = host_stream(context.active_host);
    if (restart_result != 0) {
      LOGE("Fallback restart failed (%d)", restart_result);
      context.stream.loss_retry_active = false;
      context.stream.reconnect_overlay_active = false;
      context.stream.last_restart_failure_us = sceKernelGetProcessTimeWide();
      context.stream.restart_failure_active = true;
      // Defer finalization — UI thread will join + fini
      context.stream.input_thread_should_exit = true;
      chiaki_mutex_lock(&context.stream.finalization_mutex);
      context.stream.session_init = false;
      chiaki_mutex_unlock(&context.stream.finalization_mutex);
      context.stream.session_finalize_pending = true;
    } else {
      context.stream.loss_retry_active = false;
      context.stream.reconnect_overlay_active = false;
      host_resume_discovery_if_needed();
    }
  } else if (restart_failed && !retry_allowed_reason) {
    LOGD("Skipping hard fallback retry for quit reason %d (%s)", event->quit.reason, reason_label);
  }
  context.stream.stop_requested_by_user = false;
  context.stream.teardown_in_progress = false;
}
