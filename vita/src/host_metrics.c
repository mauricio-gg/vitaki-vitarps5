#include "context.h"
#include "host_metrics.h"
#include "host_feedback.h"
#include "host_recovery.h"
#include "host_callbacks.h"
#include "audio.h"
#include "video.h"
#include "sps_probe.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/netctl.h>
#include <stdio.h>
#include <string.h>

#define AV_DIAG_LOG_INTERVAL_US (5 * 1000 * 1000ULL)
#define AV_DIAG_STALE_SNAPSHOT_WARN_STREAK 5

/* Windowed-bitrate ring: accumulate at least this much wall-clock before
 * closing a window; discard windows longer than the max (post-stall gaps). */
#define BITRATE_WINDOW_MIN_US 100000ULL  /* 100 ms */
#define BITRATE_WINDOW_MAX_US 2000000ULL /* 2 s */

/* Latency investigation: nearest-rank percentile selectors for the per-window e2e
 * latency sample array (see host_metrics_update_latency() below). */
#define LATENCY_PERCENTILE_P50 50
#define LATENCY_PERCENTILE_P95 95

/* GH #258: PS-button suspend/resume detection and recovery scheduling. A gap this large
 * between consecutive UI-loop ticks cannot happen while actually streaming (normal cadence
 * is 1-33ms). Lowered from 1s to 500ms per log 99798574648_vitarps5-testing.log forensics
 * (GH #261 investigation): the original 1s threshold missed the user's real PS-button taps,
 * which measured ~850-980ms -- under the old threshold entirely. 500ms still clears the
 * 1-33ms normal tick cadence by >15x, so false positives from an ordinary frame stall remain
 * as unlikely as before. */
#define SUSPEND_GAP_THRESHOLD_US 500000ULL
/* First scheduled resync fires quickly in case the console's resume burst hasn't yet
 * stepped on the decoder's reference chain. */
#define SUSPEND_RESYNC_FIRST_DELAY_US 250000ULL
/* Second shot lands after the observed ~0.9s drain of the resume burst, bracketing the
 * window where the first shot's IDR reply is likely to die in the keystream-desync burst
 * (see GH #258 hardware evidence: both IDR replies #1 and #2 died there). */
#define SUSPEND_RESYNC_FOLLOWUP_DELAY_US 1250000ULL
/* Two shots bracket the drain window observed on hardware; a single shot risks landing
 * inside the same failure window as the resume burst. */
#define SUSPEND_RESYNC_SHOTS 2

/* PIPE/DECHOLD: tries at a consistent copy of the decode thread's published window, and the
 * last copy that was won (printed again if a window's copy fails). Reset with the stream. */
#define DECHOLD_READ_ATTEMPTS 4
static DecHoldWindow dechold_last;
/* PIPE/AUDIOOUT: tries at a consistent copy of the audio thread's published window, and the last
 * consistent copy (printed again when no try succeeds). */
#define AUDIOOUT_READ_ATTEMPTS 4
static AudioOutWindow audioout_last;

/* PIPE/UITHREAD: what was printed last this stream; the line repeats only when one of these moves.
 */
static struct {
  bool printed;
  int32_t prio;
  int32_t affinity;
  int32_t cpu;
} ui_thread_printed;

/* PIPE/RECVBUF: same scheme for the receive thread's published window. Reset with the stream. */
#define RECVBUF_READ_ATTEMPTS 4
#define RECVBUF_BUSY_PERMILLE_PER_PCT 10 /* busy_permille is in tenths of a percent */
static ChiakiTakionRecvbufWindow recvbuf_last;

/* PIPE/CONSOLE: tries at a consistent copy of the stream connection's console info; the
 * previous copy (baseline for the per-window deltas, and what is printed again if a copy fails);
 * whether PIPE/CONSOLE_TS was already printed this session. All reset with the stream. */
#define CONSOLE_READ_ATTEMPTS 4
static ChiakiConsoleInfo console_prev;
static bool console_ts_printed;

/* PIPE/SPS and PIPE/SPS_REPEAT: the probe version at the last look (a new copy is taken only when
 * it moved), whether the once-per-session PIPE/SPS line was printed, the repeat counts already
 * printed and when. All reset with the stream. */
#define SPS_READ_ATTEMPTS 4
#define SPS_REPEAT_MIN_INTERVAL_US 1000000ULL /* at most one PIPE/SPS_REPEAT per window */
#define SPS_HEX_BUF_SIZE (SPS_PROBE_CAP * 2 + 1)
#define SPS_CUT_BUF_SIZE 32
#define SPS_OPT_BUF_SIZE 16
static uint32_t sps_seen_version;
static bool sps_line_printed;
static uint32_t sps_repeat_printed_seen;
static uint32_t sps_repeat_printed_differed;
static uint64_t sps_repeat_last_us;
/* Text buffers of the PIPE/CONSOLE line: the type:count list (overflow is shown as "..."), one
 * formatted number, and the hex dump of the first periodic-timestamp message. */
#define CONSOLE_TYPES_BUF_SIZE 256
#define CONSOLE_NUM_BUF_SIZE 32
#define CONSOLE_TS_HEX_BUF_SIZE (CHIAKI_CONSOLE_INFO_FIRST_TS_CAP * 2 + 1)
/* rtt is a double of unverified unit; small steps must be visible, so six decimals. */
#define CONSOLE_RTT_DECIMALS 6
#define CONSOLE_LOSS_DECIMALS 4

/* Insertion sort for the bounded (<= LATENCY_WINDOW_SAMPLE_CAP) per-window latency sample
 * array. O(n^2) worst case is trivial at this size (<=64 elements) and this runs once per
 * ~1s window on the UI thread -- not a hot path -- so a tiny in-place sort is simpler than
 * wiring qsort()'s comparator for what would otherwise be this file's only sort call. */
static void sort_latency_samples(uint32_t *samples, uint32_t count) {
  for (uint32_t i = 1; i < count; i++) {
    uint32_t key = samples[i];
    uint32_t j = i;
    while (j > 0 && samples[j - 1] > key) {
      samples[j] = samples[j - 1];
      j--;
    }
    samples[j] = key;
  }
}

/* Nearest-rank percentile index into a sorted array of `count` elements. */
static uint32_t percentile_index(uint32_t count, uint32_t percentile) {
  if (count == 0)
    return 0;
  uint32_t idx = (count * percentile) / 100;
  if (idx >= count)
    idx = count - 1;
  return idx;
}

/* PIPE/DISPLAY: reduce one display-pipeline stage ring (see vita/src/video.c
 * record_display_stage_sample() and the field comment in stream_state.h) to p50/p95, then
 * reset the ring for the next window -- same sort/percentile-index/reset pattern as the
 * PIPE/LATENCY reduce block below, factored into a helper since PIPE/DISPLAY has four
 * structurally-identical rings to reduce per window instead of one. out_n/out_dropped
 * report this window's sample count and overwrite count (snapshotted BEFORE reset) so the
 * caller can decide which ring's counts to publish -- see the call site below for why only
 * one of the four is actually printed. */
static void reduce_display_stage_ring(uint32_t *samples, uint32_t *count, uint32_t *write_idx,
                                      uint32_t *dropped_count, uint32_t *out_p50, uint32_t *out_p95,
                                      uint32_t *out_n, uint32_t *out_dropped) {
  uint32_t n = *count;
  if (n > 0) {
    sort_latency_samples(samples, n);
    *out_p50 = samples[percentile_index(n, LATENCY_PERCENTILE_P50)];
    *out_p95 = samples[percentile_index(n, LATENCY_PERCENTILE_P95)];
  } else {
    *out_p50 = 0;
    *out_p95 = 0;
  }
  *out_n = n;
  *out_dropped = *dropped_count;
  *count = 0;
  *write_idx = 0;
  *dropped_count = 0;
}

void host_metrics_reset_stream(bool preserve_recovery_state) {
  context.stream.measured_bitrate_mbps = 0.0f;
  context.stream.measured_rtt_ms = 0;
  context.stream.last_rtt_refresh_us = 0;
  context.stream.metrics_last_update_us = 0;
  // GH #258: always clear the suspend-detection anchor, even when preserve_recovery_state
  // is set (fast restart) -- a stale anchor surviving a fast restart would read as a bogus
  // multi-second gap on the next tick and fire a spurious resync.
  context.stream.suspend_tick_last_us = 0;
  context.stream.suspend_resync_next_us = 0;
  context.stream.suspend_resync_shots_left = 0;
  context.stream.suspend_fps_guard = false;
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

  // D1: Decode timing
  context.stream.decode_time_us = 0;
  context.stream.decode_avg_us = 0;
  context.stream.decode_max_us = 0;
  context.stream.decode_window_total_us = 0;
  context.stream.decode_window_max_us = 0;
  context.stream.decode_window_count = 0;

  // D4: Windowed bitrate
  context.stream.bitrate_prev_bytes = 0;
  context.stream.bitrate_prev_frames = 0;
  context.stream.bitrate_prev_update_us = 0;
  memset(context.stream.bitrate_window_delta_bytes, 0,
         sizeof(context.stream.bitrate_window_delta_bytes));
  memset(context.stream.bitrate_window_elapsed_us, 0,
         sizeof(context.stream.bitrate_window_elapsed_us));
  context.stream.bitrate_window_index = 0;
  context.stream.bitrate_window_filled = 0;
  context.stream.windowed_bitrate_mbps = 0.0f;

  // D5: Frame overwrite
  context.stream.frame_overwrite_count = 0;
  context.stream.freeze_engaged_count = 0;

  // D6: Wi-Fi RSSI
  context.stream.wifi_rssi = -1;

  // D7: Display FPS
  context.stream.display_fps = 0;
  context.stream.display_frame_count = 0;
  context.stream.display_fps_window_start_us = 0;

  // Stuck bitrate detection (streak resets always; once-per-session flag
  // survives fast restarts so we don't re-trigger after our own restart)
  context.stream.stuck_bitrate_low_fps_streak = 0;
  if (!preserve_recovery_state)
    context.stream.stuck_bitrate_restart_used = false;

  // Cascade alarm (streak resets always; once-per-session flag
  // survives fast restarts so we don't re-trigger after our own restart)
  context.stream.cascade_prev_missing_ref_count = 0;
  context.stream.cascade_alarm_streak = 0;
  if (!preserve_recovery_state)
    context.stream.cascade_alarm_restart_used = false;
  context.stream.cascade_alarm_last_action_us = 0;

  // Latency investigation instrumentation
  memset(context.stream.latency_window_samples_ms, 0,
         sizeof(context.stream.latency_window_samples_ms));
  context.stream.latency_window_sample_count = 0;
  context.stream.latency_window_write_idx = 0;
  context.stream.latency_window_dropped_count = 0;  // per-window; see stream_state.h
  context.stream.latency_dropped_total_count = 0;   // session-cumulative twin
  context.stream.latency_p50_ms = 0;
  context.stream.latency_p95_ms = 0;
  context.stream.latency_max_ms = 0;
  context.stream.latency_sample_n = 0;
  context.stream.latency_dropped_n = 0;
  context.stream.latency_log_last_us = 0;

  // PIPE/DISPLAY instrumentation
  memset(context.stream.display_pickup_samples_us, 0,
         sizeof(context.stream.display_pickup_samples_us));
  context.stream.display_pickup_sample_count = 0;
  context.stream.display_pickup_write_idx = 0;
  context.stream.display_pickup_dropped_count = 0;
  memset(context.stream.display_snapshot_samples_us, 0,
         sizeof(context.stream.display_snapshot_samples_us));
  context.stream.display_snapshot_sample_count = 0;
  context.stream.display_snapshot_write_idx = 0;
  context.stream.display_snapshot_dropped_count = 0;
  memset(context.stream.display_draw_samples_us, 0, sizeof(context.stream.display_draw_samples_us));
  context.stream.display_draw_sample_count = 0;
  context.stream.display_draw_write_idx = 0;
  context.stream.display_draw_dropped_count = 0;
  memset(context.stream.display_swap_samples_us, 0, sizeof(context.stream.display_swap_samples_us));
  context.stream.display_swap_sample_count = 0;
  context.stream.display_swap_write_idx = 0;
  context.stream.display_swap_dropped_count = 0;
  context.stream.display_paced_count = 0;
  context.stream.display_pickup_p50_us = 0;
  context.stream.display_pickup_p95_us = 0;
  context.stream.display_snapshot_p50_us = 0;
  context.stream.display_snapshot_p95_us = 0;
  context.stream.display_draw_p50_us = 0;
  context.stream.display_draw_p95_us = 0;
  context.stream.display_swap_p50_us = 0;
  context.stream.display_swap_p95_us = 0;
  context.stream.display_sample_n = 0;
  context.stream.display_dropped_n = 0;
  context.stream.display_paced_n = 0;

  memset(&context.stream.press_acc, 0, sizeof(context.stream.press_acc));
  memset(&context.stream.press_pub, 0, sizeof(context.stream.press_pub));
  context.stream.press_seen_presses = 0;
  memset(&context.stream.press_seen, 0, sizeof(context.stream.press_seen));
  context.stream.press_total_presses = 0;
  context.stream.press_total_samples = 0;

  context.stream.dechold_lock = 0;
  memset(&context.stream.dechold_pub, 0, sizeof(context.stream.dechold_pub));
  memset(&dechold_last, 0, sizeof(dechold_last));
  context.stream.audioout_lock = 0;
  memset(&context.stream.audioout_pub, 0, sizeof(context.stream.audioout_pub));
  memset(&audioout_last, 0, sizeof(audioout_last));
  memset(&ui_thread_printed, 0, sizeof(ui_thread_printed));
  memset(&recvbuf_last, 0, sizeof(recvbuf_last));
  recv_probe_reset_report();
  memset(&console_prev, 0, sizeof(console_prev));
  console_ts_printed = false;
  sps_seen_version = 0;
  sps_line_printed = false;
  sps_repeat_printed_seen = 0;
  sps_repeat_printed_differed = 0;
  sps_repeat_last_us = 0;

  context.stream.input_latency_p50_us = 0;
  context.stream.input_latency_p95_us = 0;
  context.stream.input_latency_max_us = 0;
  context.stream.input_latency_sample_n = 0;
  context.stream.input_latency_dropped_n = 0;
  context.stream.decode_q_occ_avg = 0;
  context.stream.decode_q_occ_max = 0;
  context.stream.audio_q_occ_avg = 0;
  context.stream.audio_q_occ_max = 0;
  context.stream.net_unstable_banner_streak = 0;
  context.stream.net_unstable_last_loss_us = 0;

  context.stream.disconnect_reason[0] = '\0';
  context.stream.disconnect_banner_until_us = 0;
  context.stream.loss_retry_pending = false;
  // GH #272: a hard-fallback connect calls this via host_stream(); its attempt budget, bitrate
  // and Reconnecting overlay must survive until the new session reports CONNECTED.
  if (!context.stream.recovery_active) {
    context.stream.loss_retry_attempts = 0;
    context.stream.recovery_release_polls = 0;
    context.stream.recovery_bitrate_kbps = 0;
    context.stream.recovery_cause = NULL;
    context.stream.loss_retry_ready_us = 0;
    context.stream.reconnect_overlay_active = false;
    context.stream.reconnect_overlay_start_us = 0;
  }
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
  context.stream.auto_reconnect_count = 0;
  context.stream.stop_requested_by_user = false;
  context.stream.teardown_in_progress = false;
  vitavideo_hide_poor_net_indicator();
}

/* Window figure of a session-total counter: the growth since the previous copy. A counter that
 * went down means the stream connection was re-initialised, so its value is the whole growth. */
static uint32_t console_counter_delta(uint32_t cur, uint32_t prev) {
  return cur >= prev ? cur - prev : cur;
}

/* Formats a value the console may never have sent: "-" when it did not, never a made-up zero. */
static void console_format_double(char *out, size_t size, bool seen, int decimals, double value) {
  if (seen)
    snprintf(out, size, "%.*f", decimals, value);
  else
    snprintf(out, size, "-");
}

static void console_format_u64(char *out, size_t size, bool seen, uint64_t value) {
  if (seen)
    snprintf(out, size, "%llu", (unsigned long long)value);
  else
    snprintf(out, size, "-");
}

/* Appends "label:count," to the type list. On lack of room it stops appending and sets *truncated,
 * so the caller can make the cut visible. */
static void console_append_type(char *buf, size_t size, size_t *len, bool *truncated,
                                const char *label, uint32_t count) {
  if (*truncated)
    return;
  int n = snprintf(buf + *len, size - *len, "%s:%u,", label, count);
  if (n < 0 || (size_t)n >= size - *len) {
    *truncated = true;
    buf[*len] = '\0';
    return;
  }
  *len += (size_t)n;
}

/* PIPE/CONSOLE and the one-off PIPE/CONSOLE_TS: what the console told us in control messages
 * (docs/design/streaming.md section 9.4). Copies the receive thread's info under its sequence
 * lock, prints window figures as deltas against the previous copy and never writes anything the
 * receive thread owns. Call once per window, only when show_latency is on. */
static void host_metrics_report_console(ChiakiStreamConnection *stream_connection) {
  ChiakiConsoleInfo cur;
  bool copied = false;
  for (int attempt = 0; attempt < CONSOLE_READ_ATTEMPTS && !copied; attempt++) {
    uint32_t lock_before = stream_connection->console_info_lock;
    if (lock_before & 1)
      continue;
    __sync_synchronize();
    cur = stream_connection->console_info;
    __sync_synchronize();
    copied = stream_connection->console_info_lock == lock_before;
  }
  if (!copied)
    cur = console_prev;  // no consistent copy: print the last one, window figures are 0
  const ChiakiConsoleInfo *prev = &console_prev;

  // rtt: latest value, and min/max over the window's values (the newest ones if the ring is full).
  uint32_t rtt_window_n = console_counter_delta(cur.quality_rtt_msgs, prev->quality_rtt_msgs);
  uint32_t range_n =
      rtt_window_n < CHIAKI_CONSOLE_INFO_RTT_RING ? rtt_window_n : CHIAKI_CONSOLE_INFO_RTT_RING;
  double rtt_min = 0.0;
  double rtt_max = 0.0;
  for (uint32_t i = 0; i < range_n; i++) {
    double v = cur.rtt_ring[(cur.quality_rtt_msgs - 1 - i) % CHIAKI_CONSOLE_INFO_RTT_RING];
    if (i == 0 || v < rtt_min)
      rtt_min = v;
    if (i == 0 || v > rtt_max)
      rtt_max = v;
  }

  char rtt_s[CONSOLE_NUM_BUF_SIZE];
  char rtt_min_s[CONSOLE_NUM_BUF_SIZE];
  char rtt_max_s[CONSOLE_NUM_BUF_SIZE];
  char target_s[CONSOLE_NUM_BUF_SIZE], upstream_s[CONSOLE_NUM_BUF_SIZE];
  char upstream_loss_s[CONSOLE_NUM_BUF_SIZE], loss_s[CONSOLE_NUM_BUF_SIZE];
  char ts_gap_s[CONSOLE_NUM_BUF_SIZE];
  console_format_double(rtt_s, sizeof(rtt_s), cur.quality_rtt_msgs > 0, CONSOLE_RTT_DECIMALS,
                        cur.rtt);
  console_format_double(rtt_min_s, sizeof(rtt_min_s), range_n > 0, CONSOLE_RTT_DECIMALS, rtt_min);
  console_format_double(rtt_max_s, sizeof(rtt_max_s), range_n > 0, CONSOLE_RTT_DECIMALS, rtt_max);
  console_format_u64(target_s, sizeof(target_s),
                     cur.quality_seen & CHIAKI_CONSOLE_INFO_SEEN_TARGET_BITRATE,
                     cur.target_bitrate);
  console_format_u64(upstream_s, sizeof(upstream_s),
                     cur.quality_seen & CHIAKI_CONSOLE_INFO_SEEN_UPSTREAM_BITRATE,
                     cur.upstream_bitrate);
  console_format_double(upstream_loss_s, sizeof(upstream_loss_s),
                        cur.quality_seen & CHIAKI_CONSOLE_INFO_SEEN_UPSTREAM_LOSS,
                        CONSOLE_LOSS_DECIMALS, cur.upstream_loss);
  console_format_u64(loss_s, sizeof(loss_s), cur.quality_seen & CHIAKI_CONSOLE_INFO_SEEN_LOSS,
                     cur.loss);
  console_format_u64(ts_gap_s, sizeof(ts_gap_s), cur.ts_msgs >= 2, cur.ts_gap_ms);

  // Session totals per ignored type number, every type seen at least once; "other" = out of range.
  char types[CONSOLE_TYPES_BUF_SIZE];
  size_t types_len = 0;
  bool types_truncated = false;
  types[0] = '\0';
  for (uint32_t t = 0; t <= CHIAKI_CONSOLE_INFO_MSG_TYPE_MAX; t++) {
    if (cur.ignored_by_type[t] == 0)
      continue;
    char label[CONSOLE_NUM_BUF_SIZE];
    snprintf(label, sizeof(label), "%u", t);
    console_append_type(types, sizeof(types), &types_len, &types_truncated, label,
                        cur.ignored_by_type[t]);
  }
  if (cur.ignored_other > 0)
    console_append_type(types, sizeof(types), &types_len, &types_truncated, "other",
                        cur.ignored_other);
  if (types_truncated) {
    // Make the cut visible: end the list with "..." (room is guaranteed: the buffer is far larger).
    snprintf(types + types_len, sizeof(types) - types_len, "...");
  } else if (types_len > 0) {
    types[types_len - 1] = '\0';  // drop the trailing comma
  }

  LOGD(
      "PIPE/CONSOLE quality_win=%u quality_total=%u rtt_msgs_win=%u rtt_range_n=%u rtt=%s "
      "rtt_min=%s rtt_max=%s target_bitrate=%s upstream_bitrate=%s upstream_loss=%s loss=%s "
      "ignored_win=%u ignored_total=%u types=[%s] ts_win=%u ts_total=%u ts_gap_ms=%s",
      console_counter_delta(cur.quality_msgs, prev->quality_msgs), cur.quality_msgs, rtt_window_n,
      range_n, rtt_s, rtt_min_s, rtt_max_s, target_s, upstream_s, upstream_loss_s, loss_s,
      console_counter_delta(cur.ignored_msgs, prev->ignored_msgs), cur.ignored_msgs, types,
      console_counter_delta(cur.ts_msgs, prev->ts_msgs), cur.ts_msgs, ts_gap_s);

  // First periodic-timestamp message of the session, raw, once.
  if (!console_ts_printed && cur.first_ts_len > 0) {
    static const char hex_digits[] = "0123456789abcdef";
    uint32_t stored = cur.first_ts_len < CHIAKI_CONSOLE_INFO_FIRST_TS_CAP
                          ? cur.first_ts_len
                          : CHIAKI_CONSOLE_INFO_FIRST_TS_CAP;
    char hex[CONSOLE_TS_HEX_BUF_SIZE];
    for (uint32_t i = 0; i < stored; i++) {
      hex[i * 2] = hex_digits[cur.first_ts[i] >> 4];
      hex[i * 2 + 1] = hex_digits[cur.first_ts[i] & 0x0F];
    }
    hex[stored * 2] = '\0';
    if (stored < cur.first_ts_len)
      LOGD("PIPE/CONSOLE_TS first len=%u hex=%s truncated_to=%u", cur.first_ts_len, hex, stored);
    else
      LOGD("PIPE/CONSOLE_TS first len=%u hex=%s", cur.first_ts_len, hex);
    console_ts_printed = true;
  }

  console_prev = cur;
}

/* Writes the lower-case hex of n bytes into out (room for 2n+1 chars). */
static void sps_hex_encode(char *out, const uint8_t *bytes, uint32_t n) {
  static const char hex_digits[] = "0123456789abcdef";
  for (uint32_t i = 0; i < n; i++) {
    out[i * 2] = hex_digits[bytes[i] >> 4];
    out[i * 2 + 1] = hex_digits[bytes[i] & 0x0F];
  }
  out[n * 2] = '\0';
}

/* PIPE/SPS: reads the session's first SPS with sps_probe_parse() (bounded, no allocation) and
 * prints the fields the app does not read elsewhere, or says why it refused (`fail=`) and prints
 * the bytes so the line can be decoded by hand (docs/design/streaming.md section 9.5). UI thread
 * only. */
static void host_metrics_print_sps(const SpsProbeData *d) {
  uint32_t stored = d->first_len < SPS_PROBE_CAP ? d->first_len : SPS_PROBE_CAP;
  char hex[SPS_HEX_BUF_SIZE];
  char cut[SPS_CUT_BUF_SIZE] = "";
  sps_hex_encode(hex, d->first, stored);
  if (stored < d->first_len)
    snprintf(cut, sizeof(cut), " truncated_to=%u", stored);

  SpsFields f;
  SpsParseStatus status = sps_probe_parse(d->first, stored, d->first_len, &f);
  if (status != SPS_PARSE_OK) {
    LOGD("PIPE/SPS fail=%s sps_len=%u hex_cap=%u hex=%s%s", sps_probe_status_name(status),
         d->first_len, SPS_PROBE_CAP, hex, cut);
    return;
  }
  char restriction_s[SPS_OPT_BUF_SIZE];
  char reorder_s[SPS_OPT_BUF_SIZE];
  char dec_buffering_s[SPS_OPT_BUF_SIZE];
  console_format_u64(restriction_s, sizeof(restriction_s), f.vui,
                     f.bitstream_restriction ? 1u : 0u);
  console_format_u64(reorder_s, sizeof(reorder_s), f.bitstream_restriction, f.num_reorder_frames);
  console_format_u64(dec_buffering_s, sizeof(dec_buffering_s), f.bitstream_restriction,
                     f.max_dec_frame_buffering);
  LOGD(
      "PIPE/SPS sps_len=%u profile_idc=%u constraint_flags=0x%02x level_idc=%u poc_type=%u "
      "frame_mbs_only=%u max_num_ref_frames=%u vui=%d bitstream_restriction=%s "
      "num_reorder_frames=%s max_dec_frame_buffering=%s hex_cap=%u hex=%s",
      d->first_len, (unsigned)f.profile_idc, (unsigned)f.constraint_flags, (unsigned)f.level_idc,
      f.poc_type, f.frame_mbs_only, f.max_num_ref_frames, f.vui ? 1 : 0, restriction_s, reorder_s,
      dec_buffering_s, SPS_PROBE_CAP, hex);
}

/* PIPE/SPS (once per session) and PIPE/SPS_REPEAT (when the counts moved, at most once per
 * window). Copies the receive thread's record only when it changed. Not behind show_latency: it is
 * one line per session and has to be in every testing log. UI thread only. */
static void host_metrics_report_sps(uint64_t now_us) {
  uint32_t version = sps_probe_version();
  if (version == sps_seen_version || (version & 1))
    return;
  SpsProbeData d;
  if (!sps_probe_snapshot(&d, SPS_READ_ATTEMPTS))
    return; /* overlapped a write; look again on the next tick */
  if (d.first_len == 0) {
    sps_seen_version = version;
    return;
  }
  if (!sps_line_printed) {
    host_metrics_print_sps(&d);
    sps_line_printed = true;
  }
  if (d.seen_again != sps_repeat_printed_seen || d.differed != sps_repeat_printed_differed) {
    if (now_us - sps_repeat_last_us < SPS_REPEAT_MIN_INTERVAL_US)
      return; /* too soon after the last repeat line; the change is still pending */
    LOGD("PIPE/SPS_REPEAT seen_again=%u differed=%u", d.seen_again, d.differed);
    sps_repeat_printed_seen = d.seen_again;
    sps_repeat_printed_differed = d.differed;
    sps_repeat_last_us = now_us;
  }
  sps_seen_version = version;
}

void host_metrics_update_latency(void) {
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

  // GH #258: detect a PS-button suspend/resume via a UI-loop clock jump and schedule an
  // immediate decoder resync instead of waiting for loss-detection heuristics to stumble
  // onto one 1-3.5s late. The anchor below is rewritten every tick regardless of outcome,
  // so exactly one detection fires per actual suspend -- self-throttling by construction,
  // no separate cooldown state needed.
  if (context.stream.suspend_tick_last_us != 0) {
    uint64_t suspend_gap_us = now_us - context.stream.suspend_tick_last_us;
    if (suspend_gap_us > SUSPEND_GAP_THRESHOLD_US) {
      LOGD("Suspend/resume detected — UI loop gap %llu ms",
           (unsigned long long)(suspend_gap_us / 1000ULL));
      context.stream.suspend_resync_next_us = now_us + SUSPEND_RESYNC_FIRST_DELAY_US;
      context.stream.suspend_resync_shots_left = SUSPEND_RESYNC_SHOTS;
      context.stream.suspend_fps_guard = true;
      context.stream.last_rtt_refresh_us = now_us;
      vitavideo_show_poor_net_indicator();
    }
  }
  context.stream.suspend_tick_last_us = now_us;

  // Fire scheduled post-resume resyncs one at a time. Checked here (fire time), not at
  // detection time, so a stop or fast-restart that starts mid-schedule cancels the
  // remaining shots cleanly instead of racing decoder teardown.
  if (context.stream.suspend_resync_shots_left > 0 &&
      now_us >= context.stream.suspend_resync_next_us) {
    if (context.stream.stop_requested || context.stream.fast_restart_active) {
      context.stream.suspend_resync_shots_left = 0;
    } else {
      host_request_decoder_resync("resume from suspend");
      context.stream.suspend_resync_shots_left--;
      if (context.stream.suspend_resync_shots_left > 0) {
        context.stream.suspend_resync_next_us = now_us + SUSPEND_RESYNC_FOLLOWUP_DELAY_US;
      }
    }
  }

  context.stream.measured_bitrate_mbps = bitrate_mbps;

  // D4: Windowed bitrate — 3-element ring buffer, time-based rate (not fps/frames).
  // Uses elapsed wall-clock µs per window so frame-bunching and byte-counter lags
  // do not inflate the estimate. Reads monotonic _total counters (never zeroed by
  // chiaki_stream_stats_reset) to survive per-second CONNECTIONQUALITY resets that
  // would otherwise trigger the reset-detection branch and stall the ring.
  // The prev_* pointers advance only when a window closes (>= BITRATE_WINDOW_MIN_US),
  // not on every call. Re-anchoring every call (the old behaviour) reset elapsed_us
  // to ~one UI frame so the gate was never reached and windowed_mbps stayed 0.00.
  {
    uint64_t total_bytes = stats->bytes_total;
    uint64_t total_frames = stats->frames_total;

    // Detect counter regression (e.g. new frame_processor on reconnect restarts
    // bytes_total at 0 while bitrate_prev_bytes still holds the old high value).
    // When the counter goes backwards, discard this interval and re-anchor.
    bool byte_counter_reset = (total_bytes < context.stream.bitrate_prev_bytes);
    if (byte_counter_reset) {
      context.stream.bitrate_prev_bytes = total_bytes;
      context.stream.bitrate_prev_frames = total_frames;
      context.stream.bitrate_prev_update_us = now_us;
      // Do not push to ring — treat as a gap.
    } else if (context.stream.bitrate_prev_update_us == 0) {
      /* First observation — anchor without pushing so elapsed accumulates
       * from a real timestamp on subsequent calls. */
      context.stream.bitrate_prev_bytes = total_bytes;
      context.stream.bitrate_prev_frames = total_frames;
      context.stream.bitrate_prev_update_us = now_us;
    } else {
      uint64_t delta_bytes = total_bytes - context.stream.bitrate_prev_bytes;
      uint32_t delta_frames = (uint32_t)(total_frames - context.stream.bitrate_prev_frames);
      uint64_t elapsed_us = now_us - context.stream.bitrate_prev_update_us;

      /* Accumulate wall-clock ACROSS calls until a full window (>= MIN_US) has
       * elapsed, then push one window and re-anchor. Re-anchoring on every call
       * (the previous behaviour) reset elapsed_us to ~one UI frame each time so
       * it never reached the threshold — windowed_mbps stayed 0.00. We only
       * re-anchor when a window closes; until then prev_* stay put. */
      if (delta_frames > 0 && elapsed_us >= BITRATE_WINDOW_MIN_US) {
        if (elapsed_us <= BITRATE_WINDOW_MAX_US) {
          uint8_t idx = context.stream.bitrate_window_index;
          context.stream.bitrate_window_delta_bytes[idx] = delta_bytes;
          context.stream.bitrate_window_elapsed_us[idx] = elapsed_us;
          context.stream.bitrate_window_index = (idx + 1) % 3;
          if (context.stream.bitrate_window_filled < 3)
            context.stream.bitrate_window_filled++;

          if (context.stream.bitrate_window_filled >= 2) {
            uint64_t sum_bytes = 0;
            uint64_t sum_elapsed_us = 0;
            for (uint8_t i = 0; i < context.stream.bitrate_window_filled; i++) {
              sum_bytes += context.stream.bitrate_window_delta_bytes[i];
              sum_elapsed_us += context.stream.bitrate_window_elapsed_us[i];
            }
            if (sum_elapsed_us > 0) {
              float window_mbps =
                  ((float)sum_bytes * 8.0f) / ((float)sum_elapsed_us / 1000000.0f) / 1000000.0f;
              if (window_mbps > 100.0f)
                window_mbps = 100.0f; /* sanity clamp: Vita Wi-Fi ceiling */
              context.stream.windowed_bitrate_mbps = window_mbps;
            }
          }
        }
        /* Re-anchor on every closed window (pushed OR discarded as too-long). */
        context.stream.bitrate_prev_bytes = total_bytes;
        context.stream.bitrate_prev_frames = total_frames;
        context.stream.bitrate_prev_update_us = now_us;
      }
      /* delta_frames == 0 or window still < MIN_US: keep accumulating, do NOT
       * advance prev_* — that is the whole point of the fix. */
    }
  }

  uint32_t effective_target_fps =
      context.stream.target_fps ? context.stream.target_fps : context.stream.negotiated_fps;
  uint32_t incoming_fps = context.stream.measured_incoming_fps;
  bool low_fps_window =
      effective_target_fps > 0 && incoming_fps > 0 && incoming_fps + 5 < effective_target_fps;
  // GH #258: mask the first post-resume window -- the collapsed fps during the resume
  // burst/decoder-resync drain is expected, not a real health signal, and must not bump
  // fps_under_target_windows or feed the recovery tiers below.
  if (context.stream.suspend_fps_guard)
    low_fps_window = false;
  bool av_diag_progressed =
      av_diag_missing_ref_count > context.stream.av_diag.logged_missing_ref_count ||
      av_diag_corrupt_burst_count > context.stream.av_diag.logged_corrupt_burst_count ||
      av_diag_fec_fail_count > context.stream.av_diag.logged_fec_fail_count ||
      av_diag_sendbuf_overflow_count > context.stream.av_diag.logged_sendbuf_overflow_count;
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

    // D6: Probe Wi-Fi RSSI once per second
    {
      SceNetCtlInfo rssi_info;
      int rssi_ret = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_PERCENTAGE, &rssi_info);
      context.stream.wifi_rssi = (rssi_ret >= 0) ? (int32_t)rssi_info.rssi_percentage : -1;
    }

    // Count low-fps health once per metrics window (about 1 second), not per frame.
    if (low_fps_window) {
      context.stream.fps_under_target_windows++;
      if (context.stream.post_reconnect_window_until_us &&
          now_us <= context.stream.post_reconnect_window_until_us) {
        context.stream.post_reconnect_low_fps_windows++;
      }
    }
    // GH #258: the guard only needs to suppress the first post-resume 1s window; clear it
    // once that window has closed so subsequent windows are evaluated normally.
    context.stream.suspend_fps_guard = false;

    // DEAD-CODE: this call always returns immediately at the callee's entry
    // guard clause -- post_reconnect_window_until_us is never set to a
    // nonzero deadline anywhere in this codebase. See the DEAD-CODE comment
    // on host_recovery_handle_post_reconnect_degraded_mode() (host_recovery.c)
    // for the full citation chain.
    host_recovery_handle_post_reconnect_degraded_mode(av_diag_progressed, incoming_fps,
                                                      effective_target_fps, low_fps_window, now_us);
    // Keep diagnostics passive here; stability path avoids restart escalation.

    // Latency investigation (item 1): reduce this window's true end-to-end latency
    // samples (written by vita_video_render_latest_frame() in video.c, see the
    // clock-domain comment there) to p50/p95/max and reset the ring for the next window.
    // Runs unconditionally here -- not gated by show_latency below -- so stale samples
    // from a debug-overlay-off session never linger; only the LOGD print is gated.
    //
    // Code review fix: latency_window_dropped_count is now per-window (was
    // session-cumulative), snapshotted into latency_dropped_n before reset so the printed
    // line can show whether THIS window's ring actually wrapped -- the session-cumulative
    // view is preserved separately in latency_dropped_total_count.
    {
      uint32_t n = context.stream.latency_window_sample_count;
      if (n > 0) {
        sort_latency_samples(context.stream.latency_window_samples_ms, n);
        context.stream.latency_p50_ms =
            context.stream.latency_window_samples_ms[percentile_index(n, LATENCY_PERCENTILE_P50)];
        context.stream.latency_p95_ms =
            context.stream.latency_window_samples_ms[percentile_index(n, LATENCY_PERCENTILE_P95)];
        context.stream.latency_max_ms = context.stream.latency_window_samples_ms[n - 1];
      } else {
        context.stream.latency_p50_ms = 0;
        context.stream.latency_p95_ms = 0;
        context.stream.latency_max_ms = 0;
      }
      context.stream.latency_sample_n = n;
      context.stream.latency_dropped_n = context.stream.latency_window_dropped_count;
      context.stream.latency_dropped_total_count += context.stream.latency_window_dropped_count;
      context.stream.latency_window_dropped_count = 0;
      context.stream.latency_window_sample_count = 0;
      context.stream.latency_window_write_idx = 0;
    }

    // PIPE/DISPLAY: reduce this window's decode-done -> on-screen split samples (written by
    // vita_video_render_latest_frame() in video.c, see the field comment in stream_state.h)
    // to p50/p95 per stage and reset the four rings for the next window. Runs
    // unconditionally here, same as the PIPE/LATENCY reduce above, so stale samples never
    // linger across a show_latency toggle; only the LOGD print further down is gated.
    {
      uint32_t discard_n, discard_dropped;
      uint32_t snapshot_n, snapshot_dropped;

      reduce_display_stage_ring(
          context.stream.display_pickup_samples_us, &context.stream.display_pickup_sample_count,
          &context.stream.display_pickup_write_idx, &context.stream.display_pickup_dropped_count,
          &context.stream.display_pickup_p50_us, &context.stream.display_pickup_p95_us, &discard_n,
          &discard_dropped);
      reduce_display_stage_ring(
          context.stream.display_snapshot_samples_us, &context.stream.display_snapshot_sample_count,
          &context.stream.display_snapshot_write_idx,
          &context.stream.display_snapshot_dropped_count, &context.stream.display_snapshot_p50_us,
          &context.stream.display_snapshot_p95_us, &snapshot_n, &snapshot_dropped);
      reduce_display_stage_ring(
          context.stream.display_draw_samples_us, &context.stream.display_draw_sample_count,
          &context.stream.display_draw_write_idx, &context.stream.display_draw_dropped_count,
          &context.stream.display_draw_p50_us, &context.stream.display_draw_p95_us, &discard_n,
          &discard_dropped);
      reduce_display_stage_ring(
          context.stream.display_swap_samples_us, &context.stream.display_swap_sample_count,
          &context.stream.display_swap_write_idx, &context.stream.display_swap_dropped_count,
          &context.stream.display_swap_p50_us, &context.stream.display_swap_p95_us, &discard_n,
          &discard_dropped);
      (void)discard_n;
      (void)discard_dropped;

      // snapshot/draw/swap rings wrap in lockstep -- all three are always written
      // together on the non-paced path (see the display_pickup_* field comment in
      // stream_state.h) -- so snapshot's n/dropped represents all three; pickup's own
      // n/dropped are internal ring bookkeeping only and are not separately published
      // (pickup's true sample count for the window is display_sample_n + display_paced_n).
      context.stream.display_sample_n = snapshot_n;
      context.stream.display_dropped_n = snapshot_dropped;
      context.stream.display_paced_n = context.stream.display_paced_count;
      context.stream.display_paced_count = 0;
    }

    // PIPE/PRESS: publish this window's press-to-picture results (see PressWindow in
    // stream_state.h) and clear the accumulator. Recv-thread totals are running counters, so
    // the window's figures are deltas against what the previous close saw. Unconditional like
    // the reducers above; only the LOGD print further down is gated by show_latency.
    {
      PressCounters counters_now;
      vita_video_press_meter_totals(&counters_now);
      uint32_t press_count = 0;
      uint64_t press_time_us;
      if (!chiaki_feedback_sender_read_press_stamp(&stream_connection->feedback_sender,
                                                   &press_count, &press_time_us))
        press_count = context.stream.press_seen_presses;  // no consistent read: no news this window

      PressWindow *pub = &context.stream.press_pub;
      *pub = context.stream.press_acc;
      pub->presses = press_count >= context.stream.press_seen_presses
                         ? press_count - context.stream.press_seen_presses
                         : press_count;
      press_counters_delta(&counters_now, &context.stream.press_seen, &pub->counters);
      context.stream.press_seen_presses = press_count;
      context.stream.press_seen = counters_now;
      context.stream.press_total_presses = press_count;
      memset(&context.stream.press_acc, 0, sizeof(context.stream.press_acc));
    }

    // PIPE/INPUT: reduce this window's controller-poll-origin -> feedback-sender-wire
    // latency samples (ring lives in ChiakiFeedbackSender, lib/include/chiaki/feedbacksender.h
    // -- see the field comment there and in stream_state.h for why it's not duplicated
    // vita-side) to p50/p95/max and reset the ring for the next window. Non-blocking
    // outer/inner trylock, same lock order used everywhere else in this codebase
    // (feedback_sender_mutex outer, the feedback sender's own state_mutex inner -- see
    // session.c:350-356/chiaki_session_set_controller_state()); if either is busy or the
    // feedback sender isn't active, skip this window's publish and leave the previous
    // values in place (same graceful-degradation style as diag_snapshot_stale above).
    //
    // Snapshot-then-reduce (mirrors the diag_mutex trylock block above): the raw samples
    // are memcpy'd into a stack-local array and the counts reset while state_mutex is
    // held, then both mutexes are released BEFORE sort_latency_samples() runs. Sorting is
    // O(n^2) over up to FEEDBACK_SENDER_INPUT_LATENCY_SAMPLE_CAP elements and must never
    // run while holding state_mutex: that mutex is also needed by the priority-65 feedback
    // sender thread to send and by chiaki_feedback_sender_set_controller_state_ts() to
    // enqueue new samples, so sorting under lock would stall the real-time input path
    // once per second.
    {
      uint32_t in_n = 0;
      uint32_t dropped_n = 0;
      bool have_snapshot = false;
      uint32_t local_samples[FEEDBACK_SENDER_INPUT_LATENCY_SAMPLE_CAP];
      if (chiaki_mutex_trylock(&stream_connection->feedback_sender_mutex) == CHIAKI_ERR_SUCCESS) {
        if (stream_connection->feedback_sender_active) {
          ChiakiFeedbackSender *feedback_sender = &stream_connection->feedback_sender;
          if (chiaki_mutex_trylock(&feedback_sender->state_mutex) == CHIAKI_ERR_SUCCESS) {
            in_n = feedback_sender->input_latency_sample_count;
            if (in_n > FEEDBACK_SENDER_INPUT_LATENCY_SAMPLE_CAP)
              in_n = FEEDBACK_SENDER_INPUT_LATENCY_SAMPLE_CAP;  // defensive: local_samples is sized
                                                                // to this cap
            if (in_n > 0)
              memcpy(local_samples, feedback_sender->input_latency_samples_us,
                     in_n * sizeof(uint32_t));
            dropped_n = feedback_sender->input_latency_dropped_count;
            feedback_sender->input_latency_sample_count = 0;
            feedback_sender->input_latency_dropped_count = 0;
            chiaki_mutex_unlock(&feedback_sender->state_mutex);
            have_snapshot = true;
          }
        }
        chiaki_mutex_unlock(&stream_connection->feedback_sender_mutex);
      }
      if (have_snapshot) {
        if (in_n > 0) {
          sort_latency_samples(local_samples, in_n);
          context.stream.input_latency_p50_us =
              local_samples[percentile_index(in_n, LATENCY_PERCENTILE_P50)];
          context.stream.input_latency_p95_us =
              local_samples[percentile_index(in_n, LATENCY_PERCENTILE_P95)];
          context.stream.input_latency_max_us = local_samples[in_n - 1];
        } else {
          context.stream.input_latency_p50_us = 0;
          context.stream.input_latency_p95_us = 0;
          context.stream.input_latency_max_us = 0;
        }
        context.stream.input_latency_sample_n = in_n;
        context.stream.input_latency_dropped_n = dropped_n;
      }
    }
  }

  host_metrics_report_sps(now_us);

  if (!context.config.show_latency)
    return;

  static const uint64_t LOG_INTERVAL_US = 1000000;
  // Code review finding: this was previously a function-local `static`, which
  // host_metrics_reset_stream() had no way to clear -- a session reset or a
  // show_latency toggle mid-stream could print one stale-anchored line using a timestamp
  // from a previous session. Living in context.stream lets the reset function clear it
  // alongside every other window anchor in this file (see below).
  if (now_us - context.stream.latency_log_last_us >= LOG_INTERVAL_US) {
    float target_mbps = context.stream.session.connect_info.video_profile.bitrate / 1000.0f;
    // Item 4: this used to print (base RTT + control-channel jitter) labeled as "RTT",
    // which is neither a real RTT (base is a one-shot Senkusha handshake value, never
    // updated mid-stream -- lib/src/senkusha.c, lib/src/session.c:685) nor real jitter
    // (the EWMA is control-channel packet cadence -- lib/src/takion.c:~2020-2050 -- which
    // video/audio AV traffic never touches). Relabeled honestly below; the UI-facing
    // "Latency" overlay value (context.stream.measured_rtt_ms, video_overlay.c) is left
    // untouched -- out of scope for this pure-instrumentation change. See the new
    // PIPE/LATENCY line for the genuine measured end-to-end latency and video-packet
    // jitter this investigation added.
    LOGD(
        "Latency metrics — target %.2f Mbps, measured %.2f Mbps, senkusha_rtt %u ms "
        "(one-shot handshake, never updated mid-stream), ctrl_cadence_jitter %llu us "
        "(control-channel EWMA, NOT real network jitter)",
        target_mbps, context.stream.windowed_bitrate_mbps,
        (uint32_t)(context.stream.session.rtt_us / 1000),
        (unsigned long long)stream_connection->takion.jitter_stats.jitter_us);
    // Code review fix: this line used to read latency_window_dropped_count directly, but
    // that field is reset to 0 by the (unconditional) window-close reduce block above on
    // every call, *before* this show_latency-gated print runs -- so it would always print
    // 0. Print the published per-window snapshot (dropped_win) and the session-cumulative
    // twin (dropped_total) instead.
    // Code review fix: decode_q_occ_avg/audio_q_occ_avg are fixed-point, scaled by
    // OCC_AVG_FIXED_POINT_SCALE (see stream_state.h) -- a 4-slot queue's whole interesting
    // range is 0.00-4.00 frames and plain integer printing would floor away the sub-frame
    // persistent depth shift this PR exists to detect. Formatted here as X.XX; *_max stays
    // a plain integer (genuinely discrete depth counts).
    LOGD(
        "PIPE/LATENCY p50_ms=%u p95_ms=%u max_ms=%u n=%u dropped_win=%u dropped_total=%u "
        "decode_q_avg=%u.%02u decode_q_max=%u audio_q_avg=%u.%02u audio_q_max=%u "
        "video_jitter_us=%llu",
        context.stream.latency_p50_ms, context.stream.latency_p95_ms, context.stream.latency_max_ms,
        context.stream.latency_sample_n, context.stream.latency_dropped_n,
        context.stream.latency_dropped_total_count,
        context.stream.decode_q_occ_avg / OCC_AVG_FIXED_POINT_SCALE,
        context.stream.decode_q_occ_avg % OCC_AVG_FIXED_POINT_SCALE,
        context.stream.decode_q_occ_max, context.stream.audio_q_occ_avg / OCC_AVG_FIXED_POINT_SCALE,
        context.stream.audio_q_occ_avg % OCC_AVG_FIXED_POINT_SCALE, context.stream.audio_q_occ_max,
        (unsigned long long)stream_connection->takion.video_jitter_stats.jitter_us);
    LOGD("PIPE/INPUT p50_us=%u p95_us=%u max_us=%u n=%u dropped=%u",
         context.stream.input_latency_p50_us, context.stream.input_latency_p95_us,
         context.stream.input_latency_max_us, context.stream.input_latency_sample_n,
         context.stream.input_latency_dropped_n);
    // PIPE/DISPLAY: the decode-done -> on-screen split this whole instrumentation pass
    // exists to produce -- see the PIPE/DISPLAY field comment in stream_state.h for the
    // four stage definitions and how paced-drop frames (n vs. paced) are handled.
    LOGD(
        "PIPE/DISPLAY pickup_p50_us=%u pickup_p95_us=%u snapshot_p50_us=%u snapshot_p95_us=%u "
        "draw_p50_us=%u draw_p95_us=%u swap_p50_us=%u swap_p95_us=%u n=%u dropped=%u paced=%u",
        context.stream.display_pickup_p50_us, context.stream.display_pickup_p95_us,
        context.stream.display_snapshot_p50_us, context.stream.display_snapshot_p95_us,
        context.stream.display_draw_p50_us, context.stream.display_draw_p95_us,
        context.stream.display_swap_p50_us, context.stream.display_swap_p95_us,
        context.stream.display_sample_n, context.stream.display_dropped_n,
        context.stream.display_paced_n);
    // PIPE/PRESENT (#416): how evenly and in what order the UI thread presented pictures this
    // window, then reset. Counts buffer swaps, not panel refreshes. See streaming.md section 9.10.
    {
      PresentWindow pr;
      vita_video_present_take(&pr);
      LOGD(
          "PIPE/PRESENT passes=%u misses=%u shown=%u back=%u same=%u back_max=%u skip_frames=%u "
          "gaps=%u,%u,%u,%u,%u,%u gap_max_us=%u",
          pr.passes, pr.misses, pr.shown, pr.back, pr.same, pr.back_max, pr.skip_frames, pr.gaps[0],
          pr.gaps[1], pr.gaps[2], pr.gaps[3], pr.gaps[4], pr.gaps[5], pr.gap_max_us);
    }
    // PIPE/UITHREAD (#416): the UI thread as the system reports it, sampled on the UI thread
    // (this function), printed at the first window and when priority, affinity or cpu moved.
    {
      SceKernelThreadInfo ti;
      memset(&ti, 0, sizeof(ti));
      ti.size = sizeof(ti);
      int info_rc = sceKernelGetThreadInfo(sceKernelGetThreadId(), &ti);
      int32_t prio = info_rc == 0 ? (int32_t)ti.currentPriority : 0;
      int32_t affinity = info_rc == 0 ? (int32_t)ti.currentCpuAffinityMask : 0;
      int32_t cpu = info_rc == 0 ? (int32_t)ti.currentCpuId : 0;
      if (!ui_thread_printed.printed || ui_thread_printed.prio != prio ||
          ui_thread_printed.affinity != affinity || ui_thread_printed.cpu != cpu) {
        ui_thread_printed.printed = true;
        ui_thread_printed.prio = prio;
        ui_thread_printed.affinity = affinity;
        ui_thread_printed.cpu = cpu;
        LOGD("PIPE/UITHREAD prio=%d affinity=0x%x cpu=%d last_cpu=%d info_rc=%d", prio,
             (unsigned int)affinity, cpu, info_rc == 0 ? (int)ti.lastExecutedCpuId : 0, info_rc);
      }
    }
    // PIPE/PRESS: press-to-picture meter, one summary line per window, printed even when empty,
    // followed by one PIPE/PRESS_SAMPLE line per completed sample (up to PRESS_WINDOW_SAMPLE_CAP;
    // the rest are counted in sample_overflow). The *_us stages are microseconds since the press
    // was sent; first_us is resolved to 1 ms (it comes from the lib's millisecond stamp). Printed
    // here, on the UI thread at window close, never from the receive or decode thread. See
    // section 9.1 of docs/design/streaming.md for the fields and for how to pick trustworthy
    // samples (waiting=1 and order_ok=1; also inburst=0 for the strictest set).
    {
      const PressWindow *pw = &context.stream.press_pub;
      LOGD(
          "PIPE/PRESS presses=%u n=%u amb=%u order_err=%u expired=%u full=%u inburst=%u unseen=%u "
          "unmatched=%u lost=%u sample_overflow=%u presses_total=%u samples_total=%u",
          pw->presses, pw->samples, pw->ambiguous, pw->order_err, pw->counters.expired,
          pw->counters.full, pw->counters.inburst, pw->counters.unseen, pw->counters.unmatched,
          pw->counters.lost, pw->sample_overflow, context.stream.press_total_presses,
          context.stream.press_total_samples);
      uint32_t listed = pw->samples - pw->sample_overflow;
      for (uint32_t i = 0; i < listed; i++) {
        const PressSample *ps = &pw->list[i];
        LOGD(
            "PIPE/PRESS_SAMPLE press=%u first_us=%d assembled_us=%d decoded_us=%d shown_us=%d "
            "frame_bytes=%u quiet_bytes=%u waiting=%u inburst=%u order_ok=%u",
            ps->press_count, ps->first_us, ps->assembled_us, ps->decoded_us, ps->shown_us,
            ps->frame_size, ps->quiet_ref, ps->waiting, ps->in_burst ? 1u : 0u,
            ps->order_ok ? 1u : 0u);
      }
    }
    // PIPE/DECHOLD: decoder hold (frames fed minus pictures handed back) and the per-picture
    // lag / hold time from the timestamp read-back. Copied under the decode thread's seqlock;
    // when no consistent copy is won in a few tries the previous window's values are printed.
    // lag_avg is x100 fixed point like the occupancy lines. See streaming.md section 9.2.
    {
      DecHoldWindow dh;
      bool dh_ok = false;
      for (int attempt = 0; attempt < DECHOLD_READ_ATTEMPTS && !dh_ok; attempt++) {
        uint32_t lock_before = context.stream.dechold_lock;
        if (lock_before & 1)
          continue;
        __sync_synchronize();
        dh = context.stream.dechold_pub;
        __sync_synchronize();
        dh_ok = context.stream.dechold_lock == lock_before;
      }
      if (dh_ok)
        dechold_last = dh;
      const DecHoldWindow *d = &dechold_last;
      LOGD(
          "PIPE/DECHOLD fed=%u handed=%u hold=%u hold_min=%u hold_max=%u lag_min=%u "
          "lag_avg=%u.%02u lag_max=%u hold_us_avg=%u hold_us_max=%u rb_good=%u rb_bad=%u "
          "rb_stale=%u",
          d->fed, d->handed, d->hold, d->hold_min, d->hold_max, d->lag_min,
          d->lag_avg_x100 / OCC_AVG_FIXED_POINT_SCALE, d->lag_avg_x100 % OCC_AVG_FIXED_POINT_SCALE,
          d->lag_max, d->hold_us_avg, d->hold_us_max, d->good, d->bad, d->stale);
    }
    // PIPE/RECVBUF: packets and bytes drained per receive wake-up and the receive buffer size,
    // from the Takion receive thread's last closed window (seqlock copy as above). The queue-length
    // part was left out: see streaming.md section 9.3. drained_avg is x100 fixed point.
    {
      ChiakiTakion *takion = &stream_connection->takion;
      ChiakiTakionRecvbufWindow rb;
      bool rb_ok = false;
      for (int attempt = 0; attempt < RECVBUF_READ_ATTEMPTS && !rb_ok; attempt++) {
        uint32_t lock_before = takion->recvbuf_lock;
        if (lock_before & 1)
          continue;
        __sync_synchronize();
        rb = takion->recvbuf_pub;
        __sync_synchronize();
        rb_ok = takion->recvbuf_lock == lock_before;
      }
      if (rb_ok)
        recvbuf_last = rb;
      const ChiakiTakionRecvbufWindow *r = &recvbuf_last;
      LOGD(
          "PIPE/RECVBUF wakeups=%u drained_avg=%u.%02u drained_max=%u drained_bytes_avg=%u "
          "drained_bytes_max=%u drain_cap=%u rcvbuf_req=%u rcvbuf_actual=%u packets=%u bytes=%u "
          "drain_empty=%u drain_capped=%u block_us=%u poll_us=%u poll_max_us=%u work_us=%u "
          "wall_us=%u busy_pct=%u.%u",
          r->wakeups, r->drained_avg_x100 / CHIAKI_TAKION_RECVBUF_AVG_SCALE,
          r->drained_avg_x100 % CHIAKI_TAKION_RECVBUF_AVG_SCALE, r->drained_max,
          r->drained_bytes_avg, r->drained_bytes_max, r->drain_cap, r->rcvbuf_requested,
          r->rcvbuf_actual, r->packets, r->bytes, r->drain_empty, r->drain_capped, r->block_us,
          r->poll_us, r->poll_max_us, r->work_us, r->wall_us,
          r->busy_permille / RECVBUF_BUSY_PERMILLE_PER_PCT,
          r->busy_permille % RECVBUF_BUSY_PERMILLE_PER_PCT);
      // PIPE/AUDIOOUT: the audio output thread's last published window, same scheme.
      AudioOutWindow ao;
      bool ao_ok = false;
      for (int attempt = 0; attempt < AUDIOOUT_READ_ATTEMPTS && !ao_ok; attempt++) {
        uint32_t lock_before = context.stream.audioout_lock;
        if (lock_before & 1)
          continue;
        __sync_synchronize();
        ao = context.stream.audioout_pub;
        __sync_synchronize();
        ao_ok = context.stream.audioout_lock == lock_before;
      }
      if (ao_ok)
        audioout_last = ao;
      recv_probe_report(r, &audioout_last);
    }
    host_metrics_report_console(stream_connection);
    LOGD(
        "PIPE/FPS gen=%u reconnect_gen=%u incoming=%u target=%u low_windows=%u "
        "post_reconnect_low=%u post_window_remaining_ms=%llu decode_avg_ms=%.1f decode_max_ms=%.1f "
        "windowed_mbps=%.2f overwrites=%u freeze=%u rssi=%d display_fps=%u stuck_streak=%u "
        "stuck_used=%d cascade_streak=%u cascade_used=%d decode_q_drops=%u",
        context.stream.session_generation, context.stream.reconnect_generation, incoming_fps,
        effective_target_fps, context.stream.fps_under_target_windows,
        context.stream.post_reconnect_low_fps_windows,
        context.stream.post_reconnect_window_until_us &&
                now_us < context.stream.post_reconnect_window_until_us
            ? (unsigned long long)((context.stream.post_reconnect_window_until_us - now_us) /
                                   1000ULL)
            : 0ULL,
        context.stream.decode_avg_us / 1000.0f, context.stream.decode_max_us / 1000.0f,
        context.stream.windowed_bitrate_mbps, context.stream.frame_overwrite_count,
        context.stream.freeze_engaged_count, context.stream.wifi_rssi, context.stream.display_fps,
        context.stream.stuck_bitrate_low_fps_streak, (int)context.stream.stuck_bitrate_restart_used,
        context.stream.cascade_alarm_streak, (int)context.stream.cascade_alarm_restart_used,
        vita_video_decode_queue_drops());
    context.stream.latency_log_last_us = now_us;
  }

  if (context.stream.takion_drop_events != context.stream.logged_drop_events) {
    uint32_t delta = context.stream.takion_drop_events - context.stream.logged_drop_events;
    LOGD("Packet loss — Takion dropped %u packet(s), total %u", delta,
         context.stream.takion_drop_packets);
    context.stream.logged_drop_events = context.stream.takion_drop_events;
    host_handle_takion_overflow();
  }

  bool av_diag_changed = av_diag_progressed;
  if (av_diag_changed || (context.stream.av_diag.last_log_us == 0 ||
                          now_us - context.stream.av_diag.last_log_us >= AV_DIAG_LOG_INTERVAL_US)) {
    LOGD(
        "AV diag — missing_ref=%u, corrupt_bursts=%u, fec_fail=%u, sendbuf_overflow=%u, "
        "diag_trylock_failures=%u, stale_diag_streak=%u, last_corrupt=%u-%u",
        context.stream.av_diag.missing_ref_count, context.stream.av_diag.corrupt_burst_count,
        context.stream.av_diag.fec_fail_count, context.stream.av_diag.sendbuf_overflow_count,
        av_diag_trylock_failures, context.stream.av_diag_stale_snapshot_streak,
        context.stream.av_diag.last_corrupt_start, context.stream.av_diag.last_corrupt_end);
    context.stream.av_diag.logged_missing_ref_count = context.stream.av_diag.missing_ref_count;
    context.stream.av_diag.logged_corrupt_burst_count = context.stream.av_diag.corrupt_burst_count;
    context.stream.av_diag.logged_fec_fail_count = context.stream.av_diag.fec_fail_count;
    context.stream.av_diag.logged_sendbuf_overflow_count =
        context.stream.av_diag.sendbuf_overflow_count;
    context.stream.av_diag.last_log_us = now_us;
  }
}
