// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for which front touches belong to the on-screen mic control (GH #474).
// Run natively:
//
//   cc -std=c99 -Wall -Wextra -I vita/include test/mic_touch_tests.c vita/src/mic_touch.c \
//      -o /tmp/mic_touch_tests && /tmp/mic_touch_tests

#include <assert.h>
#include <stdio.h>

#include "mic_touch.h"

/* Raw touch units for a screen pixel. */
#define TU(px) ((px) * MIC_TOUCH_PER_SCREEN_PX)

/* catches: a tap on the button reaching the game as a touchpad or zone touch, or the touch area being
 * the wrong size: 96 x 48 at rest (y 488-536), and the pill plus 8 px each side while the label
 * shows. */
static void test_button_touch_area(void) {
  const MicTouchMode m = MIC_TOUCH_MODE_BUTTON;
  /* Rest: x 432..527, y 488..535. */
  assert(mic_touch_hit(m, 0, TU(480), TU(512)));
  assert(mic_touch_hit(m, 0, TU(432), TU(488)));
  assert(mic_touch_hit(m, 0, TU(527), TU(535)));
  assert(!mic_touch_hit(m, 0, TU(431), TU(512)));
  assert(!mic_touch_hit(m, 0, TU(528), TU(512)));
  assert(!mic_touch_hit(m, 0, TU(480), TU(487)));
  assert(!mic_touch_hit(m, 0, TU(480), TU(536)));
  /* "Mic muted" (144 wide): pill x 408..551, area x 400..559. */
  assert(mic_touch_hit(m, MIC_BTN_LABEL_MUTED_W, TU(400), TU(512)));
  assert(mic_touch_hit(m, MIC_BTN_LABEL_MUTED_W, TU(559), TU(512)));
  assert(!mic_touch_hit(m, MIC_BTN_LABEL_MUTED_W, TU(399), TU(512)));
  assert(!mic_touch_hit(m, MIC_BTN_LABEL_MUTED_W, TU(560), TU(512)));
  /* A point just outside the rest area is inside the widened one. */
  assert(!mic_touch_hit(m, 0, TU(420), TU(512)));
  assert(mic_touch_hit(m, MIC_BTN_LABEL_ON_W, TU(420), TU(512)));
  /* The widened area is no taller than the rest one. */
  assert(!mic_touch_hit(m, MIC_BTN_LABEL_MUTED_W, TU(480), TU(487)));
}

/* catches: touches near the corner icon being swallowed when no control is there, and the corner
 * icon not taking taps. */
static void test_corner_and_none_modes(void) {
  assert(mic_touch_hit(MIC_TOUCH_MODE_CORNER, 0, TU(48), TU(48)));
  assert(mic_touch_hit(MIC_TOUCH_MODE_CORNER, 0, TU(24), TU(24)));
  assert(!mic_touch_hit(MIC_TOUCH_MODE_CORNER, 0, TU(23), TU(48)));
  assert(!mic_touch_hit(MIC_TOUCH_MODE_CORNER, 0, TU(480), TU(512)));
  assert(!mic_touch_hit(MIC_TOUCH_MODE_BUTTON, 0, TU(48), TU(48)));
  assert(!mic_touch_hit(MIC_TOUCH_MODE_NONE, 0, TU(480), TU(512)));
  assert(!mic_touch_hit(MIC_TOUCH_MODE_NONE, 0, TU(48), TU(48)));
}

/* catches: a held finger toggling the mute again on every pass, a swallowed finger that slides off
 * the button leaking to the game, a game finger that slides onto the button being swallowed
 * mid-gesture, and a lifted finger's slot not being freed. */
static void test_a_touch_is_judged_once_when_it_first_appears(void) {
  MicTouchTracker t = {0};
  bool tap;
  const MicTouchMode m = MIC_TOUCH_MODE_BUTTON;

  mic_touch_pass_begin(&t);
  assert(mic_touch_judge(&t, 1, TU(480), TU(512), m, 0, &tap) && tap);
  assert(!mic_touch_judge(&t, 2, TU(100), TU(100), m, 0, &tap) && !tap);
  mic_touch_pass_end(&t);

  /* Both fingers move: 1 slides off the button, 2 slides onto it. */
  mic_touch_pass_begin(&t);
  assert(mic_touch_judge(&t, 1, TU(100), TU(100), m, 0, &tap) && !tap);
  assert(!mic_touch_judge(&t, 2, TU(480), TU(512), m, 0, &tap) && !tap);
  mic_touch_pass_end(&t);

  /* Finger 1 lifts; a new touch with the same id on the button is a new tap. */
  mic_touch_pass_begin(&t);
  assert(!mic_touch_judge(&t, 2, TU(480), TU(512), m, 0, &tap));
  mic_touch_pass_end(&t);
  mic_touch_pass_begin(&t);
  assert(mic_touch_judge(&t, 1, TU(480), TU(512), m, 0, &tap) && tap);
  mic_touch_pass_end(&t);
}

int main(void) {
  test_button_touch_area();
  test_corner_and_none_modes();
  test_a_touch_is_judged_once_when_it_first_appears();
  printf("mic_touch_tests: all passed\n");
  return 0;
}
