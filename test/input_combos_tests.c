// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the hold-to-fire button combos (GH #474): L + R + Select mutes the
// mic, L + R + Triangle opens the debug Input test panel. Run natively:
//
//   cc -std=c99 -Wall -Wextra -I vita/include test/input_combos_tests.c vita/src/input_combos.c \
//      -o /tmp/input_combos_tests && /tmp/input_combos_tests

#include <assert.h>
#include <stdio.h>

#include "input_combos.h"

#define MS(n) ((uint64_t)(n) * 1000ULL)
#define FRAME_US 2000ULL /* the 2 ms input loop */

enum { L = 1, R = 2, SELECT = 4, START = 8, TRIANGLE = 16, CROSS = 32 };
static const InputComboButtons MAP = {.l = L, .r = R, .select = SELECT, .start = START,
                                      .triangle = TRIANGLE};

typedef struct sim_t {
  InputCombos combos;
  uint64_t now_us;
  int mute_fires;
  int panel_fires;
  unsigned last_out; /* buttons the game saw on the last pass */
} Sim;

/* Holds @buttons for @ms milliseconds, one pass per input frame, counting the fires. */
static void hold(Sim *s, unsigned buttons, int ms, bool mute_active, bool panel_enabled) {
  const uint64_t end_us = s->now_us + MS(ms);
  for (; s->now_us < end_us; s->now_us += FRAME_US) {
    InputComboResult r;
    s->last_out = input_combos_step(&s->combos, &MAP, buttons, s->now_us, mute_active,
                                    panel_enabled, &r);
    s->mute_fires += r.mute_fired;
    s->panel_fires += r.panel_fired;
  }
}

/* catches: L + R + Select no longer muting (or muting before the hold completes, or again and again
 * while still held), or Select leaking to the game as Share while the combo is held. */
static void test_mute_combo_fires_once_after_the_hold(void) {
  Sim s = {.now_us = MS(1000)};
  hold(&s, L | R | SELECT, 400, true, false);
  assert(s.mute_fires == 0);
  assert(s.last_out == 0); /* hidden from the game from the first pass */
  hold(&s, L | R | SELECT, 300, true, false);
  assert(s.mute_fires == 1);
  hold(&s, L | R | SELECT, 2000, true, false);
  assert(s.mute_fires == 1); /* must be released before it can fire again */
  hold(&s, 0, 20, true, false);
  hold(&s, L | R | SELECT, 600, true, false);
  assert(s.mute_fires == 2);
}

/* catches: the old combo still opening the panel (L + R + Select) and the new one doing nothing. */
static void test_panel_combo_is_triangle_not_select(void) {
  Sim s = {.now_us = MS(1000)};
  hold(&s, L | R | SELECT, 1000, false, true);
  assert(s.panel_fires == 0);
  hold(&s, 0, 20, false, true);
  hold(&s, L | R | TRIANGLE, 600, false, true);
  assert(s.panel_fires == 1);
  assert(s.mute_fires == 0);
}

/* catches: the mute combo firing, or swallowing L, R and Select, when the mic is off or failed;
 * and the panel combo existing in a release build. */
static void test_combos_are_watched_only_when_active(void) {
  Sim s = {.now_us = MS(1000)};
  hold(&s, L | R | SELECT, 800, false, false);
  assert(s.mute_fires == 0);
  assert(s.last_out == (L | R | SELECT)); /* all three reach the game */
  hold(&s, L | R | TRIANGLE, 800, true, false);
  assert(s.panel_fires == 0);
  assert(s.last_out == (L | R | TRIANGLE));
}

/* catches: the mute or panel combo firing during the stream-exit combo (L + R + Start), which would
 * toggle the mic or the panel as the player leaves. */
static void test_start_down_vetoes_both_combos(void) {
  Sim s = {.now_us = MS(1000)};
  hold(&s, L | R | SELECT | START, 800, true, true);
  hold(&s, L | R | TRIANGLE | START, 800, true, true);
  assert(s.mute_fires == 0 && s.panel_fires == 0);
  assert(s.last_out == (L | R | TRIANGLE | START));
}

/* catches: other buttons held with a combo being swallowed along with it. */
static void test_other_buttons_are_left_alone(void) {
  Sim s = {.now_us = MS(1000)};
  hold(&s, L | R | SELECT | CROSS, 100, true, false);
  assert(s.last_out == CROSS);
}

int main(void) {
  test_mute_combo_fires_once_after_the_hold();
  test_panel_combo_is_triangle_not_select();
  test_combos_are_watched_only_when_active();
  test_start_down_vetoes_both_combos();
  test_other_buttons_are_left_alone();
  printf("input_combos_tests: all passed\n");
  return 0;
}
