/**
 * @file ui_pin_field.c
 * @brief C17 PinField (SPEC.md C17, section 3.2)
 */

#include "ui/ui_pin_field.h"

#include <stdbool.h>

#include <vita2d.h>

#include "ui/ui_animation.h"
#include "ui/ui_bake.h"
#include "ui/ui_constants.h"
#include "ui/ui_shapes.h"
#include "ui/ui_text.h"

/** Chevrons are drawn in the mock's 24 x 24 grid: the Up one is (5,15) (12,8) (19,15). */
#define CHEVRON_GRID 24.0f
#define CHEVRON_BACK_Y 15.0f
#define CHEVRON_TIP_Y 8.0f
#define CHEVRON_LEFT_X 5.0f
#define CHEVRON_MID_X 12.0f
#define CHEVRON_RIGHT_X 19.0f

/** Digits 0 to 9 as strings, for drawing without formatting. */
#define DIGIT_VALUES 10
static const char *const DIGIT_TEXT[DIGIT_VALUES] = {"0", "1", "2", "3", "4",
                                                     "5", "6", "7", "8", "9"};

static const char LABEL_CLEAR[] = "Clear digit";
static const char LABEL_CANCEL[] = "Cancel";
static const char LABEL_REGISTER[] = "Register";
static const char HINT_DIGIT[] = "Digit";
static const char HINT_CHANGE[] = "Change";
static const char HINT_BUTTON[] = "Button";
static const char HINT_DIGITS[] = "Digits";

static const char *const BUTTON_LABELS[UI_PIN_BTN_COUNT] = {
    [UI_PIN_BUTTON_CLEAR] = LABEL_CLEAR,
    [UI_PIN_BUTTON_CANCEL] = LABEL_CANCEL,
    [UI_PIN_BUTTON_REGISTER] = LABEL_REGISTER,
};

static vita2d_texture *s_chevron_up = NULL;
static vita2d_texture *s_chevron_down = NULL;
/** Width of each digit in the T40 face, measured in ui_pin_field_init(). */
static int s_digit_w[DIGIT_VALUES];

/** Alpha of a chevron; @ctx points to a bool, true for the Up chevron. */
static float chevron_alpha(float px, float py, const void *ctx) {
  const bool up = *(const bool *)ctx;
  const float scale = (float)UI_PIN_CHEV_ART / CHEVRON_GRID;
  const float back_y = (up ? CHEVRON_BACK_Y : CHEVRON_GRID - CHEVRON_BACK_Y) * scale;
  const float tip_y = (up ? CHEVRON_TIP_Y : CHEVRON_GRID - CHEVRON_TIP_Y) * scale;
  const float d1 = ui_bake_distance_to_segment(px, py, CHEVRON_LEFT_X * scale, back_y,
                                               CHEVRON_MID_X * scale, tip_y);
  const float d2 = ui_bake_distance_to_segment(px, py, CHEVRON_MID_X * scale, tip_y,
                                               CHEVRON_RIGHT_X * scale, back_y);
  const float d = d1 < d2 ? d1 : d2;
  return UI_PIN_CHEV_STROKE / 2.0f + 0.5f - d;
}

/** True when box @i is focused: the digits zone, and not while locked. */
static bool box_focused(const UiPinField *f, int i) {
  return f->zone == UI_PIN_ZONE_DIGITS && !f->locked && i == f->cur;
}

/** Bring the buttons' focus and disabled flags in line with the field's state. */
static void sync_buttons(UiPinField *f) {
  const bool full = ui_pin_field_is_full(f);
  for (int i = 0; i < UI_PIN_BTN_COUNT; i++) {
    f->buttons[i].focused = f->zone == UI_PIN_ZONE_BUTTONS && (int)f->button == i;
    f->buttons[i].disabled = i == UI_PIN_BUTTON_CANCEL  ? false
                             : i == UI_PIN_BUTTON_CLEAR ? f->locked
                                                        : (f->locked || !full);
  }
}

/** Restart the cursor blink so it is on right after a move or an edit. */
static void restart_blink(UiPinField *f) {
  f->blink_start_us = ui_anim_now_us();
}

void ui_pin_field_init(UiPinField *f) {
  static const bool UP = true;
  static const bool DOWN = false;
  if (!s_chevron_up) {
    s_chevron_up = ui_bake_white(UI_PIN_CHEV_ART, UI_PIN_CHEV_ART, chevron_alpha, &UP);
    s_chevron_down = ui_bake_white(UI_PIN_CHEV_ART, UI_PIN_CHEV_ART, chevron_alpha, &DOWN);
  }
  for (int d = 0; d < DIGIT_VALUES; d++)
    s_digit_w[d] = ui_text_face_width(UI_FACE_T40, DIGIT_TEXT[d]);

  *f = (UiPinField){0};
  for (int i = 0; i < UI_PIN_DIGITS; i++)
    f->digit[i] = UI_PIN_EMPTY;
  f->zone = UI_PIN_ZONE_DIGITS;
  f->button = UI_PIN_BUTTON_REGISTER;
  restart_blink(f);

  /* The row of boxes is centred; the chevron boxes sit above and below each. */
  const int row_w = UI_PIN_DIGITS * UI_PIN_BOX_W + (UI_PIN_DIGITS - 1) * UI_PIN_GAP;
  const int x0 = (VITA_WIDTH - row_w) / 2;
  const int box_y = UI_PIN_ROW_Y + UI_PIN_CHEV_H;
  for (int i = 0; i < UI_PIN_DIGITS; i++) {
    const int x = x0 + i * (UI_PIN_BOX_W + UI_PIN_GAP);
    f->box[i] = (UiRect){x, box_y, UI_PIN_BOX_W, UI_PIN_BOX_H};
    f->chev_up[i] = (UiRect){x, UI_PIN_ROW_Y, UI_PIN_CHEV_W, UI_PIN_CHEV_H};
    f->chev_down[i] = (UiRect){x, box_y + UI_PIN_BOX_H, UI_PIN_CHEV_W, UI_PIN_CHEV_H};
  }

  /* The buttons are centred as a group. */
  int total_w = UI_PIN_BTN_GAP * (UI_PIN_BTN_COUNT - 1);
  for (int i = 0; i < UI_PIN_BTN_COUNT; i++)
    total_w += ui_text_button_width(BUTTON_LABELS[i]);
  int x = (VITA_WIDTH - total_w) / 2;
  for (int i = 0; i < UI_PIN_BTN_COUNT; i++) {
    ui_text_button_init(&f->buttons[i], BUTTON_LABELS[i], x, UI_PIN_BTN_Y);
    x += f->buttons[i].visible.w + UI_PIN_BTN_GAP;
  }
  sync_buttons(f);
}

void ui_pin_field_set_locked(UiPinField *f, bool locked) {
  f->locked = locked;
  if (locked) {
    f->zone = UI_PIN_ZONE_BUTTONS;
    f->button = UI_PIN_BUTTON_CANCEL;
  }
  sync_buttons(f);
}

bool ui_pin_field_is_full(const UiPinField *f) {
  return ui_pin_digits_full(f->digit);
}

int ui_pin_field_value(const UiPinField *f) {
  return ui_pin_digits_value(f->digit);
}

/* ============================================================================
 * Draw
 * ============================================================================ */

/** Draw box @i: its outline or its focus look, its digit or cursor. */
static void draw_box(const UiPinField *f, int i, float k) {
  const UiRect b = f->box[i];
  const bool focused = box_focused(f, i);

  if (focused) {
    ui_glow_draw_rect(b, UI_PIN_GLOW,
                      ui_color_scale_alpha(UI_GLOW, (float)UI_PIN_GLOW_PCT / 100.0f));
    ui_shape1_draw(UI_SHAPE1_PIN_BOX, b.x, b.y, UI_FILL_FOCUS);
  }
  ui_shape1_draw(UI_SHAPE1_PIN_BOX_BORDER, b.x, b.y,
                 ui_color_scale_alpha(focused ? UI_TEXT : UI_LINE, k));

  const int digit = f->digit[i];
  if (digit != UI_PIN_EMPTY) {
    ui_text_draw_face_centered_v(UI_FACE_T40, b.x + (b.w - s_digit_w[digit]) / 2, b.y, b.h,
                                 ui_color_scale_alpha(focused ? UI_TEXT : UI_TEXT_2, k),
                                 DIGIT_TEXT[digit]);
  } else if (focused) {
    const bool on =
        (int)ui_anim_elapsed_ms(f->blink_start_us) % UI_PIN_BLINK_MS < UI_PIN_BLINK_MS / 2;
    vita2d_draw_rectangle(
        (float)(b.x + (b.w - UI_PIN_CURSOR_W) / 2), (float)(b.y + (b.h - UI_PIN_CURSOR_H) / 2),
        (float)UI_PIN_CURSOR_W, (float)UI_PIN_CURSOR_H,
        ui_layer_color(on ? UI_TEXT
                          : ui_color_scale_alpha(UI_TEXT, (float)UI_PIN_CURSOR_LOW_PCT / 100.0f)));
  }
}

/** Draw a chevron centred in its box @r. */
static void draw_chevron(vita2d_texture *tex, UiRect r) {
  if (!tex)
    return;
  vita2d_draw_texture_tint(tex, (float)(r.x + (r.w - UI_PIN_CHEV_ART) / 2),
                           (float)(r.y + (r.h - UI_PIN_CHEV_ART) / 2), ui_layer_color(UI_TEXT_2));
}

void ui_pin_field_draw(const UiPinField *f) {
  const float k = f->locked ? (float)UI_PIN_LOCKED_PCT / 100.0f : 1.0f;
  for (int i = 0; i < UI_PIN_DIGITS; i++)
    draw_box(f, i, k);
  if (f->zone == UI_PIN_ZONE_DIGITS && !f->locked) {
    draw_chevron(s_chevron_up, f->chev_up[f->cur]);
    draw_chevron(s_chevron_down, f->chev_down[f->cur]);
  }
  for (int i = 0; i < UI_PIN_BTN_COUNT; i++)
    ui_text_button_draw(&f->buttons[i]);
}

/* ============================================================================
 * Input
 * ============================================================================ */

/** Step the focused digit up or down. */
static void step_digit(UiPinField *f, bool upward) {
  f->digit[f->cur] = (int8_t)ui_pin_digit_step(f->digit[f->cur], upward);
}

/** Clear the focused digit and put the focus back on the digits. */
static void clear_digit(UiPinField *f) {
  f->digit[f->cur] = UI_PIN_EMPTY;
  f->zone = UI_PIN_ZONE_DIGITS;
}

/** The enabled button next to @from in @dir (-1 or +1), or @from when there is none. */
static UiPinButton neighbour_button(const UiPinField *f, UiPinButton from, int dir) {
  for (int i = (int)from + dir; i >= 0 && i < UI_PIN_BTN_COUNT; i += dir) {
    if (!f->buttons[i].disabled)
      return (UiPinButton)i;
  }
  return from;
}

/** Handle a tap on a box or on the focused box's chevrons. Returns true when it hit one. */
static bool tap_digits(UiPinField *f, float x, float y) {
  if (f->zone == UI_PIN_ZONE_DIGITS) {
    if (ui_rect_contains(f->chev_up[f->cur], x, y)) {
      step_digit(f, true);
      return true;
    }
    if (ui_rect_contains(f->chev_down[f->cur], x, y)) {
      step_digit(f, false);
      return true;
    }
  }
  for (int i = 0; i < UI_PIN_DIGITS; i++) {
    if (ui_rect_contains(f->box[i], x, y)) {
      f->cur = i;
      f->zone = UI_PIN_ZONE_DIGITS;
      return true;
    }
  }
  return false;
}

/** D-pad, Square and Confirm in the digits zone. */
static UiEvent keys_in_digits(UiPinField *f, const UiInput *in) {
  if (in->repeat & UI_BTN_LEFT) {
    if (f->cur > 0)
      f->cur--;
  } else if (in->repeat & UI_BTN_RIGHT) {
    if (f->cur < UI_PIN_DIGITS - 1) {
      f->cur++;
    } else if (ui_pin_field_is_full(f)) {
      f->zone = UI_PIN_ZONE_BUTTONS;
      f->button = UI_PIN_BUTTON_REGISTER;
    }
  } else if (in->repeat & UI_BTN_UP) {
    step_digit(f, true);
  } else if (in->repeat & UI_BTN_DOWN) {
    step_digit(f, false);
  } else if (in->pressed & UI_BTN_CLEAR) {
    f->digit[f->cur] = UI_PIN_EMPTY;
  } else if ((in->pressed & UI_BTN_CONFIRM) && ui_pin_field_is_full(f)) {
    return UI_EVENT_ACTIVATED;
  } else {
    return UI_EVENT_NONE;
  }
  return UI_EVENT_MOVED;
}

/** D-pad in the buttons zone; Confirm is the focused button's own input. */
static UiEvent keys_in_buttons(UiPinField *f, const UiInput *in) {
  if (in->repeat & UI_BTN_LEFT) {
    f->button = neighbour_button(f, f->button, -1);
  } else if (in->repeat & UI_BTN_RIGHT) {
    f->button = neighbour_button(f, f->button, +1);
  } else if (in->repeat & UI_BTN_UP) {
    f->zone = UI_PIN_ZONE_DIGITS;
    f->cur = UI_PIN_DIGITS - 1;
  } else {
    return UI_EVENT_NONE;
  }
  return UI_EVENT_MOVED;
}

/** What pressing button @which means for the screen. */
static UiEvent press_button(UiPinField *f, UiPinButton which) {
  switch (which) {
    case UI_PIN_BUTTON_CLEAR:
      clear_digit(f);
      return UI_EVENT_MOVED;
    case UI_PIN_BUTTON_CANCEL:
      return UI_EVENT_CANCELLED;
    default:
      return UI_EVENT_ACTIVATED;
  }
}

UiEvent ui_pin_field_input(UiPinField *f, const UiInput *in) {
  if (in->pressed & UI_BTN_CANCEL)
    return UI_EVENT_CANCELLED;

  sync_buttons(f);
  for (int i = 0; i < UI_PIN_BTN_COUNT; i++) {
    if (ui_text_button_input(&f->buttons[i], in) == UI_EVENT_ACTIVATED) {
      const UiEvent ev = press_button(f, (UiPinButton)i);
      sync_buttons(f);
      restart_blink(f);
      return ev;
    }
  }
  if (f->locked)
    return UI_EVENT_NONE;

  UiEvent ev = UI_EVENT_NONE;
  if (ui_touch_tap(in)) {
    if (tap_digits(f, in->touch.x, in->touch.y))
      ev = UI_EVENT_MOVED;
  } else if (f->zone == UI_PIN_ZONE_DIGITS) {
    ev = keys_in_digits(f, in);
  } else {
    ev = keys_in_buttons(f, in);
  }
  if (ev != UI_EVENT_NONE) {
    sync_buttons(f);
    restart_blink(f);
  }
  return ev;
}

int ui_pin_field_hints(const UiPinField *f, UiHintItem out[UI_HINT_MAX_ITEMS]) {
  int n = 0;
  if (f->locked) {
    out[n++] = (UiHintItem){.action = UI_BTN_CANCEL, .label = LABEL_CANCEL};
    return n;
  }
  if (f->zone == UI_PIN_ZONE_BUTTONS) {
    out[n++] = (UiHintItem){.action = UI_BTN_LEFT | UI_BTN_RIGHT, .label = HINT_BUTTON};
    out[n++] = (UiHintItem){.action = UI_BTN_UP | UI_BTN_DOWN, .label = HINT_DIGITS};
    out[n++] = (UiHintItem){.action = UI_BTN_CONFIRM, .label = BUTTON_LABELS[f->button]};
    out[n++] = (UiHintItem){.action = UI_BTN_CANCEL, .label = LABEL_CANCEL};
    return n;
  }
  out[n++] = (UiHintItem){.action = UI_BTN_LEFT | UI_BTN_RIGHT, .label = HINT_DIGIT};
  out[n++] = (UiHintItem){.action = UI_BTN_UP | UI_BTN_DOWN, .label = HINT_CHANGE};
  out[n++] = (UiHintItem){.action = UI_BTN_CLEAR, .label = LABEL_CLEAR, .low_priority = true};
  out[n++] = (UiHintItem){
      .action = UI_BTN_CONFIRM, .label = LABEL_REGISTER, .dim = !ui_pin_field_is_full(f)};
  out[n++] = (UiHintItem){.action = UI_BTN_CANCEL, .label = LABEL_CANCEL};
  return n;
}
