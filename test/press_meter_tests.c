// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) test for the press-to-picture jump detector in vita/src/press_meter.c
// (ticket #413). `./tools/build.sh test` only cross-compiles, so run it on the host:
//   cc -std=c99 -Wall -Wextra -I vita/include test/press_meter_tests.c vita/src/press_meter.c \
//      -o /tmp/press_meter_tests && /tmp/press_meter_tests

#include <assert.h>
#include <stdio.h>

#include "press_meter.h"

#define QUIET_SIZE 400u
#define ANSWER_SIZE 5000u /* sizes seen on hardware for a menu move: 3-8 KB */
#define FRAME_GAP_US 16000ULL
#define BURST_FRAMES 25 /* about 0.4 s at 60 fps */
#define MS 1000ULL

/* Feeds `count` quiet frames (sizes wobble around QUIET_SIZE) and returns the next time. */
static uint64_t feed_quiet(PressMeter *m, uint32_t count, uint64_t t_us) {
  PressJump jump;
  for (uint32_t i = 0; i < count; i++) {
    t_us += FRAME_GAP_US;
    assert(!press_meter_on_frame(m, QUIET_SIZE + (i % 5) * 40, false, t_us - 5000, t_us, &jump));
  }
  return t_us;
}

/* Feeds a menu-move burst: BURST_FRAMES large frames. Stores the jump in *jump when one of them
 * is taken as an answer and returns how many were. Advances *t_us. */
static int feed_burst(PressMeter *m, uint64_t *t_us, uint32_t size, PressJump *jump) {
  int found = 0;
  for (int i = 0; i < BURST_FRAMES; i++) {
    *t_us += FRAME_GAP_US;
    if (press_meter_on_frame(m, size, false, *t_us - 5000, *t_us, jump))
      found++;
  }
  return found;
}

/** A big frame nobody asked for never produces a sample (and is counted as such). */
static void test_no_press_no_sample(void) {
  PressMeter m;
  PressJump jump;
  press_meter_reset(&m);
  uint64_t t = feed_quiet(&m, 40, 1000000);
  assert(!press_meter_on_frame(&m, ANSWER_SIZE, false, t, t + 1, &jump));
  assert(m.jumps_total == 0 && m.unmatched_total == 1);
}

/** Two presses 600 ms apart, each answered 400 ms after its press (the second press lands inside
 *  the first move's burst): both get a sample, paired with the right press, unambiguous, the
 *  second marked as sent inside a burst. The quiet reference must have recovered by the second
 *  answer even though the first burst's frames were five times the quiet level. */
static void test_two_presses_two_samples(void) {
  PressMeter m;
  PressJump j1, j2;
  press_meter_reset(&m);
  uint64_t t = feed_quiet(&m, 30, 1000000);

  uint64_t press1 = t + 1000;
  press_meter_on_press(&m, 1, press1);
  t = feed_quiet(&m, 24, press1); /* 384 ms of quiet */
  uint64_t burst1_start = t;
  assert(feed_burst(&m, &t, ANSWER_SIZE, &j1) == 1);
  assert(j1.press_count == 1 && j1.press_us == press1 && j1.waiting == 1 && !j1.in_burst);
  assert(j1.arrive_us - burst1_start == FRAME_GAP_US);

  uint64_t press2 = press1 + 600 * MS; /* inside the first burst */
  assert(press2 < t);
  press_meter_on_press(&m, 2, press2);
  assert(m.inburst_total == 1);
  t = feed_quiet(&m, 12, t); /* calm: the burst ends */
  while (t + FRAME_GAP_US < press2 + 400 * MS) /* quiet until the second answer is due */
    t = feed_quiet(&m, 1, t);
  assert(feed_burst(&m, &t, ANSWER_SIZE, &j2) == 1);
  assert(j2.press_count == 2 && j2.press_us == press2 && j2.waiting == 1 && j2.in_burst);
  assert(m.jumps_total == 2 && m.unmatched_total == 0);
}

/** The big frames that follow a burst's first frame never become the answer to a press that is
 *  already waiting: that press stays queued until a real new jump. */
static void test_burst_tail_is_not_the_next_answer(void) {
  PressMeter m;
  PressJump jump;
  press_meter_reset(&m);
  uint64_t t = feed_quiet(&m, 30, 1000000);

  press_meter_on_press(&m, 1, t);
  t = feed_quiet(&m, 5, t);
  press_meter_on_press(&m, 2, t); /* two waiting when the burst starts */
  uint64_t burst_start = t;
  assert(feed_burst(&m, &t, ANSWER_SIZE, &jump) == 1); /* later frames, even huge ones, add none */
  assert(jump.press_count == 1 && jump.waiting == 2 && jump.press_us < burst_start);
  assert(feed_burst(&m, &t, ANSWER_SIZE * 4, &jump) == 0);
  assert(m.jumps_total == 1 && m.queue_len == 1 && m.queue[0].press_count == 2);
}

/** One outlier among the quiet frames does not hide a 3 KB answer. With the largest of the
 *  window as reference (1612 -> needs 4836 bytes) this answer would be missed. */
static void test_outlier_does_not_hide_a_small_answer(void) {
  PressMeter m;
  PressJump jump;
  press_meter_reset(&m);
  uint64_t t = feed_quiet(&m, 30, 1000000);
  t += FRAME_GAP_US;
  assert(!press_meter_on_frame(&m, 1612, false, t - 5000, t, &jump)); /* the outlier */
  t = feed_quiet(&m, 3, t);

  press_meter_on_press(&m, 1, t);
  t += 300 * MS;
  assert(press_meter_on_frame(&m, 3033, false, t - 5000, t, &jump));
  assert(jump.quiet_ref < 1000 && jump.waiting == 1);
}

/** A press with no answer expires and, once it has, cannot take a later press's answer; while it
 *  is still within its life it does take it, and the sample says two presses were candidates. */
static void test_dead_press_takes_an_answer_only_while_alive(void) {
  PressMeter m;
  PressJump jump;
  press_meter_reset(&m);
  uint64_t t = feed_quiet(&m, 30, 1000000);

  uint64_t dead = t;
  press_meter_on_press(&m, 1, dead);
  uint64_t late = dead + PRESS_METER_EXPIRY_US + 100 * MS;
  t = feed_quiet(&m, 70, dead); /* 1.12 s of quiet: the first press expires */
  press_meter_on_press(&m, 2, late);
  assert(m.expired_total == 1 && m.queue_len == 1);
  t = late + 400 * MS;
  assert(press_meter_on_frame(&m, ANSWER_SIZE, false, t - 5000, t, &jump));
  assert(jump.press_count == 2 && jump.waiting == 1);

  /* Same again, but the second press comes while the first still lives. */
  PressMeter n;
  press_meter_reset(&n);
  t = feed_quiet(&n, 30, 1000000);
  press_meter_on_press(&n, 1, t);
  press_meter_on_press(&n, 2, t + 500 * MS);
  uint64_t answer = t + 900 * MS;
  assert(press_meter_on_frame(&n, ANSWER_SIZE, false, answer - 5000, answer, &jump));
  assert(jump.press_count == 1 && jump.waiting == 2);
}

/** A full queue refuses the next press and counts it; the waiting ones are untouched. */
static void test_full_queue_refuses_and_counts(void) {
  PressMeter m;
  PressJump jump;
  press_meter_reset(&m);
  uint64_t t = feed_quiet(&m, 30, 1000000);

  for (uint32_t i = 1; i <= PRESS_METER_QUEUE_DEPTH + 1; i++)
    press_meter_on_press(&m, i, t + i * 10 * MS);
  assert(m.full_total == 1 && m.queue_len == PRESS_METER_QUEUE_DEPTH);
  t += 200 * MS;
  assert(press_meter_on_frame(&m, ANSWER_SIZE, false, t - 5000, t, &jump));
  assert(jump.press_count == 1 && jump.waiting == PRESS_METER_QUEUE_DEPTH);
}

/** A big frame whose first packet was read before the press was sent is not its answer; the
 *  press keeps waiting for a later jump. */
static void test_frame_started_before_press_is_not_the_answer(void) {
  PressMeter m;
  PressJump jump;
  press_meter_reset(&m);
  uint64_t t = feed_quiet(&m, 30, 1000000);

  uint64_t press_us = t + 50000;
  press_meter_on_press(&m, 1, press_us);
  assert(!press_meter_on_frame(&m, ANSWER_SIZE, false, press_us - 10000, press_us + 2000, &jump));
  assert(m.unmatched_total == 1 && m.queue_len == 1);

  t = press_us + 2000;
  assert(feed_burst(&m, &t, ANSWER_SIZE, &jump) == 0); /* still inside that burst */
  t = feed_quiet(&m, 12, t);
  t += 100 * MS;
  assert(press_meter_on_frame(&m, ANSWER_SIZE, false, t - 5000, t, &jump));
  assert(jump.press_count == 1);
}

/** A corrupt recovery frame after packet loss is not the answer to the press. */
static void test_corrupt_big_frame_is_ignored(void) {
  PressMeter m;
  PressJump jump;
  press_meter_reset(&m);
  uint64_t t = feed_quiet(&m, 30, 1000000);

  press_meter_on_press(&m, 1, t);
  assert(!press_meter_on_frame(&m, ANSWER_SIZE * 4, true, t + 5000, t + 6000, &jump));
  assert(m.jumps_total == 0);
  assert(press_meter_on_frame(&m, ANSWER_SIZE, false, t + 20000, t + 40000, &jump));
}

/** A picture that becomes permanently busier does not blind the meter for good: the burst is cut
 *  off, the quiet window rebuilt, and a clear step above the new level is seen again. */
static void test_permanently_busier_picture_recovers(void) {
  PressMeter m;
  PressJump jump;
  press_meter_reset(&m);
  uint64_t t = feed_quiet(&m, 30, 1000000);

  uint64_t end = t + PRESS_METER_BURST_MAX_US + 200 * MS;
  while (t < end) {
    t += FRAME_GAP_US;
    assert(!press_meter_on_frame(&m, ANSWER_SIZE, false, t - 5000, t, &jump));
  }
  press_meter_on_press(&m, 1, t);
  t += 300 * MS;
  assert(press_meter_on_frame(&m, ANSWER_SIZE * 6, false, t - 5000, t, &jump));
}

/** A session restart (counters begin again) gives the new value, not a huge unsigned delta. */
static void test_counter_delta_survives_restart(void) {
  PressCounters prev = {.expired = 9, .full = 2, .inburst = 5, .unseen = 1, .unmatched = 4, .lost = 3};
  PressCounters now = {.expired = 1, .full = 2, .inburst = 7, .unseen = 0, .unmatched = 4, .lost = 0};
  PressCounters d;
  press_counters_delta(&now, &prev, &d);
  assert(d.expired == 1 && d.full == 0 && d.inburst == 2 && d.unseen == 0 && d.unmatched == 0 &&
         d.lost == 0);
}

/** Finished samples keep raw values and flag an impossible order instead of hiding it. */
static void test_complete_flags_bad_order(void) {
  PressJump jump = {1, 1000000, 1030000, 1050000, 20000, 1000, 1, false};
  PressSample s;

  press_meter_complete(&jump, 1062000, 1075000, &s);
  assert(s.first_us == 30000 && s.assembled_us == 50000);
  assert(s.decoded_us == 62000 && s.shown_us == 75000 && s.order_ok);

  press_meter_complete(&jump, 1040000, 1075000, &s); /* decoded before assembled */
  assert(s.decoded_us == 40000 && !s.order_ok);

  jump.first_us = 999500; /* first packet "before" the press by the 1 ms stamp resolution */
  press_meter_complete(&jump, 1062000, 1075000, &s);
  assert(s.first_us == -500 && !s.order_ok);
}

int main(void) {
  test_no_press_no_sample();
  test_two_presses_two_samples();
  test_burst_tail_is_not_the_next_answer();
  test_outlier_does_not_hide_a_small_answer();
  test_dead_press_takes_an_answer_only_while_alive();
  test_full_queue_refuses_and_counts();
  test_frame_started_before_press_is_not_the_answer();
  test_corrupt_big_frame_is_ignored();
  test_permanently_busier_picture_recovers();
  test_counter_delta_survives_restart();
  test_complete_flags_bad_order();
  printf("press_meter_tests: ok\n");
  return 0;
}
