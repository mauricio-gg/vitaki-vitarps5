#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "chiaki/reorderqueue.h"
#include "chiaki/packetstats.h"
#include "../lib/src/videoreceiver_gap.h"

static void test_reorder_find_first_set_after_skip_and_drop(void) {
  ChiakiReorderQueue queue;
  assert(chiaki_reorder_queue_init_16(&queue, 4, (ChiakiSeqNum16)100) == CHIAKI_ERR_SUCCESS);

  uint32_t marker_102 = 102;
  uint32_t marker_104 = 104;
  chiaki_reorder_queue_push(&queue, (uint16_t)102, &marker_102);
  chiaki_reorder_queue_push(&queue, (uint16_t)104, &marker_104);

  uint64_t idx = 0;
  uint64_t seq = 0;
  void *user = NULL;
  assert(chiaki_reorder_queue_find_first_set(&queue, &idx, &seq, &user));
  assert(idx == 2);
  assert((uint16_t)seq == 102);
  assert(user == &marker_102);

  chiaki_reorder_queue_skip_gap(&queue);
  assert(chiaki_reorder_queue_find_first_set(&queue, &idx, &seq, &user));
  assert(idx == 1);
  assert((uint16_t)seq == 102);

  chiaki_reorder_queue_drop(&queue, 1);
  assert(chiaki_reorder_queue_find_first_set(&queue, &idx, &seq, &user));
  assert(idx == 3);
  assert((uint16_t)seq == 104);
  assert(user == &marker_104);

  chiaki_reorder_queue_fini(&queue);
}

static void test_reorder_wraparound_progression(void) {
  ChiakiReorderQueue queue;
  assert(chiaki_reorder_queue_init_16(&queue, 4, (ChiakiSeqNum16)65534) == CHIAKI_ERR_SUCCESS);

  uint32_t marker_65535 = 65535;
  uint32_t marker_0 = 0;
  chiaki_reorder_queue_push(&queue, (uint16_t)0, &marker_0);
  chiaki_reorder_queue_push(&queue, (uint16_t)65535, &marker_65535);

  uint64_t idx = 0;
  uint64_t seq = 0;
  void *user = NULL;
  assert(chiaki_reorder_queue_find_first_set(&queue, &idx, &seq, &user));
  assert(idx == 1);
  assert((uint16_t)seq == 65535);

  chiaki_reorder_queue_skip_gap(&queue);
  assert(chiaki_reorder_queue_pull(&queue, &seq, &user));
  assert((uint16_t)seq == 65535);
  assert(user == &marker_65535);

  assert(chiaki_reorder_queue_pull(&queue, &seq, &user));
  assert((uint16_t)seq == 0);
  assert(user == &marker_0);

  chiaki_reorder_queue_fini(&queue);
}

static void test_reorder_skip_clears_entry_slot(void) {
  ChiakiReorderQueue queue;
  assert(chiaki_reorder_queue_init_16(&queue, 4, (ChiakiSeqNum16)5) == CHIAKI_ERR_SUCCESS);

  uint32_t marker = 5;
  chiaki_reorder_queue_push(&queue, (uint16_t)5, &marker);
  uint64_t slot = 5U & (((uint64_t)1U << queue.size_exp) - 1U);
  assert(queue.queue[slot].set);
  assert(queue.queue[slot].user == &marker);

  chiaki_reorder_queue_skip_gap(&queue);
  assert(!queue.queue[slot].set);
  assert(queue.queue[slot].user == NULL);

  chiaki_reorder_queue_fini(&queue);
}

static void test_gap_update_set_and_extend(void) {
  ChiakiVideoGapReportState state = {0};
  ChiakiSeqNum16 flush_start = 0;
  ChiakiSeqNum16 flush_end = 0;

  ChiakiVideoGapUpdateAction a = chiaki_video_gap_report_update(
      &state, (ChiakiSeqNum16)10, (ChiakiSeqNum16)12, 100, 12, &flush_start, &flush_end);
  assert(a == CHIAKI_VIDEO_GAP_UPDATE_SET_PENDING);
  assert(state.pending);
  assert(state.start == 10);
  assert(state.end == 12);
  assert(state.deadline_ms == 112);

  a = chiaki_video_gap_report_update(
      &state, (ChiakiSeqNum16)10, (ChiakiSeqNum16)14, 101, 12, &flush_start, &flush_end);
  assert(a == CHIAKI_VIDEO_GAP_UPDATE_EXTEND_PENDING);
  assert(state.start == 10);
  assert(state.end == 14);
}

static void test_gap_update_flush_previous_on_new_range(void) {
  ChiakiVideoGapReportState state = {
      .pending = true,
      .start = (ChiakiSeqNum16)10,
      .end = (ChiakiSeqNum16)12,
      .deadline_ms = 500,
  };
  ChiakiSeqNum16 flush_start = 0;
  ChiakiSeqNum16 flush_end = 0;

  ChiakiVideoGapUpdateAction a = chiaki_video_gap_report_update(
      &state, (ChiakiSeqNum16)20, (ChiakiSeqNum16)24, 200, 12, &flush_start, &flush_end);
  assert(a == CHIAKI_VIDEO_GAP_UPDATE_FLUSH_PREVIOUS);
  assert(flush_start == 10);
  assert(flush_end == 12);
  assert(state.pending);
  assert(state.start == 20);
  assert(state.end == 24);
  assert(state.deadline_ms == 212);
}

static void test_gap_update_none_for_stale_end_and_null_state(void) {
  ChiakiVideoGapReportState state = {
      .pending = true,
      .start = (ChiakiSeqNum16)40,
      .end = (ChiakiSeqNum16)44,
      .deadline_ms = 900,
  };

  // Older/equal ends must not shrink or flush the pending range.
  ChiakiVideoGapUpdateAction a = chiaki_video_gap_report_update(
      &state, (ChiakiSeqNum16)40, (ChiakiSeqNum16)42, 500, 12, NULL, NULL);
  assert(a == CHIAKI_VIDEO_GAP_UPDATE_NONE);
  assert(state.pending);
  assert(state.start == 40);
  assert(state.end == 44);
  assert(state.deadline_ms == 900);

  // Null state is a safe no-op.
  a = chiaki_video_gap_report_update(
      NULL, (ChiakiSeqNum16)1, (ChiakiSeqNum16)2, 0, 0, NULL, NULL);
  assert(a == CHIAKI_VIDEO_GAP_UPDATE_NONE);
}

static void test_gap_update_wraparound_extend(void) {
  ChiakiVideoGapReportState state = {0};

  ChiakiVideoGapUpdateAction a = chiaki_video_gap_report_update(
      &state, (ChiakiSeqNum16)65534, (ChiakiSeqNum16)65535, 1000, 12, NULL, NULL);
  assert(a == CHIAKI_VIDEO_GAP_UPDATE_SET_PENDING);
  assert(state.pending);
  assert(state.start == (ChiakiSeqNum16)65534);
  assert(state.end == (ChiakiSeqNum16)65535);

  // Across wraparound, 0 is newer than 65535 and should extend.
  a = chiaki_video_gap_report_update(
      &state, (ChiakiSeqNum16)65534, (ChiakiSeqNum16)0, 1001, 12, NULL, NULL);
  assert(a == CHIAKI_VIDEO_GAP_UPDATE_EXTEND_PENDING);
  assert(state.end == (ChiakiSeqNum16)0);
}

// chiaki_video_corrupt_report_classify() backs the corrupt-frame report
// cooldown in lib/src/videoreceiver.c (report_corrupt_frame_range()). It
// decides whether a candidate report [start, end] should be EMITted, is
// already OBSOLETE (no new info beyond the last report, or a pathologically
// large span), or must be DEFERred (rate-limited, caller must retry later).

static void test_corrupt_report_classify_new_burst_always_emits(void) {
  // A different start must never be rate-limited, even if it arrives
  // immediately after an unrelated report -- it's what drives PS5-side
  // recovery for a fresh gap.
  ChiakiVideoCorruptReportDisposition d = chiaki_video_corrupt_report_classify(
      (ChiakiSeqNum16)10, (ChiakiSeqNum16)12, 1000,
      (ChiakiSeqNum16)50, (ChiakiSeqNum16)52, 1001,
      false, 500, 32, 4096);
  assert(d == CHIAKI_VIDEO_CORRUPT_REPORT_EMIT);
}

static void test_corrupt_report_classify_obsolete_when_fully_covered(void) {
  // Same start, new end already covered by the last reported end -> nothing
  // new to send.
  ChiakiVideoCorruptReportDisposition d = chiaki_video_corrupt_report_classify(
      (ChiakiSeqNum16)10, (ChiakiSeqNum16)20, 1000,
      (ChiakiSeqNum16)10, (ChiakiSeqNum16)15, 1100,
      false, 500, 32, 4096);
  assert(d == CHIAKI_VIDEO_CORRUPT_REPORT_OBSOLETE);

  // Exact repeat of the same range is also obsolete.
  d = chiaki_video_corrupt_report_classify(
      (ChiakiSeqNum16)10, (ChiakiSeqNum16)20, 1000,
      (ChiakiSeqNum16)10, (ChiakiSeqNum16)20, 1100,
      false, 500, 32, 4096);
  assert(d == CHIAKI_VIDEO_CORRUPT_REPORT_OBSOLETE);
}

static void test_corrupt_report_classify_minimal_growth_boundary(void) {
  // end == last_end + 1 is the smallest possible expansion (growth == 1).
  // Well under the bypass threshold and only 1ms into the cooldown -> defer.
  ChiakiVideoCorruptReportDisposition d = chiaki_video_corrupt_report_classify(
      (ChiakiSeqNum16)175, (ChiakiSeqNum16)178, 1000,
      (ChiakiSeqNum16)175, (ChiakiSeqNum16)179, 1001,
      false, 500, 32, 4096);
  assert(d == CHIAKI_VIDEO_CORRUPT_REPORT_DEFER);
}

static void test_corrupt_report_classify_expansion_inside_cooldown_defers(void) {
  // Same start, small growth, cooldown not yet elapsed -> this is the exact
  // shape of the hardware-log spam this cooldown was added to fix (see
  // 24402261711_vitarps5-testing.log lines 1719-1853, e.g. "175 to 178" then
  // "175 to 179" a few ms later).
  ChiakiVideoCorruptReportDisposition d = chiaki_video_corrupt_report_classify(
      (ChiakiSeqNum16)175, (ChiakiSeqNum16)178, 1000,
      (ChiakiSeqNum16)175, (ChiakiSeqNum16)181, 1100, // 100ms elapsed of 500ms cooldown
      false, 500, 32, 4096);
  assert(d == CHIAKI_VIDEO_CORRUPT_REPORT_DEFER);
}

static void test_corrupt_report_classify_expansion_after_cooldown_emits(void) {
  // Same small growth as above, but the full cooldown window has elapsed --
  // this is the periodic refresh that keeps the console's view of an
  // ongoing burst current without spamming it every frame advance.
  ChiakiVideoCorruptReportDisposition d = chiaki_video_corrupt_report_classify(
      (ChiakiSeqNum16)175, (ChiakiSeqNum16)178, 1000,
      (ChiakiSeqNum16)175, (ChiakiSeqNum16)181, 1500, // elapsed_ms == cooldown_ms
      false, 500, 32, 4096);
  assert(d == CHIAKI_VIDEO_CORRUPT_REPORT_EMIT);
}

static void test_corrupt_report_classify_growth_bypass_threshold(void) {
  // growth == growth_bypass_span (32) bypasses the cooldown immediately,
  // even though only 50ms of the 500ms cooldown has elapsed -- a single
  // large jump must not wait out the full cooldown.
  ChiakiVideoCorruptReportDisposition d = chiaki_video_corrupt_report_classify(
      (ChiakiSeqNum16)175, (ChiakiSeqNum16)178, 1000,
      (ChiakiSeqNum16)175, (ChiakiSeqNum16)210, 1050, // growth = 210 - 178 = 32
      false, 500, 32, 4096);
  assert(d == CHIAKI_VIDEO_CORRUPT_REPORT_EMIT);

  // One frame short of the threshold, same elapsed time, stays deferred.
  d = chiaki_video_corrupt_report_classify(
      (ChiakiSeqNum16)175, (ChiakiSeqNum16)178, 1000,
      (ChiakiSeqNum16)175, (ChiakiSeqNum16)209, 1050, // growth = 209 - 178 = 31
      false, 500, 32, 4096);
  assert(d == CHIAKI_VIDEO_CORRUPT_REPORT_DEFER);
}

static void test_corrupt_report_classify_wraparound_growth(void) {
  // last_end=65530, end=5 wraps across the 16-bit boundary. chiaki_seq16_span()
  // must compute the true inclusive distance (12, so growth=11), not treat
  // this as a huge or negative value that would spuriously bypass the
  // cooldown.
  ChiakiVideoCorruptReportDisposition d = chiaki_video_corrupt_report_classify(
      (ChiakiSeqNum16)65530, (ChiakiSeqNum16)65530, 1000,
      (ChiakiSeqNum16)65530, (ChiakiSeqNum16)5, 1050, // growth=11 < 32, 50ms elapsed
      false, 500, 32, 4096);
  assert(d == CHIAKI_VIDEO_CORRUPT_REPORT_DEFER);

  // Same wrapped range after the cooldown elapses -> emits.
  d = chiaki_video_corrupt_report_classify(
      (ChiakiSeqNum16)65530, (ChiakiSeqNum16)65530, 1000,
      (ChiakiSeqNum16)65530, (ChiakiSeqNum16)5, 1600,
      false, 500, 32, 4096);
  assert(d == CHIAKI_VIDEO_CORRUPT_REPORT_EMIT);
}

static void test_corrupt_report_classify_bypass_cooldown_for_retirement(void) {
  // Same inputs as a cooldown-deferred expansion, but bypass_cooldown=true
  // (the caller is retiring this range for good -- e.g. gap-hold
  // FLUSH_PREVIOUS, or frame_index_prev_complete advancing past a
  // fec_failed range's pinned start) must force an emit even though the
  // cooldown/growth checks alone would defer it. Without this, a
  // cooldown-deferred range whose start is about to become unreachable
  // would be silently dropped forever instead of merely delayed.
  ChiakiVideoCorruptReportDisposition d = chiaki_video_corrupt_report_classify(
      (ChiakiSeqNum16)175, (ChiakiSeqNum16)178, 1000,
      (ChiakiSeqNum16)175, (ChiakiSeqNum16)179, 1001, // 1ms elapsed, growth=1
      true /* bypass_cooldown */, 500, 32, 4096);
  assert(d == CHIAKI_VIDEO_CORRUPT_REPORT_EMIT);

  // But bypass_cooldown must not resurrect information that's genuinely
  // obsolete (already fully covered by the last report) -- OBSOLETE still
  // wins, since there's nothing new to tell the console.
  d = chiaki_video_corrupt_report_classify(
      (ChiakiSeqNum16)175, (ChiakiSeqNum16)178, 1000,
      (ChiakiSeqNum16)175, (ChiakiSeqNum16)178, 1001,
      true /* bypass_cooldown */, 500, 32, 4096);
  assert(d == CHIAKI_VIDEO_CORRUPT_REPORT_OBSOLETE);
}

static void test_corrupt_report_classify_pathological_span_is_obsolete_even_with_bypass(void) {
  // A span this large (> span_sanity_max) indicates corrupted sequence
  // state or a multi-thousand-frame recv-thread freeze/reconnect arc
  // (GH #208-class incidents), not a genuine reportable gap. It must never
  // be emitted -- not even via bypass_cooldown=true (retirement) or a
  // brand-new start -- since that would hand the PS5's bitrate ratchet the
  // strongest possible false loss signal. Demonstrated shape from review:
  // frame_index_prev_complete=1000, frame_index_cur=6000 ->
  // retiring_start=1001, retiring_end=5999, span=4999.
  ChiakiVideoCorruptReportDisposition d = chiaki_video_corrupt_report_classify(
      (ChiakiSeqNum16)0, (ChiakiSeqNum16)0, 0,          // no prior report at all
      (ChiakiSeqNum16)1001, (ChiakiSeqNum16)5999, 1000, // span = 4999 > 4096
      true /* bypass_cooldown */, 500, 32, 4096);
  assert(d == CHIAKI_VIDEO_CORRUPT_REPORT_OBSOLETE);

  // Same pathological span without bypass_cooldown is also obsolete -- the
  // guard is checked first, ahead of every other branch, so it applies
  // whether or not the caller is retiring the range.
  d = chiaki_video_corrupt_report_classify(
      (ChiakiSeqNum16)0, (ChiakiSeqNum16)0, 0,
      (ChiakiSeqNum16)1001, (ChiakiSeqNum16)5999, 1000,
      false, 500, 32, 4096);
  assert(d == CHIAKI_VIDEO_CORRUPT_REPORT_OBSOLETE);

  // A span exactly AT the sanity threshold (not exceeding it) is not
  // suppressed by this guard -- span_sanity_max is a ceiling, not a typical
  // operating value -- and falls through to normal classification (a new
  // start here, so EMIT). Proves the boundary is ">", not ">=".
  d = chiaki_video_corrupt_report_classify(
      (ChiakiSeqNum16)0, (ChiakiSeqNum16)0, 0,
      (ChiakiSeqNum16)1, (ChiakiSeqNum16)4096, 1000, // span == 4096, not > 4096
      false, 500, 32, 4096);
  assert(d == CHIAKI_VIDEO_CORRUPT_REPORT_EMIT);
}

// chiaki_seq16_forward_delta() backs the D5-B drift sampling in
// lib/src/videoreceiver.c (GH #251) -- it turns a frame-index jump into a
// wrap-safe frame count without a distance helper existing in chiaki/seqnum.h.

static void test_seq16_forward_delta_equal_is_zero(void) {
  assert(chiaki_seq16_forward_delta((ChiakiSeqNum16)100, (ChiakiSeqNum16)100) == 0U);
}

static void test_seq16_forward_delta_small_steps(void) {
  assert(chiaki_seq16_forward_delta((ChiakiSeqNum16)100, (ChiakiSeqNum16)101) == 1U);
  assert(chiaki_seq16_forward_delta((ChiakiSeqNum16)100, (ChiakiSeqNum16)105) == 5U);
}

static void test_seq16_forward_delta_wraparound(void) {
  // 65534 -> 2 crosses the 16-bit boundary: the true forward distance is
  // (65535, 0, 1, 2) = 4 steps, not a huge or negative value.
  assert(chiaki_seq16_forward_delta((ChiakiSeqNum16)65534, (ChiakiSeqNum16)2) == 4U);
  // 65535 -> 0 is the smallest possible wrap: one step.
  assert(chiaki_seq16_forward_delta((ChiakiSeqNum16)65535, (ChiakiSeqNum16)0) == 1U);
}

static void test_seq16_forward_delta_backwards_wraps_the_long_way(void) {
  // There is no distinct "backwards" case in unsigned wraparound arithmetic --
  // a `cur` that is RFC-1982-behind `prev` yields the long way around rather
  // than a negative delta. This codebase's only caller already gates on
  // chiaki_seq_num_16_gt() before reaching here, so this documents actual
  // behaviour rather than asserting it is desirable input.
  assert(chiaki_seq16_forward_delta((ChiakiSeqNum16)100, (ChiakiSeqNum16)90) == 65526U);
}

// chiaki_video_gap_hist_bucket() backs the D5-A gap histogram in
// lib/src/videoreceiver.c (GH #251). Buckets are upper-exclusive: gap_ms ==
// an edge value falls into the NEXT bucket, not the one the edge terminates.

static void test_gap_hist_bucket_zero_is_bucket_zero(void) {
  assert(chiaki_video_gap_hist_bucket(0) == 0U);
}

static void test_gap_hist_bucket_edges_are_upper_exclusive(void) {
  // edge-1 stays in the lower bucket; the edge value itself moves to the next.
  assert(chiaki_video_gap_hist_bucket(VIDEO_GAP_HIST_EDGE_0_MS - 1) == 0U);
  assert(chiaki_video_gap_hist_bucket(VIDEO_GAP_HIST_EDGE_0_MS) == 1U);
  assert(chiaki_video_gap_hist_bucket(VIDEO_GAP_HIST_EDGE_1_MS - 1) == 1U);
  assert(chiaki_video_gap_hist_bucket(VIDEO_GAP_HIST_EDGE_1_MS) == 2U);
  assert(chiaki_video_gap_hist_bucket(VIDEO_GAP_HIST_EDGE_2_MS - 1) == 2U);
  assert(chiaki_video_gap_hist_bucket(VIDEO_GAP_HIST_EDGE_2_MS) == 3U);
  assert(chiaki_video_gap_hist_bucket(VIDEO_GAP_HIST_EDGE_3_MS - 1) == 3U);
  assert(chiaki_video_gap_hist_bucket(VIDEO_GAP_HIST_EDGE_3_MS) == 4U);
  assert(chiaki_video_gap_hist_bucket(VIDEO_GAP_HIST_EDGE_4_MS - 1) == 4U);
  assert(chiaki_video_gap_hist_bucket(VIDEO_GAP_HIST_EDGE_4_MS) == 5U);
  assert(chiaki_video_gap_hist_bucket(VIDEO_GAP_HIST_EDGE_5_MS - 1) == 5U);
  assert(chiaki_video_gap_hist_bucket(VIDEO_GAP_HIST_EDGE_5_MS) == 6U);
  assert(chiaki_video_gap_hist_bucket(VIDEO_GAP_HIST_EDGE_6_MS - 1) == 6U);
  assert(chiaki_video_gap_hist_bucket(VIDEO_GAP_HIST_EDGE_6_MS) == 7U);
  assert(chiaki_video_gap_hist_bucket(VIDEO_GAP_HIST_EDGE_7_MS - 1) == 7U);
  assert(chiaki_video_gap_hist_bucket(VIDEO_GAP_HIST_EDGE_7_MS) == 8U);
}

static void test_gap_hist_bucket_hard_stall_is_top_bucket(void) {
  assert(chiaki_video_gap_hist_bucket(300) == 8U);
  assert(chiaki_video_gap_hist_bucket(10000) == 8U);
  assert(chiaki_video_gap_hist_bucket(UINT64_MAX) == 8U);
}

// chiaki_packet_stats_get's seq contribution used to report seq_diff (i.e.
// MAXIMUM loss) whenever seq_received exceeded the expected span, instead of
// 0. Isolate the seq-only contribution by resetting on read (gen_ fields
// start and stay at 0), so the returned `lost` IS the seq-only lost value.
static void test_packet_stats_seq_received_exceeds_span_reports_zero_lost(void) {
  ChiakiPacketStats stats;
  assert(chiaki_packet_stats_init(&stats) == CHIAKI_ERR_SUCCESS);

  stats.seq_min = 0;
  stats.seq_max = 10;    // expected span = 10
  stats.seq_received = 15; // more received than the span implies

  uint64_t received = 0;
  uint64_t lost = 0;
  uint64_t recovered = 0;
  chiaki_packet_stats_get(&stats, true, &received, &lost, &recovered);

  assert(received == 15);
  assert(lost == 0);
  assert(recovered == 0);

  chiaki_packet_stats_fini(&stats);
}

// ChiakiSeqNum16 is uint16_t; seq_max - seq_min must wrap at 16 bits (not
// promote to a huge 64-bit value) when seq_max < seq_min.
static void test_packet_stats_seq_16bit_wrap(void) {
  ChiakiPacketStats stats;
  assert(chiaki_packet_stats_init(&stats) == CHIAKI_ERR_SUCCESS);

  stats.seq_min = (ChiakiSeqNum16)65530;
  stats.seq_max = (ChiakiSeqNum16)5; // wrapped: true span is (65536 - 65530) + 5 = 11
  stats.seq_received = 8;            // 8 of the 11 expected arrived -> 3 lost

  uint64_t received = 0;
  uint64_t lost = 0;
  uint64_t recovered = 0;
  chiaki_packet_stats_get(&stats, true, &received, &lost, &recovered);

  assert(received == 8);
  assert(lost == 3); // would be an enormous value without the (uint16_t) truncation fix

  chiaki_packet_stats_fini(&stats);
}

// chiaki_packet_stats_push_generation must aggregate lost/recovered independently:
// a FEC_RECOVERED-style push reports the shortfall as `recovered` with zero raw
// loss, while a FEC_FAILED-style push reports the shortfall as raw `lost`.
static void test_packet_stats_push_generation_aggregation(void) {
  ChiakiPacketStats stats;
  assert(chiaki_packet_stats_init(&stats) == CHIAKI_ERR_SUCCESS);

  const uint64_t expected = 16;
  const uint64_t recovered_n = 2;
  chiaki_packet_stats_push_generation(&stats, expected, 0, recovered_n);

  uint64_t received = 0;
  uint64_t lost = 0;
  uint64_t recovered = 0;
  chiaki_packet_stats_get(&stats, true, &received, &lost, &recovered);
  assert(lost == 0);
  assert(recovered == recovered_n);

  const uint64_t actual = 12;
  const uint64_t expected2 = 16;
  chiaki_packet_stats_push_generation(&stats, actual, expected2 - actual, 0);

  chiaki_packet_stats_get(&stats, true, &received, &lost, &recovered);
  assert(lost == expected2 - actual);
  assert(recovered == 0);

  chiaki_packet_stats_fini(&stats);
}

void run_packet_path_tests(void) {
  test_reorder_find_first_set_after_skip_and_drop();
  test_reorder_wraparound_progression();
  test_reorder_skip_clears_entry_slot();
  test_gap_update_set_and_extend();
  test_gap_update_flush_previous_on_new_range();
  test_gap_update_none_for_stale_end_and_null_state();
  test_gap_update_wraparound_extend();
  test_corrupt_report_classify_new_burst_always_emits();
  test_corrupt_report_classify_obsolete_when_fully_covered();
  test_corrupt_report_classify_minimal_growth_boundary();
  test_corrupt_report_classify_expansion_inside_cooldown_defers();
  test_corrupt_report_classify_expansion_after_cooldown_emits();
  test_corrupt_report_classify_growth_bypass_threshold();
  test_corrupt_report_classify_wraparound_growth();
  test_corrupt_report_classify_bypass_cooldown_for_retirement();
  test_corrupt_report_classify_pathological_span_is_obsolete_even_with_bypass();
  test_seq16_forward_delta_equal_is_zero();
  test_seq16_forward_delta_small_steps();
  test_seq16_forward_delta_wraparound();
  test_seq16_forward_delta_backwards_wraps_the_long_way();
  test_gap_hist_bucket_zero_is_bucket_zero();
  test_gap_hist_bucket_edges_are_upper_exclusive();
  test_gap_hist_bucket_hard_stall_is_top_bucket();
  test_packet_stats_seq_received_exceeds_span_reports_zero_lost();
  test_packet_stats_seq_16bit_wrap();
  test_packet_stats_push_generation_aggregation();
}
