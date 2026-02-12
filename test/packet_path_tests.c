#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "chiaki/reorderqueue.h"
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

void run_packet_path_tests(void) {
  test_reorder_find_first_set_after_skip_and_drop();
  test_reorder_wraparound_progression();
  test_gap_update_set_and_extend();
  test_gap_update_flush_previous_on_new_range();
}
