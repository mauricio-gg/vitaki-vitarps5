// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) test for the splash timeline in vita/src/ui/ui_splash_timeline.c (ticket
// #398). `./tools/build.sh test` only cross-compiles, so run it on the host:
//   cc -std=c99 -I vita/include test/ui_splash_timeline_tests.c vita/src/ui/ui_splash_timeline.c \
//      vita/src/ui/ui_splash_particles.c -lm -o /tmp/ui_splash_timeline_tests \
//      && /tmp/ui_splash_timeline_tests

#include <assert.h>
#include <stdio.h>

#include "ui/ui_splash_timeline.h"

/* Same numbers as the UI_SPLASH_* timeline block in ui_theme.h. */
static const UiSplashTiming T = {
    .logo_in_ms = 450.0f,
    .logo_scale0 = 0.97f,
    .min_hold_ms = 1000.0f,
    .dissolve_ms = 1020.0f,
    .ui_start_ms = 400.0f,
    .ui_fade_ms = 300.0f,
};

/* A fast load must not flash the logo: the dissolve waits for the minimum hold; a slow one waits
 * for the load. */
static void test_dissolve_start(void) {
  assert(ui_splash_dissolve_start_ms(&T, 300.0f) == 1000.0f);
  assert(ui_splash_dissolve_start_ms(&T, 2400.0f) == 2400.0f);
}

/* Home must not take input before D + 0.70 s, and the dissolve clock must run the #366 clock
 * backwards from its full length to 0 at D + 1.02 s. */
static void test_home_gate_and_clock(void) {
  assert(!ui_splash_timeline_home_input_enabled(&T, 0.0f));
  assert(!ui_splash_timeline_home_input_enabled(&T, 699.0f));
  assert(ui_splash_timeline_home_input_enabled(&T, 700.0f));
  assert(ui_splash_timeline_home_opacity(&T, 400.0f) == 0.0f);
  assert(ui_splash_timeline_home_opacity(&T, 401.0f) > 0.0f);
  assert(ui_splash_timeline_home_opacity(&T, 700.0f) == 1.0f);
  assert(ui_splash_dissolve_clock_ms(&T, -50.0f) == 1020.0f);
  assert(ui_splash_dissolve_clock_ms(&T, 20.0f) == 1000.0f);
  assert(ui_splash_dissolve_clock_ms(&T, 1020.0f) == 0.0f);
  assert(!ui_splash_dissolve_done(&T, 1019.0f));
  assert(ui_splash_dissolve_done(&T, 1020.0f));
}

/* A skip may pull D forward, but never into the logo's fade-in and never push it back, and a press
 * during the dissolve changes nothing. */
static void test_skip(void) {
  assert(ui_splash_skip_dissolve_start_ms(&T, 200.0f, 2400.0f) == 450.0f);
  assert(ui_splash_skip_dissolve_start_ms(&T, 700.0f, 2400.0f) == 700.0f);
  assert(ui_splash_skip_dissolve_start_ms(&T, 1200.0f, 1000.0f) == 1000.0f);
  assert(ui_splash_skip_dissolve_start_ms(&T, 2600.0f, 2400.0f) == 2400.0f);
}

int main(void) {
  test_dissolve_start();
  test_home_gate_and_clock();
  test_skip();
  puts("ui_splash_timeline_tests: ok");
  return 0;
}
