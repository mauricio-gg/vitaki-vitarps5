// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the debug Input test panel (GH #458): the click measurement and the
// rule for which touches the panel swallows. `./tools/build.sh test` only cross-compiles, so run
// these natively:
//
//   cc -std=c99 -Wall -Wextra -DVITARPS5_DEBUG_TOOLS=1 -I vita/include \
//      test/input_test_tests.c vita/src/input_test_click.c -o /tmp/input_test_tests && \
//      /tmp/input_test_tests

#include <assert.h>
#include <stdio.h>

#include "debug_tools.h"
#include "input_test_click.h"

#define FRAME_US 2000ULL /* the 2 ms input loop */
#define MS(n) ((uint64_t)(n) * 1000ULL)

/* Runs the click for @p hold_ms the way the input loop does, one sample per frame. */
static uint64_t press_for(InputTestClick *c, uint64_t now_us, uint64_t hold_ms) {
  uint64_t end_us = now_us + MS(hold_ms);
  for (; now_us < end_us; now_us += FRAME_US)
    input_test_click_step(c, true, now_us);
  input_test_click_step(c, false, now_us);
  return now_us;
}

/* A 50 ms click (what the touchpad gesture sends) reads 50 ms and counts once, however many
 * passes it spans. */
static void test_click_reads_hold_and_counts_once(void) {
  InputTestClick c;
  input_test_click_init(&c);
  assert(!c.hold_valid && c.count == 0);

  press_for(&c, MS(1000), 50);
  assert(c.count == 1);
  assert(c.hold_valid && c.last_hold_ms == 50);
  assert(c.last_press_us == MS(1000));
}

/* The hold shown is the LAST finished click, and a click still in progress does not replace it. */
static void test_hold_is_last_finished_click(void) {
  InputTestClick c;
  input_test_click_init(&c);
  uint64_t now_us = press_for(&c, MS(1000), 50);
  now_us = press_for(&c, now_us + MS(300), 120);
  assert(c.count == 2 && c.last_hold_ms == 120);

  input_test_click_step(&c, true, now_us + MS(500)); /* third press begins, not finished */
  assert(c.count == 3 && c.last_hold_ms == 120);
}

/* Touch units for a screen pixel. */
#define TU(px) ((px) * DEBUG_WIDGET_TOUCH_PER_SCREEN_PX)

/* A touch-down on the panel body is sent to the game; one on the Close pill is swallowed, but
 * only while the panel is open. Each button swallows the strip under its own width down to the
 * bottom of the screen, and nothing beside it. */
static void test_touch_targets(void) {
  assert(debug_touch_target_at(TU(DEBUG_PANEL_X + 100), TU(DEBUG_PANEL_Y + 200), true) ==
         DEBUG_TOUCH_TARGET_NONE);

  const int close_x = TU(DEBUG_CLOSE_X + 4);
  const int close_y = TU(DEBUG_CLOSE_Y + 4);
  assert(debug_touch_target_at(close_x, close_y, true) == DEBUG_TOUCH_TARGET_PANEL_CLOSE);
  assert(debug_touch_target_at(close_x, close_y, false) == DEBUG_TOUCH_TARGET_NONE);

  const int bottom_y = TU(DEBUG_WIDGET_SCREEN_HEIGHT - 1);
  const int box_top_y = TU(DEBUG_TAP_BOX_Y);
  const int input_last_x = DEBUG_INPUT_BTN_X + DEBUG_INPUT_BTN_W - 1;
  const int resync_last_x = DEBUG_WIDGET_X + DEBUG_WIDGET_W - 1;

  /* Inside each button's x-span, at the bottom of the screen and at the top of the box. */
  assert(debug_touch_target_at(TU(DEBUG_INPUT_BTN_X), bottom_y, false) ==
         DEBUG_TOUCH_TARGET_INPUT_TEST);
  assert(debug_touch_target_at(TU(input_last_x), box_top_y, false) == DEBUG_TOUCH_TARGET_INPUT_TEST);
  assert(debug_touch_target_at(TU(DEBUG_WIDGET_X), bottom_y, false) == DEBUG_TOUCH_TARGET_RESYNC);
  assert(debug_touch_target_at(TU(resync_last_x), box_top_y, false) == DEBUG_TOUCH_TARGET_RESYNC);

  /* Just outside: left of the Input test button, the gap between the buttons, and the
   * bottom-right corner of the screen are all the game's. */
  assert(debug_touch_target_at(TU(DEBUG_INPUT_BTN_X - 1), bottom_y, false) ==
         DEBUG_TOUCH_TARGET_NONE);
  assert(debug_touch_target_at(TU(input_last_x + 1), bottom_y, false) == DEBUG_TOUCH_TARGET_NONE);
  assert(debug_touch_target_at(TU(DEBUG_WIDGET_X - 1), bottom_y, false) == DEBUG_TOUCH_TARGET_NONE);
  assert(debug_touch_target_at(TU(DEBUG_WIDGET_SCREEN_WIDTH - 1), bottom_y, false) ==
         DEBUG_TOUCH_TARGET_NONE);

  /* Above the box the buttons take nothing. */
  assert(debug_touch_target_at(TU(DEBUG_WIDGET_X), TU(DEBUG_TAP_BOX_Y - 1), false) ==
         DEBUG_TOUCH_TARGET_NONE);
}

int main(void) {
  test_click_reads_hold_and_counts_once();
  test_hold_is_last_finished_click();
  test_touch_targets();
  printf("input_test_tests: all passed\n");
  return 0;
}
