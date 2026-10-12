// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) test for the press-to-picture jump detector in vita/src/press_meter.c
// (ticket #413). `./tools/build.sh test` only cross-compiles, so run it on the host:
//   cc -std=c99 -Wall -Wextra -I vita/include test/press_meter_tests.c vita/src/press_meter.c \
//      -o /tmp/press_meter_tests && /tmp/press_meter_tests

#include <assert.h>
#include <stdio.h>

#include "press_meter.h"

#define QUIET_SIZE 1000u
#define JUMP_SIZE 20000u
#define FRAME_GAP_US 16000ULL

/* Feeds `count` quiet frames (sizes wobble around QUIET_SIZE) and returns the next time. */
static uint64_t feed_quiet(PressMeter *m, uint32_t count, uint64_t t_us) {
  PressJump jump;
  for (uint32_t i = 0; i < count; i++) {
    t_us += FRAME_GAP_US;
    assert(!press_meter_on_frame(m, QUIET_SIZE + (i % 5) * 40, false, t_us - 5000, t_us, &jump));
  }
  return t_us;
}

/** Noise on quiet frames, and a large frame nobody asked for, never produce a sample. */
static void test_no_press_no_sample(void) {
  PressMeter m;
  PressJump jump;
  press_meter_reset(&m);
  uint64_t t = feed_quiet(&m, 40, 1000000);
  assert(!press_meter_on_frame(&m, JUMP_SIZE, false, t, t + 1, &jump));
  assert(m.jumps_total == 0);
}

/** One press gives exactly one sample at the first big frame, even when the animation's
 *  following frames are big too; the sample carries that frame's arrival time and size. */
static void test_one_sample_per_press(void) {
  PressMeter m;
  PressJump jump;
  press_meter_reset(&m);
  uint64_t t = feed_quiet(&m, 30, 1000000);

  uint64_t press_us = t + 1000;
  press_meter_on_press(&m, 1, press_us);
  t = feed_quiet(&m, 2, press_us); /* small frames after the press: still waiting */

  uint64_t first_us = t + 3000;
  uint64_t arrive_us = t + 20000;
  assert(press_meter_on_frame(&m, JUMP_SIZE, false, first_us, arrive_us, &jump));
  assert(jump.press_count == 1);
  assert(jump.arrive_us - jump.press_us == arrive_us - press_us);
  assert(jump.frame_size == JUMP_SIZE);
  assert(jump.quiet_ref >= QUIET_SIZE && jump.quiet_ref < JUMP_SIZE / 3);

  for (int i = 1; i <= 5; i++)
    assert(!press_meter_on_frame(&m, JUMP_SIZE, false, arrive_us + i, arrive_us + i * 1000, &jump));
  assert(m.jumps_total == 1);
}

/** A press that gets no answer expires without a sample, and the next press still measures. */
static void test_expired_press_does_not_block_the_next(void) {
  PressMeter m;
  PressJump jump;
  press_meter_reset(&m);
  uint64_t t = feed_quiet(&m, 30, 1000000);

  press_meter_on_press(&m, 1, t);
  t = feed_quiet(&m, 2, t);
  t += PRESS_METER_EXPIRY_US;
  assert(!press_meter_on_frame(&m, JUMP_SIZE, false, t - 1000, t, &jump)); /* too late */
  assert(m.expired_total == 1);
  assert(m.jumps_total == 0);

  t = feed_quiet(&m, 20, t); /* the late big frame went in as normal material */
  press_meter_on_press(&m, 2, t);
  assert(press_meter_on_frame(&m, JUMP_SIZE, false, t + 10000, t + 30000, &jump));
  assert(jump.press_count == 2);
}

/** A corrupt recovery frame after packet loss is not the answer to the press. */
static void test_corrupt_big_frame_is_ignored(void) {
  PressMeter m;
  PressJump jump;
  press_meter_reset(&m);
  uint64_t t = feed_quiet(&m, 30, 1000000);

  press_meter_on_press(&m, 1, t);
  assert(!press_meter_on_frame(&m, JUMP_SIZE * 4, true, t + 5000, t + 6000, &jump));
  assert(m.jumps_total == 0);
  assert(press_meter_on_frame(&m, JUMP_SIZE, false, t + 20000, t + 40000, &jump));
}

/** A big frame whose first packet was read before the press was sent is not its answer. */
static void test_frame_started_before_press_is_not_the_jump(void) {
  PressMeter m;
  PressJump jump;
  press_meter_reset(&m);
  uint64_t t = feed_quiet(&m, 30, 1000000);

  uint64_t press_us = t + 50000;
  press_meter_on_press(&m, 1, press_us);
  assert(!press_meter_on_frame(&m, JUMP_SIZE, false, press_us - 10000, press_us + 2000, &jump));
  assert(press_meter_on_frame(&m, JUMP_SIZE, false, press_us + 15000, press_us + 30000, &jump));
}

/** A second press while one waits is counted and does not move the first one's start time. */
static void test_overlapping_press_keeps_the_oldest(void) {
  PressMeter m;
  PressJump jump;
  press_meter_reset(&m);
  uint64_t t = feed_quiet(&m, 30, 1000000);

  press_meter_on_press(&m, 1, t);
  press_meter_on_press(&m, 2, t + 200000);
  assert(m.overlap_total == 1);
  assert(press_meter_on_frame(&m, JUMP_SIZE, false, t + 300000, t + 310000, &jump));
  assert(jump.press_count == 1 && jump.press_us == t);
}

/** Finished samples keep raw values and flag an impossible order instead of hiding it. */
static void test_complete_flags_bad_order(void) {
  PressJump jump = {1, 1000000, 1030000, 1050000, 20000, 1000};
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
  test_one_sample_per_press();
  test_expired_press_does_not_block_the_next();
  test_corrupt_big_frame_is_ignored();
  test_frame_started_before_press_is_not_the_jump();
  test_overlapping_press_keeps_the_oldest();
  test_complete_flags_bad_order();
  printf("press_meter_tests: ok\n");
  return 0;
}
