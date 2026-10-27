// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the front-touch start-zone rule (GH #460): a touch keeps the outcome
// of the zone it started in until the finger lifts. `./tools/build.sh test` only cross-compiles for
// arm-vita-eabi and never executes, so run these natively:
//
//   cc -std=c99 -Wall -Wextra -I vita/include -I lib/include \
//      test/front_touch_zone_tests.c vita/src/front_touch_zone.c -o /tmp/front_touch_zone_tests && \
//      /tmp/front_touch_zone_tests

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "front_touch_zone.h"

#define MAX_W 1919
#define MAX_H 1087

/* Screen positions (6 columns x 3 rows grid; a column is ~320 wide, a row ~362 tall). */
#define MID_X 960
#define MID_Y 540 /* middle row */
#define EDGE_X 50 /* left edge column, unmapped */
#define BOTTOM_Y 1000 /* bottom row */

#define ID_A 3
#define ID_B 4

/* The layout from the bug report: middle columns Touchpad, edge columns unmapped, the two
 * bottom-middle zones PS. */
static VitakiCtrlMapInfo ceo_layout(void) {
  VitakiCtrlMapInfo vcmi;
  memset(&vcmi, 0, sizeof(vcmi));
  for (int row = 0; row < VITAKI_FRONT_TOUCH_GRID_ROWS; row++) {
    for (int col = 1; col <= 4; col++) {
      int in = VITAKI_CTRL_IN_FRONTTOUCH_GRID_START + row * VITAKI_FRONT_TOUCH_GRID_COLS + col;
      vcmi.in_out_btn[in] = (row == 2 && (col == 2 || col == 3)) ? VITAKI_CTRL_OUT_PS
                                                                  : VITAKI_CTRL_OUT_TOUCHPAD;
    }
  }
  return vcmi;
}

/* One input pass with a single finger at (x, y). */
static FrontTouchOutcome pass(FrontTouchZones *z, const VitakiCtrlMapInfo *vcmi, uint8_t id, int x,
                              int y) {
  front_touch_zone_begin_pass(z);
  const FrontTouchOutcome *o = front_touch_zone_latch(z, id, x, y, MAX_W, MAX_H, vcmi);
  assert(o);
  FrontTouchOutcome copy = *o;
  front_touch_zone_end_pass(z);
  return copy;
}

/* An input pass with no finger down. */
static void pass_empty(FrontTouchZones *z) {
  front_touch_zone_begin_pass(z);
  front_touch_zone_end_pass(z);
}

static void touchpad_touch_survives_edge_and_ps_zone(void) {
  VitakiCtrlMapInfo vcmi = ceo_layout();
  FrontTouchZones z;
  front_touch_zone_init(&z);

  FrontTouchOutcome o = pass(&z, &vcmi, ID_A, MID_X, MID_Y);
  assert(o.to_touchpad && !(o.buttons & VITAKI_CTRL_OUT_PS));

  o = pass(&z, &vcmi, ID_A, EDGE_X, MID_Y); /* slides into an unmapped column */
  assert(o.to_touchpad && o.buttons == 0);

  o = pass(&z, &vcmi, ID_A, MID_X, BOTTOM_Y); /* slides onto the PS zone */
  assert(o.to_touchpad && !(o.buttons & VITAKI_CTRL_OUT_PS));
}

static void button_touch_keeps_its_button_and_never_becomes_touchpad(void) {
  VitakiCtrlMapInfo vcmi = ceo_layout();
  FrontTouchZones z;
  front_touch_zone_init(&z);

  FrontTouchOutcome o = pass(&z, &vcmi, ID_A, MID_X, BOTTOM_Y);
  assert((o.buttons & VITAKI_CTRL_OUT_PS) && !o.to_touchpad);

  o = pass(&z, &vcmi, ID_A, MID_X, MID_Y); /* slides into a Touchpad zone */
  assert((o.buttons & VITAKI_CTRL_OUT_PS) && !o.to_touchpad);
}

static void unmapped_start_presses_nothing_for_its_whole_life(void) {
  VitakiCtrlMapInfo vcmi = ceo_layout();
  FrontTouchZones z;
  front_touch_zone_init(&z);

  FrontTouchOutcome o = pass(&z, &vcmi, ID_A, EDGE_X, MID_Y);
  assert(o.buttons == 0 && !o.to_touchpad);

  o = pass(&z, &vcmi, ID_A, MID_X, MID_Y);
  assert(o.buttons == 0 && !o.to_touchpad);
  o = pass(&z, &vcmi, ID_A, MID_X, BOTTOM_Y);
  assert(o.buttons == 0 && !o.to_touchpad);
}

static void a_new_touch_reusing_the_id_is_judged_from_its_own_start(void) {
  VitakiCtrlMapInfo vcmi = ceo_layout();
  FrontTouchZones z;
  front_touch_zone_init(&z);

  FrontTouchOutcome o = pass(&z, &vcmi, ID_A, MID_X, BOTTOM_Y);
  assert((o.buttons & VITAKI_CTRL_OUT_PS) && !o.to_touchpad);

  pass_empty(&z); /* finger lifted */

  o = pass(&z, &vcmi, ID_A, MID_X, MID_Y); /* same id, new touch on Touchpad */
  assert(o.to_touchpad && !(o.buttons & VITAKI_CTRL_OUT_PS));
}

static void two_fingers_are_latched_independently(void) {
  VitakiCtrlMapInfo vcmi = ceo_layout();
  FrontTouchZones z;
  front_touch_zone_init(&z);

  front_touch_zone_begin_pass(&z);
  const FrontTouchOutcome *a = front_touch_zone_latch(&z, ID_A, MID_X, MID_Y, MAX_W, MAX_H, &vcmi);
  const FrontTouchOutcome *b =
      front_touch_zone_latch(&z, ID_B, MID_X, BOTTOM_Y, MAX_W, MAX_H, &vcmi);
  assert(a && b && a->to_touchpad && (b->buttons & VITAKI_CTRL_OUT_PS));
  front_touch_zone_end_pass(&z);

  /* A lifts, B stays: B keeps PS, A's id is free to start fresh. */
  front_touch_zone_begin_pass(&z);
  b = front_touch_zone_latch(&z, ID_B, MID_X, MID_Y, MAX_W, MAX_H, &vcmi);
  assert(b && (b->buttons & VITAKI_CTRL_OUT_PS) && !b->to_touchpad);
  front_touch_zone_end_pass(&z);
  FrontTouchOutcome o = pass(&z, &vcmi, ID_A, MID_X, BOTTOM_Y);
  assert((o.buttons & VITAKI_CTRL_OUT_PS) && !o.to_touchpad);
}

int main(void) {
  touchpad_touch_survives_edge_and_ps_zone();
  button_touch_keeps_its_button_and_never_becomes_touchpad();
  unmapped_start_presses_nothing_for_its_whole_life();
  a_new_touch_reusing_the_id_is_judged_from_its_own_start();
  two_fingers_are_latched_independently();
  printf("front_touch_zone_tests: all passed\n");
  return 0;
}
