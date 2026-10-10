/**
 * @file ui_pin_digits.c
 * @brief Pure rules for the 8-digit PIN (see ui_pin_digits.h)
 */

#include "ui/ui_pin_digits.h"

/** Digits 0 to 9: the wrap length, and the number base of the PIN. */
#define DIGIT_COUNT 10

int ui_pin_digit_step(int digit, bool upward) {
  if (digit < 0 || digit >= DIGIT_COUNT)
    return upward ? 0 : DIGIT_COUNT - 1;
  return upward ? (digit + 1) % DIGIT_COUNT : (digit + DIGIT_COUNT - 1) % DIGIT_COUNT;
}

bool ui_pin_digits_full(const int8_t digits[UI_PIN_DIGITS]) {
  for (int i = 0; i < UI_PIN_DIGITS; i++) {
    if (digits[i] < 0 || digits[i] >= DIGIT_COUNT)
      return false;
  }
  return true;
}

int ui_pin_digits_value(const int8_t digits[UI_PIN_DIGITS]) {
  if (!ui_pin_digits_full(digits))
    return UI_PIN_EMPTY;
  int value = 0;
  for (int i = 0; i < UI_PIN_DIGITS; i++)
    value = value * DIGIT_COUNT + digits[i];
  return value;
}
