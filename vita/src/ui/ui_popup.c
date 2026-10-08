/**
 * @file ui_popup.c
 * @brief C11 Popup (SPEC.md C11)
 */

#include "ui/ui_popup.h"

#include <string.h>

#include <vita2d.h>

#include "ui/ui_animation.h"
#include "ui/ui_chrome_layout.h"
#include "ui/ui_constants.h"
#include "ui/ui_freeze.h"
#include "ui/ui_motion.h"
#include "ui/ui_shapes.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"

static const char DEFAULT_CANCEL_LABEL[] = "Cancel";
static const char DEFAULT_CONFIRM_LABEL[] = "Select";

/** Placement of a popup size: the card's top edge and height. */
typedef struct size_spec_t {
  int y;
  int h;
} SizeSpec;

static const SizeSpec SIZE_SPECS[] = {
    [UI_POPUP_SIZE_S] = {UI_POPUP_S_Y, UI_POPUP_S_H},
    [UI_POPUP_SIZE_M] = {UI_POPUP_M_Y, UI_POPUP_M_H},
    [UI_POPUP_SIZE_L] = {UI_POPUP_L_Y, UI_POPUP_L_H},
};

static int measure_t28(const char *s, void *ctx) {
  (void)ctx;
  return ui_text_face_width(UI_FACE_T28, s);
}

static int measure_t16(const char *s, void *ctx) {
  (void)ctx;
  return ui_text_face_width(UI_FACE_T16, s);
}

/** Place the buttons right-aligned in the bar at the card's bottom; they share the bar's width. */
static void layout_buttons(UiPopup *p, const UiPopupSpec *spec, int bar_y) {
  const int inner_w = UI_POPUP_W - 2 * UI_POPUP_PAD;
  const int right = UI_POPUP_X + UI_POPUP_W - UI_POPUP_PAD;
  int w = (inner_w - UI_POPUP_BUTTON_GAP * (spec->button_count - 1)) / spec->button_count;
  if (w > UI_POPUP_BUTTON_MAX_W)
    w = UI_POPUP_BUTTON_MAX_W;

  for (int i = 0; i < spec->button_count; i++) {
    const int x =
        right - (spec->button_count - i) * w - (spec->button_count - 1 - i) * UI_POPUP_BUTTON_GAP;
    ui_text_button_init(&p->buttons[i], spec->buttons[i], x, bar_y);
    ui_text_button_set_width(&p->buttons[i], w);
    p->buttons[i].focused = i == p->focus;
  }
}

void ui_popup_open(UiPopup *p, const UiPopupSpec *spec) {
  memset(p, 0, sizeof(*p));
  const SizeSpec size = SIZE_SPECS[spec->size];
  const int inner_x = UI_POPUP_X + UI_POPUP_PAD;
  const int inner_w = UI_POPUP_W - 2 * UI_POPUP_PAD;

  p->open = true;
  p->size = spec->size;
  p->icon = spec->icon;
  p->icon_color = spec->icon_color ? spec->icon_color : UI_TEXT;
  p->button_count = spec->button_count;
  p->focus = spec->default_focus;
  p->cancel_button = spec->cancel_button;
  p->cancel_label = spec->cancel_label ? spec->cancel_label : DEFAULT_CANCEL_LABEL;
  p->confirm_label = spec->confirm_label ? spec->confirm_label : DEFAULT_CONFIRM_LABEL;
  p->visible = (UiRect){UI_POPUP_X, size.y, UI_POPUP_W, size.h};

  /* Header: the icon and the title share the first line; the subtitle and the body follow. */
  int y = size.y + UI_POPUP_PAD;
  const int icon_w = spec->icon ? UI_POPUP_ICON + UI_POPUP_ICON_GAP : 0;
  p->left_x = inner_x;
  p->title_x = inner_x + icon_w;
  p->title_y = y;
  ui_ellipsize_to_fit(spec->title, inner_w - icon_w, measure_t28, NULL, p->title, sizeof(p->title));
  y += UI_T28_LINE;
  if (spec->subtitle) {
    ui_ellipsize_to_fit(spec->subtitle, inner_w, measure_t16, NULL, p->subtitle,
                        sizeof(p->subtitle));
    p->subtitle_y = y;
    y += UI_T16_LINE;
  }
  if (spec->body) {
    ui_text_wrap(spec->body, inner_w, measure_t16, NULL, &p->body);
    p->body_y = y + UI_POPUP_BODY_GAP;
    y = p->body_y + p->body.count * UI_T16_LINE;
  }

  const int bottom = size.y + size.h - UI_POPUP_PAD;
  const int bar_y = bottom - UI_BUTTON_H;
  if (spec->button_count > 0)
    layout_buttons(p, spec, bar_y);
  p->content = (UiRect){inner_x, y, inner_w, (spec->button_count > 0 ? bar_y : bottom) - y};

  ui_freeze_request();
}

void ui_popup_close(UiPopup *p) {
  if (!p->open)
    return;
  p->open = false;
  ui_freeze_release();
}

void ui_popup_enter(const UiPopup *p, int *dy, float *alpha) {
  float e = 0.0f;
  if (p->enter_start_us != 0) {
    e = UI_EASE_OUT(
        ui_motion_progress(ui_anim_elapsed_ms(p->enter_start_us), 0.0f, (float)UI_POPUP_ENTER_MS));
  }
  *dy = (int)((1.0f - e) * (float)UI_RISE_PX + 0.5f);
  *alpha = e;
}

void ui_popup_draw(const UiPopup *p) {
  if (!p->open || p->enter_start_us == 0)
    return;

  int dy;
  float k;
  ui_popup_enter(p, &dy, &k);

  vita2d_draw_rectangle(0.0f, 0.0f, (float)VITA_WIDTH, (float)VITA_HEIGHT,
                        ui_color_scale_alpha(UI_SCRIM, k));

  ui_layer_set_alpha(k);
  UiRect card = p->visible;
  card.y += dy;
  ui_shape9_draw(UI_SHAPE9_MD, card, UI_PANEL);
  ui_shape9_draw(UI_SHAPE9_MD_BORDER, card, UI_LINE);

  if (p->icon) {
    const float scale = (float)UI_POPUP_ICON / (float)vita2d_texture_get_height(p->icon);
    vita2d_draw_texture_tint_scale(p->icon, (float)p->left_x, (float)(p->title_y + dy), scale,
                                   scale, ui_layer_color(p->icon_color));
  }
  ui_text_draw_face_centered_v(UI_FACE_T28, p->title_x, p->title_y + dy, UI_T28_LINE, UI_TEXT,
                               p->title);
  if (p->subtitle_y != 0) {
    ui_text_draw_face_centered_v(UI_FACE_T16, p->left_x, p->subtitle_y + dy, UI_T16_LINE, UI_TEXT_2,
                                 p->subtitle);
  }
  for (int i = 0; i < p->body.count; i++) {
    ui_text_draw_face_centered_v(UI_FACE_T16, p->left_x, p->body_y + i * UI_T16_LINE + dy,
                                 UI_T16_LINE, UI_TEXT_2, p->body.lines[i]);
  }
  for (int i = 0; i < p->button_count; i++) {
    UiTextButton shifted = p->buttons[i];
    shifted.visible.y += dy;
    ui_text_button_draw(&shifted);
  }
  ui_layer_set_alpha(1.0f);
}

UiEvent ui_popup_input(UiPopup *p, const UiInput *in) {
  if (!p->open)
    return UI_EVENT_NONE;
  if (p->enter_start_us == 0)
    p->enter_start_us = ui_anim_now_us();

  if (in->pressed & UI_BTN_CANCEL)
    return UI_EVENT_CANCELLED;

  if ((in->repeat & UI_BTN_LEFT) && p->focus > 0) {
    p->focus--;
  } else if ((in->repeat & UI_BTN_RIGHT) && p->focus < p->button_count - 1) {
    p->focus++;
  } else {
    for (int i = 0; i < p->button_count; i++) {
      p->buttons[i].focused = i == p->focus;
      if (ui_text_button_input(&p->buttons[i], in) == UI_EVENT_ACTIVATED) {
        p->focus = i;
        p->activated = i;
        for (int j = 0; j < p->button_count; j++)
          p->buttons[j].focused = j == p->focus;
        return UI_EVENT_ACTIVATED;
      }
    }
    if (ui_touch_tap(in) && !ui_rect_contains(p->visible, in->touch.x, in->touch.y))
      return UI_EVENT_CANCELLED;
    return UI_EVENT_NONE;
  }
  for (int i = 0; i < p->button_count; i++)
    p->buttons[i].focused = i == p->focus;
  return UI_EVENT_MOVED;
}

int ui_popup_hints(const UiPopup *p, UiHintItem out[UI_HINT_MAX_ITEMS]) {
  const char *cancel_label =
      p->cancel_button >= 0 ? p->buttons[p->cancel_button].label : p->cancel_label;
  int n = 0;

  if (p->button_count == 0) {
    out[n++] = (UiHintItem){.action = UI_BTN_CONFIRM, .label = p->confirm_label};
    out[n++] = (UiHintItem){.action = UI_BTN_CANCEL, .label = cancel_label};
    return n;
  }
  out[n++] = (UiHintItem){.action = UI_BTN_CONFIRM, .label = p->buttons[p->focus].label};
  if (p->button_count > 1 && p->focus != p->cancel_button)
    out[n++] = (UiHintItem){.action = UI_BTN_CANCEL, .label = cancel_label};
  return n;
}
