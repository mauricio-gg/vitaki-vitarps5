// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the front-touch touchpad click timing (GH #454). The feedback sender
// only sees controller state when it wakes, so the click must be on while a finger is still on the
// pad and stay on for tens of milliseconds. `./tools/build.sh test` only cross-compiles for
// arm-vita-eabi and never executes, so run these natively:
//
//   cc -std=c99 -Wall -Wextra -I vita/include \
//      test/touchpad_gesture_tests.c vita/src/touchpad_gesture.c -o /tmp/touchpad_gesture_tests && \
//      /tmp/touchpad_gesture_tests

#include <assert.h>
#include <stdio.h>

#include "touchpad_gesture.h"

#define FRAME_US 2000ULL /* the 2 ms input loop */
#define MS(n) ((uint64_t)(n) * 1000ULL)

/* Plays the host side: a Chiaki touch is active from the first frame a finger is seen until the
 * tracker releases it, exactly like host_input.c. */
typedef struct sim_t {
  TouchpadGesture g;
  uint64_t now_us;
  bool active[TOUCHPAD_GESTURE_SLOTS];
  bool seen[TOUCHPAD_GESTURE_SLOTS];
  bool moved[TOUCHPAD_GESTURE_SLOTS];
  uint64_t click_on_us;    /* total time the click was on */
  int click_rises;         /* how many separate clicks started */
  bool prev_click;
  bool click_with_no_touch; /* click was on while no touch was active */
  int click_frames_not_both; /* click frames where a slot was inactive or released */
  uint64_t first_release_us[TOUCHPAD_GESTURE_SLOTS];
} Sim;

static void sim_init(Sim *s) {
  *s = (Sim){0};
  touchpad_gesture_init(&s->g);
}

/* Runs one input frame with the current finger state, then advances the clock. */
static void sim_frame(Sim *s) {
  TouchpadGestureInput in[TOUCHPAD_GESTURE_SLOTS];
  TouchpadGestureOutput out;
  for (int i = 0; i < TOUCHPAD_GESTURE_SLOTS; i++) {
    s->active[i] = s->active[i] || s->seen[i];
    in[i] = (TouchpadGestureInput){s->active[i], s->seen[i], s->moved[i]};
  }
  touchpad_gesture_step(&s->g, s->now_us, in, &out);

  if (out.click) {
    s->click_on_us += FRAME_US;
    if (!s->prev_click)
      s->click_rises++;
    /* The click must never be on while every touch is already stopped. */
    bool any_left = false;
    for (int i = 0; i < TOUCHPAD_GESTURE_SLOTS; i++)
      any_left = any_left || (s->active[i] && !out.release[i]);
    if (!any_left)
      s->click_with_no_touch = true;
    for (int i = 0; i < TOUCHPAD_GESTURE_SLOTS; i++) {
      if (!s->active[i] || out.release[i]) {
        s->click_frames_not_both++;
        break;
      }
    }
  }
  s->prev_click = out.click;

  for (int i = 0; i < TOUCHPAD_GESTURE_SLOTS; i++) {
    if (out.release[i]) {
      assert(s->active[i]);
      s->active[i] = false;
      s->moved[i] = false;
      if (!s->first_release_us[i])
        s->first_release_us[i] = s->now_us;
    }
  }
  s->now_us += FRAME_US;
}

static void sim_run_ms(Sim *s, int ms) {
  for (uint64_t end = s->now_us + MS(ms); s->now_us < end;)
    sim_frame(s);
}

/* catches: a tap clicks only after its finger is gone (the bug), so the console misses the click;
 * or the click is a blip too short for the feedback sender to see. */
static void test_single_tap_clicks_with_finger_on_pad_for_50ms(void) {
  Sim s;
  sim_init(&s);
  s.seen[0] = true;
  sim_run_ms(&s, 80); /* finger down, quick tap */
  s.seen[0] = false;
  sim_run_ms(&s, 200);

  assert(s.click_rises == 1);
  assert(s.click_on_us >= MS(50));
  assert(!s.click_with_no_touch);
  assert(!s.active[0]); /* touch let go in the end */
}

/* catches: two fingers tapping together give no click, or two clicks, or a click after one of the
 * touches is already stopped. */
static void test_two_finger_tap_gives_one_click_with_both_touches_active(void) {
  Sim s;
  sim_init(&s);
  s.seen[0] = true;
  sim_run_ms(&s, 20);
  s.seen[1] = true;
  sim_run_ms(&s, 60);
  s.seen[0] = false; /* first lifts, second still down */
  sim_run_ms(&s, 30);
  assert(s.click_on_us == 0); /* no click until the second lifts */
  assert(s.active[0]);        /* first is held, not stopped */
  s.seen[1] = false;
  sim_run_ms(&s, 200);

  assert(s.click_rises == 1);
  assert(s.click_on_us >= MS(50));
  assert(!s.click_with_no_touch);
  assert(s.click_frames_not_both == 0); /* both touches active on every click frame */
  assert(!s.active[0] && !s.active[1]);
}

/* catches: a drag clicks when it lifts, so scrolling a list in a game presses the touchpad. */
static void test_moved_touch_never_clicks(void) {
  Sim s;
  sim_init(&s);
  s.seen[0] = true;
  sim_run_ms(&s, 40);
  s.moved[0] = true;
  sim_run_ms(&s, 40);
  s.seen[0] = false;
  sim_run_ms(&s, 200);

  assert(s.click_rises == 0);
  assert(!s.active[0]);
}

/* catches: a held first finger stays on the pad as a phantom touch when the second finger
 * moves or never lifts. */
static void test_held_touch_is_let_go_without_click(void) {
  Sim s;
  sim_init(&s);
  /* Second finger moves after the first lifted. */
  s.seen[0] = true;
  s.seen[1] = true;
  sim_run_ms(&s, 40);
  s.seen[0] = false;
  sim_run_ms(&s, 20);
  s.moved[1] = true;
  sim_run_ms(&s, 10);
  assert(!s.active[0]);
  s.seen[1] = false;
  sim_run_ms(&s, 200);
  assert(s.click_rises == 0);

  /* Second finger stays down past the lift window. */
  Sim t;
  sim_init(&t);
  t.seen[0] = true;
  t.seen[1] = true;
  sim_run_ms(&t, 40);
  t.seen[0] = false;
  sim_run_ms(&t, 260);
  assert(!t.active[0]);
  assert(t.click_rises == 0);
}

/* catches: a two-finger hold never presses the click (no long press in Witcher/Cyberpunk), presses
 * it before the threshold, keeps it past the first lift or after a touch is already released, or
 * clicks again when the second finger lifts. */
static void test_two_finger_hold_clicks_until_first_lift_only(void) {
  Sim s;
  sim_init(&s);
  s.seen[0] = true;
  sim_run_ms(&s, 20);
  s.seen[1] = true;
  sim_run_ms(&s, 400);
  assert(s.click_on_us == 0); /* not yet held long enough */
  sim_run_ms(&s, 280);
  assert(s.click_on_us >= MS(100)); /* held click is on */
  s.moved[1] = true;                /* moving after the click started does not end it */
  sim_run_ms(&s, 20);
  s.seen[0] = false; /* first finger lifts */
  sim_run_ms(&s, 4);
  uint64_t on_at_lift = s.click_on_us;
  sim_run_ms(&s, 300);
  assert(s.click_on_us == on_at_lift); /* click ended with the lift */
  assert(!s.active[0] && s.active[1]); /* first released, second still on the pad */
  s.seen[1] = false;
  sim_run_ms(&s, 300);

  assert(s.click_rises == 1); /* no tap click from either lift */
  assert(s.click_frames_not_both == 0);
  assert(!s.click_with_no_touch);
  assert(!s.active[1]);
}

/* catches: fingers that moved still trigger the hold click, so a two-finger scroll or pinch presses
 * the touchpad. */
static void test_two_finger_hold_with_movement_never_clicks(void) {
  Sim s;
  sim_init(&s);
  s.seen[0] = true;
  s.seen[1] = true;
  sim_run_ms(&s, 100);
  s.moved[1] = true;
  sim_run_ms(&s, 900);
  assert(s.click_on_us == 0);
}

/* catches: a finger resting alone, or a second finger arriving late, triggers the hold click, which
 * would break games that need a finger resting on the pad. */
static void test_resting_fingers_that_are_not_a_two_finger_press_never_hold(void) {
  Sim s;
  sim_init(&s);
  s.seen[0] = true;
  sim_run_ms(&s, 1500);
  assert(s.click_on_us == 0);

  Sim t;
  sim_init(&t);
  t.seen[0] = true;
  sim_run_ms(&t, 300); /* second finger later than the down window */
  t.seen[1] = true;
  sim_run_ms(&t, 1000);
  assert(t.click_on_us == 0);
}

int main(void) {
  test_single_tap_clicks_with_finger_on_pad_for_50ms();
  test_two_finger_tap_gives_one_click_with_both_touches_active();
  test_moved_touch_never_clicks();
  test_held_touch_is_let_go_without_click();
  test_two_finger_hold_clicks_until_first_lift_only();
  test_two_finger_hold_with_movement_never_clicks();
  test_resting_fingers_that_are_not_a_two_finger_press_never_hold();
  printf("touchpad_gesture_tests: all passed\n");
  return 0;
}
