// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the Controller page rule in vita/src/ui/ui_controller_rules.c
// (ticket #305): the common output of a set of inputs, which the mapping popup ticks, or Mixed.
// `./tools/build.sh test` only cross-compiles, so run these on the host:
//   cc -std=c99 -Wall -Wextra -I vita/include test/ui_controller_rules_tests.c \
//      vita/src/ui/ui_controller_rules.c -o /tmp/ui_controller_rules_tests && \
//      /tmp/ui_controller_rules_tests

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "ui/ui_controller_rules.h"

/* Output values as the app uses them: None is 0, L2 is 3, Touchpad is a larger id. */
enum { OUT_NONE = 0, OUT_L2 = 3, OUT_TOUCHPAD = 12 };

/**
 * Zones that all hold one output read as that output, and a set that is all None reads as None
 * (the None row is ticked), not as Mixed.
 * catches: the whole-surface popup showing "Mixed" and ticking nothing for a side that is
 * uniformly mapped or uniformly cleared.
 */
static void test_uniform_set_has_a_common_output(void) {
  const int touchpad[] = {OUT_TOUCHPAD, OUT_TOUCHPAD, OUT_TOUCHPAD};
  assert(ui_controller_common_output(touchpad, 3) == OUT_TOUCHPAD);

  const int cleared[] = {OUT_NONE, OUT_NONE};
  assert(ui_controller_common_output(cleared, 2) == OUT_NONE);

  const int single[] = {OUT_L2};
  assert(ui_controller_common_output(single, 1) == OUT_L2);
}

/**
 * One zone that differs, wherever it sits in the set, makes the set Mixed.
 * catches: a set whose first zones agree being ticked as if all were the same (the popup would
 * then show a value that only some zones have), or a lone zone with None among mapped ones being
 * shown as the mapped value.
 */
static void test_any_difference_is_mixed(void) {
  const int last_differs[] = {OUT_L2, OUT_L2, OUT_NONE};
  assert(ui_controller_common_output(last_differs, 3) == UI_CTRL_MIXED);

  const int first_differs[] = {OUT_NONE, OUT_L2, OUT_L2};
  assert(ui_controller_common_output(first_differs, 3) == UI_CTRL_MIXED);

  const int middle_differs[] = {OUT_L2, OUT_TOUCHPAD, OUT_L2};
  assert(ui_controller_common_output(middle_differs, 3) == UI_CTRL_MIXED);
}

/**
 * An empty or missing set has no common output and must not read past its end.
 * catches: a crash or a bogus tick when the popup is opened on a selection that turned out empty.
 */
static void test_empty_set_is_mixed(void) {
  const int none[] = {OUT_NONE};
  assert(ui_controller_common_output(none, 0) == UI_CTRL_MIXED);
  assert(ui_controller_common_output(NULL, 3) == UI_CTRL_MIXED);
}

int main(void) {
  test_uniform_set_has_a_common_output();
  test_any_difference_is_mixed();
  test_empty_set_is_mixed();
  printf("ui_controller_rules_tests: all passed\n");
  return 0;
}
