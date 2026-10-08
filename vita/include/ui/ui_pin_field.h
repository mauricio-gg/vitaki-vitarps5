/**
 * @file ui_pin_field.h
 * @brief C17 PinField: eight digit boxes and the Clear digit / Cancel / Register buttons
 *        (SPEC.md C17, section 3.2)
 *
 * Interactive component (SPEC 2.0): a struct plus init, draw and input. One focus only, in one of
 * two zones: the digit boxes, or the buttons below them. The focused box shows an Up and a Down
 * chevron; an empty focused box shows a blinking cursor. The field reports what the screen must
 * do: UI_EVENT_ACTIVATED means Register was pressed with all digits filled, UI_EVENT_CANCELLED
 * means Cancel (the button or the Cancel key); Clear digit and every move are handled inside.
 *
 * While the screen waits for the console the field is locked: digits, Clear digit and Register
 * are drawn at UI_PIN_LOCKED_PCT and ignore input, and only Cancel stays live.
 *
 * Paper cost with the digits zone focused and n boxes typed in: boxes 2 each (outline, digit; the
 * empty ones 1), the focused box 3 more (glow, fill, outline instead of the plain one) and its
 * cursor or chevrons 2, buttons 4 each (8 for the focused one).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui/ui_component.h"
#include "ui/ui_hint_row.h"
#include "ui/ui_pin_digits.h"
#include "ui/ui_text_button.h"
#include "ui/ui_theme.h"

/** The two focus zones. */
typedef enum ui_pin_zone_t {
  UI_PIN_ZONE_DIGITS = 0,
  UI_PIN_ZONE_BUTTONS,
} UiPinZone;

/** The buttons, left to right. */
typedef enum ui_pin_button_t {
  UI_PIN_BUTTON_CLEAR = 0,
  UI_PIN_BUTTON_CANCEL,
  UI_PIN_BUTTON_REGISTER,
} UiPinButton;

typedef struct ui_pin_field_t {
  int8_t digit[UI_PIN_DIGITS];  ///< 0 to 9, or UI_PIN_EMPTY
  int cur;                      ///< focused box (shown only in the digits zone)
  UiPinZone zone;
  UiPinButton button;  ///< focused button (in the buttons zone)
  bool locked;
  uint64_t blink_start_us;  ///< the cursor is on at this moment and every UI_PIN_BLINK_MS after

  UiTextButton buttons[UI_PIN_BTN_COUNT];
  UiRect box[UI_PIN_DIGITS];      ///< digit boxes (visible rect; at least 48 x 48, so also the hit)
  UiRect chev_up[UI_PIN_DIGITS];  ///< the chevron box above each box (visible and hit)
  UiRect chev_down[UI_PIN_DIGITS];  ///< the chevron box below each box
} UiPinField;

/**
 * ui_pin_field_init() - Empty the field, focus the first digit, place every rect and bake the
 * shared chevron art on first use. Call when the PIN screen opens (fonts must be loaded).
 */
void ui_pin_field_init(UiPinField *field);

/**
 * ui_pin_field_set_locked() - Lock the field while the console is being asked (or unlock it).
 * Locking moves the focus to Cancel.
 */
void ui_pin_field_set_locked(UiPinField *field, bool locked);

/** ui_pin_field_is_full() - True when every box holds a digit (Register is available). */
bool ui_pin_field_is_full(const UiPinField *field);

/** ui_pin_field_value() - The PIN as a number (ui_pin_digits_value()), UI_PIN_EMPTY if not full. */
int ui_pin_field_value(const UiPinField *field);

/** ui_pin_field_draw() - Draw the boxes, the focused box's chevrons and cursor, and the buttons. */
void ui_pin_field_draw(const UiPinField *field);

/**
 * ui_pin_field_input() - Move focus, change digits and press buttons (SPEC C17).
 * @return UI_EVENT_ACTIVATED (Register), UI_EVENT_CANCELLED (Cancel), UI_EVENT_MOVED (something
 *         changed), or UI_EVENT_NONE
 */
UiEvent ui_pin_field_input(UiPinField *field, const UiInput *in);

/**
 * ui_pin_field_hints() - Fill @out with the hint row for the field's zone (SPEC 3.2) and return
 * how many. Locked: Cancel only. Labels are string literals.
 */
int ui_pin_field_hints(const UiPinField *field, UiHintItem out[UI_HINT_MAX_ITEMS]);
