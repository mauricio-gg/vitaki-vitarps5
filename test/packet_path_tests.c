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
  test_packet_stats_seq_received_exceeds_span_reports_zero_lost();
  test_packet_stats_seq_16bit_wrap();
  test_packet_stats_push_generation_aggregation();
}
