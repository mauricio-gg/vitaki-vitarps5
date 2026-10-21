// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the top bar Wi-Fi bar level rule (GH #428): the thresholds at the
// borders, and that a reading hovering on a border does not make the icon flicker.
// `./tools/build.sh test` only cross-compiles for arm-vita-eabi and never executes, so run
// these natively:
//
//   cc -std=c99 -Wall -Wextra -I vita/include \
//      test/ui_wifi_level_tests.c vita/src/ui/ui_wifi_level.c -o /tmp/ui_wifi_level_tests && \
//      /tmp/ui_wifi_level_tests

#include <assert.h>
#include <stdio.h>

#include "ui/ui_wifi_level.h"

/* With no previous level, the plain thresholds decide: both sides of every border. */
static void test_plain_thresholds_without_previous_level(void) {
  const int rssi[] = {100, 60, 59, 45, 44, 25, 24, 0};
  const int level[] = {3, 3, 2, 2, 1, 1, 0, 0};
  for (int i = 0; i < 8; i++)
    assert(ui_wifi_level(rssi[i], UI_WIFI_LEVEL_NONE) == level[i]);
}

/* Falling, a level is kept down to 5 points below its entry (55, 40, 20) and left one point
 * further down. Rising, the plain entry thresholds apply. */
static void test_falling_holds_then_drops_rising_enters_plain(void) {
  assert(ui_wifi_level(55, 3) == 3);
  assert(ui_wifi_level(54, 3) == 2);
  assert(ui_wifi_level(40, 2) == 2);
  assert(ui_wifi_level(39, 2) == 1);
  assert(ui_wifi_level(20, 1) == 1);
  assert(ui_wifi_level(19, 1) == 0);

  assert(ui_wifi_level(59, 2) == 2);
  assert(ui_wifi_level(60, 2) == 3);
  assert(ui_wifi_level(44, 1) == 1);
  assert(ui_wifi_level(45, 1) == 2);
  assert(ui_wifi_level(24, 0) == 0);
  assert(ui_wifi_level(25, 0) == 1);
}

/* A reading bouncing across a border settles on one level instead of toggling every second. */
static void test_hovering_on_a_border_does_not_flicker(void) {
  const int readings[] = {61, 58, 59, 57, 60, 56, 58};
  int level = UI_WIFI_LEVEL_NONE;
  for (int i = 0; i < 7; i++) {
    level = ui_wifi_level(readings[i], level);
    assert(level == 3);
  }
}

/* One reading may skip levels in either direction, and the result is still right. */
static void test_multi_level_jumps(void) {
  assert(ui_wifi_level(10, 3) == 0);
  assert(ui_wifi_level(30, 3) == 1);
  assert(ui_wifi_level(80, 0) == 3);
  assert(ui_wifi_level(50, 0) == 2);
}

int main(void) {
  test_plain_thresholds_without_previous_level();
  test_falling_holds_then_drops_rising_enters_plain();
  test_hovering_on_a_border_does_not_flicker();
  test_multi_level_jumps();
  printf("ui_wifi_level_tests: all passed\n");
  return 0;
}
