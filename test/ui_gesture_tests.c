// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the pure touch gesture rules: tap, long-press, swipe and swipe step
// counting. `./tools/build.sh test` only cross-compiles for arm-vita-eabi and never executes, so
// run these natively:
//
//   cc -std=c99 -Wall -Wextra -I vita/include \
//      test/ui_gesture_tests.c vita/src/ui/ui_gesture.c -o /tmp/ui_gesture_tests && \
//      /tmp/ui_gesture_tests

#include <assert.h>
#include <stdio.h>

#include "ui/ui_gesture.h"

/* catches: a quick still touch stops being a tap, so no row can be selected by touch. */
static void test_quick_still_touch_is_a_tap(void) {
  UiGestureTouch t = {.released = true, .held_ms = 120};
  assert(ui_gesture_classify(&t) == UI_GESTURE_TAP);
}

/* catches: the long-press never fires, fires on every frame of the hold, or the release after it
 * still counts as a tap (which would connect to the console the user only wanted Options for). */
static void test_hold_fires_long_press_once_and_release_is_not_a_tap(void) {
  UiGestureTouch t = {.held_ms = UI_LONG_PRESS_MS};
  assert(ui_gesture_classify(&t) == UI_GESTURE_LONG_PRESS);

  t.long_pressed = true;
  t.held_ms = UI_LONG_PRESS_MS + 16;
  assert(ui_gesture_classify(&t) == UI_GESTURE_NONE);

  t.released = true;
  assert(ui_gesture_classify(&t) == UI_GESTURE_NONE);
}

/* catches: the threshold drifts early, so an ordinary slow tap opens Options. */
static void test_just_under_threshold_is_not_a_long_press(void) {
  UiGestureTouch t = {.held_ms = UI_LONG_PRESS_MS - 1};
  assert(ui_gesture_classify(&t) == UI_GESTURE_NONE);
}

/* catches: a finger that scrolls and then rests still opens Options, or a long drag is read as a
 * tap on release. */
static void test_nine_px_move_is_a_swipe_never_a_tap_or_long_press(void) {
  UiGestureTouch t = {.dx = 9.0f, .held_ms = 2000};
  assert(ui_gesture_classify(&t) == UI_GESTURE_SWIPE);

  t.released = true;
  assert(ui_gesture_classify(&t) == UI_GESTURE_SWIPE);

  /* Back at the start but it already swiped: still a swipe. */
  UiGestureTouch back = {.swiped = true, .held_ms = 2000};
  assert(ui_gesture_classify(&back) == UI_GESTURE_SWIPE);
}

/* catches: the 8 px tolerance tightens, so a normal finger wobble stops registering as a tap. */
static void test_eight_px_move_is_still_a_tap(void) {
  UiGestureTouch t = {.released = true, .dx = 8.0f, .held_ms = 100};
  assert(ui_gesture_classify(&t) == UI_GESTURE_TAP);
}

/* catches: swipe steps round away from zero or drop the sign, so a list jumps a row too far or
 * moves the wrong way. */
static void test_swipe_steps_truncate_toward_zero(void) {
  assert(ui_gesture_swipe_steps(63.0f, 64) == 0);
  assert(ui_gesture_swipe_steps(64.0f, 64) == 1);
  assert(ui_gesture_swipe_steps(-64.0f, 64) == -1);
  assert(ui_gesture_swipe_steps(-130.0f, 64) == -2);
}

int main(void) {
  test_quick_still_touch_is_a_tap();
  test_hold_fires_long_press_once_and_release_is_not_a_tap();
  test_just_under_threshold_is_not_a_long_press();
  test_nine_px_move_is_a_swipe_never_a_tap_or_long_press();
  test_eight_px_move_is_still_a_tap();
  test_swipe_steps_truncate_toward_zero();
  puts("ui_gesture_tests: all passed");
  return 0;
}
