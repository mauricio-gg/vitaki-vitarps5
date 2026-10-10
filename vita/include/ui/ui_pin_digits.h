/**
 * @file ui_pin_digits.h
 * @brief Pure rules for the 8-digit PIN the PIN screen collects (SPEC.md C17)
 *
 * No Vita dependency, so the rules can be checked natively (test/pin_digits_tests.c).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Digits in a session PIN. */
#define UI_PIN_DIGITS 8

/** The value of a box nobody has typed in yet. */
#define UI_PIN_EMPTY (-1)

/**
 * ui_pin_digit_step() - The digit after one Up or Down press on a box.
 * @digit:  The box's digit 0 to 9, or UI_PIN_EMPTY.
 * @upward: true for Up (next digit), false for Down (previous digit).
 *
 * An empty box starts at 0 on Up and at 9 on Down; digits wrap (Up on 9 gives 0, Down on 0
 * gives 9).
 */
int ui_pin_digit_step(int digit, bool upward);

/** ui_pin_digits_full() - True when every one of the UI_PIN_DIGITS boxes holds a digit. */
bool ui_pin_digits_full(const int8_t digits[UI_PIN_DIGITS]);

/**
 * ui_pin_digits_value() - The PIN as a number, first box most significant.
 *
 * Leading zeros are part of the PIN: 0 0 0 0 1 2 3 4 is 1234, which the console reads as
 * 00001234. Returns UI_PIN_EMPTY when a box is still empty.
 */
int ui_pin_digits_value(const int8_t digits[UI_PIN_DIGITS]);
