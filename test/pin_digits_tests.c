// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the PIN rules in vita/src/ui/ui_pin_digits.c (issue #303).
// `./tools/build.sh test` only cross-compiles, so run these on the host:
//   cc -std=c99 -Wall -Wextra -I vita/include test/pin_digits_tests.c \
//      vita/src/ui/ui_pin_digits.c -o /tmp/pin_digits_tests && /tmp/pin_digits_tests

#include <assert.h>
#include <stdio.h>

#include "ui/ui_pin_digits.h"

/** The console shows 8 digits and many start with 0: a PIN typed as 00001234 must reach
 * host_register() as the number 1234, not be refused or shifted. */
static void test_value_keeps_leading_zeros_and_order(void) {
  const int8_t zeros_first[UI_PIN_DIGITS] = {0, 0, 0, 0, 1, 2, 3, 4};
  const int8_t all_nines[UI_PIN_DIGITS] = {9, 9, 9, 9, 9, 9, 9, 9};
  assert(ui_pin_digits_value(zeros_first) == 1234);
  assert(ui_pin_digits_value(all_nines) == 99999999);
}

/** Register must stay unavailable while any box is empty, so a half-typed PIN can never be sent. */
static void test_a_partial_pin_is_not_full_and_has_no_value(void) {
  int8_t digits[UI_PIN_DIGITS] = {4, 8, 2, 7, 1, 9, 3, 6};
  assert(ui_pin_digits_full(digits));
  digits[5] = UI_PIN_EMPTY;
  assert(!ui_pin_digits_full(digits));
  assert(ui_pin_digits_value(digits) == UI_PIN_EMPTY);
}

/** Up and Down on a box: an empty box starts at 0 (Up) or 9 (Down) and the digits wrap. */
static void test_digit_step_starts_and_wraps(void) {
  assert(ui_pin_digit_step(UI_PIN_EMPTY, true) == 0);
  assert(ui_pin_digit_step(UI_PIN_EMPTY, false) == 9);
  assert(ui_pin_digit_step(9, true) == 0);
  assert(ui_pin_digit_step(0, false) == 9);
  assert(ui_pin_digit_step(4, true) == 5);
  assert(ui_pin_digit_step(4, false) == 3);
}

int main(void) {
  test_value_keeps_leading_zeros_and_order();
  test_a_partial_pin_is_not_full_and_has_no_value();
  test_digit_step_starts_and_wraps();
  printf("pin_digits_tests: all passed\n");
  return 0;
}
