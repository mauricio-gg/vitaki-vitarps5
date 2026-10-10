/**
 * @file ui_text_button.c
 * @brief C22 TextButton (SPEC.md C22)
 */

#include "ui/ui_text_button.h"

#include <vita2d.h>

#include "ui/ui_animation.h"
#include "ui/ui_shapes.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"

int ui_text_button_width(const char *label) {
  const int w = ui_text_face_width(UI_FACE_T20, label) + 2 * UI_BUTTON_PAD;
  return w > UI_BUTTON_MIN_W ? w : UI_BUTTON_MIN_W;
}

void ui_text_button_init(UiTextButton *btn, const char *label, int x, int y) {
  btn->label = label;
  btn->small = false;
  btn->focused = false;
  btn->disabled = false;
  btn->press_start_us = 0;
  btn->label_w = ui_text_face_width(UI_FACE_T20, label);
  btn->visible = (UiRect){x, y, ui_text_button_width(label), UI_BUTTON_H};
  btn->hit = ui_rect_hit_from_visible(btn->visible, UI_TAP_MIN, UI_TAP_MIN);
}

void ui_text_button_init_small(UiTextButton *btn, const char *label, int x, int y) {
  btn->label = label;
  btn->small = true;
  btn->focused = false;
  btn->disabled = false;
  btn->press_start_us = 0;
  btn->label_w = ui_text_face_width(UI_FACE_T16, label);
  int w = btn->label_w + 2 * UI_BUTTON_SMALL_PAD;
  if (w < UI_BUTTON_SMALL_MIN_W)
    w = UI_BUTTON_SMALL_MIN_W;
  btn->visible = (UiRect){x, y, w, UI_BUTTON_SMALL_H};
  btn->hit = ui_rect_hit_from_visible(btn->visible, UI_TAP_MIN, UI_TAP_MIN);
}

void ui_text_button_set_width(UiTextButton *btn, int w) {
  btn->visible.w = w;
  btn->hit = ui_rect_hit_from_visible(btn->visible, UI_TAP_MIN, UI_TAP_MIN);
}

/** True for UI_BUTTON_PRESS_MS after the button was activated. */
static bool is_pressed(const UiTextButton *btn) {
  return btn->press_start_us != 0 &&
         ui_anim_elapsed_ms(btn->press_start_us) < (float)UI_BUTTON_PRESS_MS;
}

void ui_text_button_draw(const UiTextButton *btn) {
  const float k = btn->disabled ? (float)UI_BUTTON_DISABLED_PCT / 100.0f : 1.0f;
  const bool pressed = is_pressed(btn);
  const UiRect v = btn->visible;
  const UiFace face = btn->small ? UI_FACE_T16 : UI_FACE_T20;
  const int line = btn->small ? UI_T16_LINE : UI_T20_LINE;
  const UiRect text = {v.x + (v.w - btn->label_w) / 2, v.y + (v.h - line) / 2, btn->label_w, line};

  if (btn->focused) {
    ui_glow_draw_rect(text, UI_BUTTON_GLOW,
                      ui_color_scale_alpha(UI_GLOW, (float)UI_BUTTON_GLOW_PCT / 100.0f * k));
  }
  if (pressed || btn->focused) {
    ui_shape3_draw(btn->small ? UI_SHAPE3_PILL_32 : UI_SHAPE3_PILL_48, v.x, v.y, v.w,
                   ui_color_scale_alpha(pressed ? UI_FILL_ON : UI_FILL_FOCUS, k));
  }
  ui_shape3_draw(btn->small ? UI_SHAPE3_PILL_32_OUTLINE : UI_SHAPE3_PILL_48_OUTLINE, v.x, v.y, v.w,
                 ui_color_scale_alpha(btn->focused ? UI_TEXT : UI_LINE, k));
  ui_text_draw_face_centered_v(face, text.x, v.y, v.h,
                               ui_color_scale_alpha(btn->focused ? UI_TEXT : UI_TEXT_2, k),
                               btn->label);
}

UiEvent ui_text_button_input(UiTextButton *btn, const UiInput *in) {
  if (btn->disabled)
    return UI_EVENT_NONE;
  const bool confirmed = btn->focused && (in->pressed & UI_BTN_CONFIRM);
  const bool tapped = ui_touch_tap(in) && ui_rect_contains(btn->hit, in->touch.x, in->touch.y);
  if (!confirmed && !tapped)
    return UI_EVENT_NONE;
  btn->press_start_us = ui_anim_now_us();
  return UI_EVENT_ACTIVATED;
}
