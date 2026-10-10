/**
 * @file ui_text_button.h
 * @brief C22 TextButton: a 48 px pill button with a label (SPEC.md C22)
 *
 * Interactive component (SPEC 2.0): a struct plus init, draw and input. The pill has a 1 px
 * LINE border and no fill; focused it gets FILL_FOCUS, a white border and a glow; for
 * UI_BUTTON_PRESS_MS after it is activated it shows FILL_ON; disabled it is drawn at 45%.
 * Paper cost: label 1 + border 3, and with a fill 3 more and the glow 1.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui/ui_component.h"

typedef struct ui_text_button_t {
  const char *label;        ///< borrowed; must outlive the button
  bool focused;             ///< set by the screen that owns the button
  bool small;               ///< the 32 px footer variant (ui_text_button_init_small)
  bool disabled;            ///< drawn at UI_BUTTON_DISABLED_PCT and ignores input
  uint64_t press_start_us;  ///< when the button was activated; 0 when it never was
  int label_w;              ///< label width, measured once at init
  UiRect visible;           ///< the drawn pill
  UiRect hit;               ///< the touch rect: the pill, at least UI_TAP_MIN square
} UiTextButton;

/** ui_text_button_width() - Width of a button labelled @label: its text plus padding, at least
 * UI_BUTTON_MIN_W. Use it to right-align a button. */
int ui_text_button_width(const char *label);

/** ui_text_button_init() - Set @btn up with its top-left corner at (@x, @y); not focused. */
void ui_text_button_init(UiTextButton *btn, const char *label, int x, int y);

/**
 * ui_text_button_init_small() - Set @btn up as the small variant (SPEC 4.1): a 32 px pill with a
 * T16 label, at least UI_BUTTON_SMALL_MIN_W wide, with its top-left corner at (@x, @y). Its hit
 * rect is UI_TAP_MIN high. Draw and input are the same calls as for the 48 px button.
 */
void ui_text_button_init_small(UiTextButton *btn, const char *label, int x, int y);

/**
 * ui_text_button_set_width() - Make the pill @w wide (a popup's button bar shares its width
 * between the buttons). The label stays centred; the hit rect is recomputed.
 */
void ui_text_button_set_width(UiTextButton *btn, int w);

/** ui_text_button_draw() - Draw the button. No state change. */
void ui_text_button_draw(const UiTextButton *btn);

/**
 * ui_text_button_input() - Activate on Confirm while focused, or on a tap on its hit rect.
 * @return UI_EVENT_ACTIVATED when activated, otherwise UI_EVENT_NONE.
 */
UiEvent ui_text_button_input(UiTextButton *btn, const UiInput *in);
