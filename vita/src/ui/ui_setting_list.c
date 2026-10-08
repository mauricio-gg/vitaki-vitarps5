/**
 * @file ui_setting_list.c
 * @brief C08 SettingRow list, C09 Toggle, C10 ChoiceValue (SPEC.md C08)
 */

#include "ui/ui_setting_list.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <vita2d.h>

#include "ui/ui_animation.h"
#include "ui/ui_bake.h"
#include "ui/ui_chrome_layout.h"
#include "ui/ui_hint_row.h"
#include "ui/ui_motion.h"
#include "ui/ui_shapes.h"
#include "ui/ui_text.h"
#include "ui/ui_toast.h"
#include "ui/ui_value_labels.h"

/* ============================================================================
 * Baked art
 * ============================================================================ */

/** Chevrons are drawn in the mock's 24 x 24 grid: the right one is (9,5) (16,12) (9,19). */
#define CHEVRON_GRID 24.0f
#define CHEVRON_TIP_X 16.0f
#define CHEVRON_BACK_X 9.0f
#define CHEVRON_TOP_Y 5.0f
#define CHEVRON_MID_Y 12.0f
#define CHEVRON_BOTTOM_Y 19.0f

/** What a baked toggle track texture holds. */
typedef enum track_part_t {
  TRACK_BORDER = 0,
  TRACK_FILL,
} TrackPart;

static vita2d_texture *s_chevron_left = NULL;
static vita2d_texture *s_chevron_right = NULL;
static vita2d_texture *s_knob = NULL;
static vita2d_texture *s_track_border = NULL;
static vita2d_texture *s_track_fill = NULL;

static float clamp01(float v) {
  return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/** Alpha of a chevron; @ctx points to +1 (right) or -1 (left). */
static float chevron_alpha(float px, float py, const void *ctx) {
  const bool right = *(const int *)ctx > 0;
  const float scale = (float)UI_CHOICE_ARROW_ART / CHEVRON_GRID;
  const float tip_x = right ? CHEVRON_TIP_X : CHEVRON_GRID - CHEVRON_TIP_X;
  const float back_x = right ? CHEVRON_BACK_X : CHEVRON_GRID - CHEVRON_BACK_X;
  const float d1 = ui_bake_distance_to_segment(px, py, back_x * scale, CHEVRON_TOP_Y * scale,
                                               tip_x * scale, CHEVRON_MID_Y * scale);
  const float d2 = ui_bake_distance_to_segment(px, py, tip_x * scale, CHEVRON_MID_Y * scale,
                                               back_x * scale, CHEVRON_BOTTOM_Y * scale);
  const float d = d1 < d2 ? d1 : d2;
  return UI_CHOICE_ARROW_STROKE / 2.0f + 0.5f - d;
}

/** Alpha of the round knob: a disc filling the texture. */
static float knob_alpha(float px, float py, const void *ctx) {
  (void)ctx;
  const float r = (float)UI_TOGGLE_KNOB / 2.0f;
  return r + 0.5f - sqrtf((px - r) * (px - r) + (py - r) * (py - r));
}

/** Coverage (0..1) of a pill of half sizes (@hx, @hy) centred at (@cx, @cy), radius @hy. */
static float pill_coverage(float px, float py, float cx, float cy, float hx, float hy) {
  const float qx = fabsf(px - cx) - (hx - hy);
  const float ox = qx > 0.0f ? qx : 0.0f;
  const float qy = fabsf(py - cy);
  return clamp01(0.5f - (sqrtf(ox * ox + qy * qy) - hy));
}

/** Alpha of the toggle track: the border ring or the fill inside it; @ctx points to a TrackPart. */
static float track_alpha(float px, float py, const void *ctx) {
  const float hx = (float)UI_TOGGLE_W / 2.0f;
  const float hy = (float)UI_TOGGLE_H / 2.0f;
  const float inner_hx = hx - (float)UI_TOGGLE_BORDER;
  const float inner_hy = hy - (float)UI_TOGGLE_BORDER;
  const float inner = pill_coverage(px, py, hx, hy, inner_hx, inner_hy);
  if (*(const TrackPart *)ctx == TRACK_FILL)
    return inner;
  return pill_coverage(px, py, hx, hy, hx, hy) - inner;
}

/* ============================================================================
 * Setup and caches
 * ============================================================================ */

void ui_setting_list_init(UiSettingList *list) {
  static const int LEFT = -1;
  static const int RIGHT = 1;
  static const TrackPart BORDER = TRACK_BORDER;
  static const TrackPart FILL = TRACK_FILL;

  if (!s_chevron_left) {
    s_chevron_left = ui_bake_white(UI_CHOICE_ARROW_ART, UI_CHOICE_ARROW_ART, chevron_alpha, &LEFT);
    s_chevron_right =
        ui_bake_white(UI_CHOICE_ARROW_ART, UI_CHOICE_ARROW_ART, chevron_alpha, &RIGHT);
    s_knob = ui_bake_white(UI_TOGGLE_KNOB, UI_TOGGLE_KNOB, knob_alpha, NULL);
    s_track_border = ui_bake_white(UI_TOGGLE_W, UI_TOGGLE_H, track_alpha, &BORDER);
    s_track_fill = ui_bake_white(UI_TOGGLE_W, UI_TOGGLE_H, track_alpha, &FILL);
  }

  memset(list, 0, sizeof(*list));
  list->pane = (UiRect){UI_PAGE_PANE_X, UI_PAGE_BODY_Y, UI_PAGE_PANE_W, UI_PAGE_PANE_H};
  const int controls_right = UI_PAGE_PANE_X + UI_PAGE_PANE_W - UI_ROW_PAD;
  const int choice_x = controls_right - 2 * UI_CHOICE_ARROW - UI_CHOICE_VALUE_W;
  for (int slot = 0; slot < UI_PAGE_PANE_ROWS; slot++) {
    const int y = UI_PAGE_BODY_Y + slot * UI_ROW_H;
    list->row[slot] = (UiRect){UI_PAGE_PANE_X, y, UI_PAGE_PANE_W, UI_ROW_H};
    list->arrow_left[slot] = ui_rect_hit_from_visible(
        (UiRect){choice_x, y, UI_CHOICE_ARROW, UI_ROW_H}, UI_TAP_MIN, UI_TAP_MIN);
    list->arrow_right[slot] = ui_rect_hit_from_visible(
        (UiRect){controls_right - UI_CHOICE_ARROW, y, UI_CHOICE_ARROW, UI_ROW_H}, UI_TAP_MIN,
        UI_TAP_MIN);
  }
}

/** Width function for ui_ellipsize_to_fit(); @ctx points to the value's UiFace. */
static int measure_value(const char *text, void *ctx) {
  return ui_text_face_width(*(const UiFace *)ctx, text);
}

/** The face an item's value is drawn in: a choice is T20, an info row T20 or T16 as it asks. */
static UiFace value_face(const UiSettingItem *item) {
  return item->kind == UI_SETTING_INFO && item->small_value ? UI_FACE_T16 : UI_FACE_T20;
}

/** The room an item's value has: a choice's fixed box, or what an info row's label (and an error
 * row's icon) leaves. */
static int value_max_width(const UiSettingList *list, int index) {
  if (list->items[index].kind == UI_SETTING_CHOICE)
    return UI_CHOICE_VALUE_W;
  const int icon_w = list->items[index].error ? UI_SETTING_ERR_ICON + UI_SETTING_ERR_ICON_GAP : 0;
  return UI_PAGE_PANE_W - 2 * UI_ROW_PAD - list->label_w[index] - UI_INFO_VALUE_GAP - icon_w;
}

/** Measure the label of row @index: its words, and, with a glyph, the glyph and the tail. */
static void measure_label(UiSettingList *list, int index) {
  const UiSettingItem *item = &list->items[index];
  list->label_seen[index] = item->label;
  list->tail_seen[index] = item->label_tail;
  list->label_head_w[index] = ui_text_face_width(UI_FACE_T20, item->label);
  list->label_w[index] = list->label_head_w[index];
  if (item->label_glyph && item->label_tail) {
    list->label_w[index] += UI_SETTING_GLYPH_GAP + ui_hint_row_glyph_width(item->label_glyph) +
                            UI_SETTING_GLYPH_GAP +
                            ui_text_face_width(UI_FACE_T20, item->label_tail);
  }
}

/**
 * refresh_caches() - Re-measure what changed in the items since the last call (the shortened
 * text and width of every choice and info value; the knob of every toggle).
 * @animate: Start the knob slide of a toggle whose value changed; otherwise place it at once.
 */
static void refresh_caches(UiSettingList *list, bool animate) {
  for (int i = 0; i < list->count; i++) {
    const UiSettingItem *item = &list->items[i];
    if (item->label != list->label_seen[i] || item->label_tail != list->tail_seen[i]) {
      measure_label(list, i);
      list->value_raw[i][0] = '\0'; /* the room for the value changed with the label */
    }
    if (item->kind == UI_SETTING_TOGGLE) {
      if (item->on != list->shown_on[i]) {
        list->shown_on[i] = item->on;
        list->toggle_start_us[i] = animate ? ui_anim_now_us() : 0;
      }
      continue;
    }
    if (item->kind == UI_SETTING_ACTION)
      continue;
    const char *text = item->value_text ? item->value_text : "";
    if (strncmp(list->value_raw[i], text, UI_SETTING_VALUE_MAX - 1) == 0)
      continue;
    UiFace face = value_face(item);
    snprintf(list->value_raw[i], sizeof(list->value_raw[i]), "%s", text);
    ui_ellipsize_to_fit(list->value_raw[i], value_max_width(list, i), measure_value, &face,
                        list->value_fit[i], sizeof(list->value_fit[i]));
    list->value_w[i] = ui_text_face_width(face, list->value_fit[i]);
  }
}

void ui_setting_list_load(UiSettingList *list, const UiSettingItem *items, int count) {
  list->items = items;
  list->count = count > UI_SETTING_MAX_ROWS ? UI_SETTING_MAX_ROWS : count;
  list->focus = 0;
  list->scroll = 0;
  list->step = 0;
  list->press_start_us = 0;
  list->swipe_active = false;
  for (int i = 0; i < list->count; i++) {
    list->label_seen[i] = NULL;
    list->tail_seen[i] = NULL;
    list->shown_on[i] = items[i].on;
    list->toggle_start_us[i] = 0;
    list->value_raw[i][0] = '\0';
    list->value_w[i] = 0;
  }
  refresh_caches(list, false);
}

void ui_setting_list_sync(UiSettingList *list) {
  refresh_caches(list, true);
}

/* ============================================================================
 * Draw
 * ============================================================================ */

/** Colour @t of the way (0..1) from @a to @b, channel by channel including alpha. */
static uint32_t mix_color(uint32_t a, uint32_t b, float t) {
  uint32_t out = 0;
  for (int shift = 0; shift < 32; shift += 8) {
    const float ca = (float)((a >> shift) & 0xFFu);
    const float cb = (float)((b >> shift) & 0xFFu);
    out |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << shift;
  }
  return out;
}

/** How far the knob of row @index is toward the on side: 0 (off) to 1 (on), eased while sliding. */
static float knob_position(const UiSettingList *list, int index) {
  const float target = list->shown_on[index] ? 1.0f : 0.0f;
  if (!list->toggle_start_us[index])
    return target;
  const float p = UI_EASE_OUT(
      ui_motion_progress(ui_anim_elapsed_ms(list->toggle_start_us[index]), 0.0f, UI_TOGGLE_MS));
  return list->shown_on[index] ? p : 1.0f - p;
}

/** Draw the toggle of row @index in the row at (@x, @y), with its On/Off text in @text_color. */
static void draw_toggle(const UiSettingList *list, int index, int x, int y, uint32_t text_color) {
  const float k = knob_position(list, index);
  const uint32_t line = mix_color(UI_TEXT_2, UI_TEXT, k);
  const int track_y = y + (UI_ROW_H - UI_TOGGLE_H) / 2;

  if (k > 0.0f && s_track_fill)
    vita2d_draw_texture_tint(s_track_fill, (float)x, (float)track_y,
                             ui_color_scale_alpha(UI_FILL_ON, k));
  if (s_track_border)
    vita2d_draw_texture_tint(s_track_border, (float)x, (float)track_y, line);
  if (s_knob) {
    const float knob_x =
        (float)(x + UI_TOGGLE_BORDER + UI_TOGGLE_INSET) + k * (float)UI_TOGGLE_TRAVEL;
    vita2d_draw_texture_tint(s_knob, knob_x, (float)(track_y + (UI_TOGGLE_H - UI_TOGGLE_KNOB) / 2),
                             line);
  }
  ui_text_draw_face_centered_v(UI_FACE_T16, x + UI_TOGGLE_W + UI_TOGGLE_TEXT_GAP, y, UI_ROW_H,
                               text_color, ui_label_on_off(list->shown_on[index]));
}

/** Draw the choice of row @index in slot rects @left and @right: chevron, value, chevron. */
static void draw_choice(const UiSettingList *list, int index, UiRect left, UiRect right, int y,
                        bool focused, uint32_t text_color) {
  const float arrow_k = focused ? 1.0f : (float)UI_CHOICE_ARROW_DIM_PCT / 100.0f;
  const uint32_t arrow_color = ui_color_scale_alpha(text_color, arrow_k);
  const int inset = (UI_CHOICE_ARROW - UI_CHOICE_ARROW_ART) / 2;
  const int left_box_x = left.x + (left.w - UI_CHOICE_ARROW) / 2;
  const int right_box_x = right.x + (right.w - UI_CHOICE_ARROW) / 2;
  const int art_y = y + (UI_ROW_H - UI_CHOICE_ARROW_ART) / 2;

  if (s_chevron_left)
    vita2d_draw_texture_tint(s_chevron_left, (float)(left_box_x + inset), (float)art_y,
                             arrow_color);
  if (s_chevron_right)
    vita2d_draw_texture_tint(s_chevron_right, (float)(right_box_x + inset), (float)art_y,
                             arrow_color);
  const int value_x = left_box_x + UI_CHOICE_ARROW + (UI_CHOICE_VALUE_W - list->value_w[index]) / 2;
  ui_text_draw_face_centered_v(UI_FACE_T20, value_x, y, UI_ROW_H, text_color,
                               list->value_fit[index]);
}

/** Draw the value of info row @index right-aligned at the row padding; an error row adds its
 * warning icon before the value and its rule at the row's left edge. */
static void draw_info(const UiSettingList *list, int index, UiRect row, uint32_t text_color) {
  const UiSettingItem *item = &list->items[index];
  const uint32_t color = item->color ? item->color : text_color;
  const int x = row.x + row.w - UI_ROW_PAD - list->value_w[index];
  if (item->error) {
    ui_toast_draw_icon(UI_TOAST_ERR, x - UI_SETTING_ERR_ICON_GAP - UI_SETTING_ERR_ICON,
                       row.y + (row.h - UI_SETTING_ERR_ICON) / 2, UI_SETTING_ERR_ICON, color);
    vita2d_draw_rectangle((float)row.x, (float)row.y, (float)UI_SETTING_ERR_RULE_W, (float)row.h,
                          color);
  }
  ui_text_draw_face_centered_v(value_face(item), x, row.y, row.h, color, list->value_fit[index]);
}

/** Draw the label of row @index: its words, then, when it has them, the glyph and the tail. */
static void draw_label(const UiSettingList *list, int index, UiRect row, uint32_t color) {
  const UiSettingItem *item = &list->items[index];
  int x = row.x + UI_ROW_PAD;
  ui_text_draw_face_centered_v(UI_FACE_T20, x, row.y, row.h, color, item->label);
  if (!item->label_glyph || !item->label_tail)
    return;
  x += list->label_head_w[index] + UI_SETTING_GLYPH_GAP;
  ui_hint_row_glyph_draw(item->label_glyph, x, row.y, row.h, color);
  x += ui_hint_row_glyph_width(item->label_glyph) + UI_SETTING_GLYPH_GAP;
  ui_text_draw_face_centered_v(UI_FACE_T20, x, row.y, row.h, color, item->label_tail);
}

/** Draw the chevron of an enabled action at the right row padding, in @color. */
static void draw_action(UiRect row, uint32_t color) {
  if (!s_chevron_right)
    return;
  vita2d_draw_texture_tint(s_chevron_right,
                           (float)(row.x + row.w - UI_ROW_PAD - UI_CHOICE_ARROW_ART),
                           (float)(row.y + (row.h - UI_CHOICE_ARROW_ART) / 2), color);
}

/** True for UI_ROW_PRESS_MS after a row acted. */
static bool row_pressed(const UiSettingList *list) {
  return list->press_start_us != 0 &&
         ui_anim_elapsed_ms(list->press_start_us) < (float)UI_ROW_PRESS_MS;
}

void ui_setting_list_draw(const UiSettingList *list) {
  for (int slot = 0; slot < UI_PAGE_PANE_ROWS; slot++) {
    const int i = list->scroll + slot;
    if (i >= list->count)
      break;
    const UiSettingItem *item = &list->items[i];
    const UiRect row = list->row[slot];
    const bool focused = i == list->focus;
    const bool disabled = item->kind == UI_SETTING_ACTION && item->disabled;
    uint32_t text_color = focused ? UI_TEXT : UI_TEXT_2;
    uint32_t label_color =
        item->kind == UI_SETTING_ACTION && item->color ? item->color : text_color;
    if (disabled) {
      text_color = ui_color_scale_alpha(text_color, (float)UI_ROW_DISABLED_PCT / 100.0f);
      label_color = ui_color_scale_alpha(label_color, (float)UI_ROW_DISABLED_PCT / 100.0f);
    }
    const int label_x = row.x + UI_ROW_PAD;

    if (focused) {
      if (list->active) {
        const UiRect label = {label_x, row.y + (row.h - UI_T20_LINE) / 2, list->label_w[i],
                              UI_T20_LINE};
        ui_glow_draw_rect(label, UI_ROW_GLOW,
                          ui_color_scale_alpha(UI_GLOW, (float)UI_ROW_GLOW_PCT / 100.0f));
      }
      ui_shape3_draw(UI_SHAPE3_BAR_48, row.x, row.y, row.w,
                     row_pressed(list) ? UI_FILL_ON : UI_FILL_FOCUS);
    } else {
      vita2d_draw_rectangle((float)row.x, (float)(row.y + row.h - UI_LW1), (float)row.w,
                            (float)UI_LW1, UI_LINE_FAINT);
    }
    draw_label(list, i, row, label_color);

    switch (item->kind) {
      case UI_SETTING_TOGGLE:
        draw_toggle(
            list, i,
            row.x + row.w - UI_ROW_PAD - UI_TOGGLE_W - UI_TOGGLE_TEXT_GAP - UI_TOGGLE_TEXT_W, row.y,
            text_color);
        break;
      case UI_SETTING_CHOICE:
        draw_choice(list, i, list->arrow_left[slot], list->arrow_right[slot], row.y, focused,
                    text_color);
        break;
      case UI_SETTING_INFO:
        draw_info(list, i, row, text_color);
        break;
      case UI_SETTING_ACTION:
        if (!disabled)
          draw_action(row, label_color);
        break;
    }
  }
}

/* ============================================================================
 * Input
 * ============================================================================ */

/** Move the scroll the least that shows the focused row. */
static void follow_focus(UiSettingList *list) {
  if (list->focus < list->scroll)
    list->scroll = list->focus;
  else if (list->focus >= list->scroll + UI_PAGE_PANE_ROWS)
    list->scroll = list->focus - UI_PAGE_PANE_ROWS + 1;
}

/** Focus row @index (clamped), scroll to it. @return true when the focus changed. */
static bool set_focus(UiSettingList *list, int index) {
  if (index >= list->count)
    index = list->count - 1;
  if (index < 0)
    index = 0;
  if (index == list->focus)
    return false;
  list->focus = index;
  follow_focus(list);
  return true;
}

/** Report that the focused row should change by @step, and start its pressed state. */
static UiEvent activate(UiSettingList *list, int step) {
  list->step = step;
  list->press_start_us = ui_anim_now_us();
  return UI_EVENT_ACTIVATED;
}

/** Focus row @index and scroll to it. */
static void focus_row(UiSettingList *list, int index) {
  list->focus = index;
  follow_focus(list);
}

/** True when row @index only takes focus: an info row. */
static bool focus_only(const UiSettingList *list, int index) {
  return list->items[index].kind == UI_SETTING_INFO;
}

/** Report that row @index was pressed: a disabled action tells the screen without the pressed
 * look, so the screen can say why it does nothing. */
static UiEvent press(UiSettingList *list, int index, int step) {
  const UiSettingItem *item = &list->items[index];
  if (item->kind == UI_SETTING_ACTION && item->disabled) {
    list->step = step;
    return UI_EVENT_ACTIVATED;
  }
  return activate(list, step);
}

/**
 * A tap at (@x, @y): a chevron steps its way, a row steps forward (acts), and a row that only
 * takes focus reports UI_EVENT_MOVED.
 */
static UiEvent handle_tap(UiSettingList *list, float x, float y) {
  for (int slot = 0; slot < UI_PAGE_PANE_ROWS && list->scroll + slot < list->count; slot++) {
    const int i = list->scroll + slot;
    int step = 0;
    if (list->items[i].kind == UI_SETTING_CHOICE) {
      if (ui_rect_contains(list->arrow_left[slot], x, y))
        step = -1;
      else if (ui_rect_contains(list->arrow_right[slot], x, y))
        step = 1;
    }
    if (!step && ui_rect_contains(list->row[slot], x, y))
      step = 1;
    if (!step)
      continue;
    focus_row(list, i);
    return focus_only(list, i) ? UI_EVENT_MOVED : press(list, i, step);
  }
  return UI_EVENT_NONE;
}

/** Follow a vertical swipe that began on the pane: one row per UI_ROW_SWIPE_PX from touch-down. */
static UiEvent handle_swipe(UiSettingList *list, const UiTouch *touch) {
  if (touch->pressed)
    list->swipe_active = ui_rect_contains(list->pane, touch->x, touch->y);
  if (!list->swipe_active)
    return UI_EVENT_NONE;
  if (!touch->down) {
    list->swipe_active = false;
    return UI_EVENT_NONE;
  }
  if (!touch->dragged)
    return UI_EVENT_NONE;
  const int rows = (int)(-touch->dy / (float)UI_ROW_SWIPE_PX);
  return set_focus(list, list->swipe_base + rows) ? UI_EVENT_MOVED : UI_EVENT_NONE;
}

UiEvent ui_setting_list_input(UiSettingList *list, const UiInput *in) {
  if (list->count == 0)
    return UI_EVENT_NONE;

  if (in->touch.pressed)
    list->swipe_base = list->focus;
  const UiEvent swiped = handle_swipe(list, &in->touch);
  if (swiped != UI_EVENT_NONE)
    return swiped;

  const UiSettingKind kind = list->items[list->focus].kind;
  if ((in->repeat & UI_BTN_UP) && set_focus(list, list->focus - 1))
    return UI_EVENT_MOVED;
  if ((in->repeat & UI_BTN_DOWN) && set_focus(list, list->focus + 1))
    return UI_EVENT_MOVED;
  if (in->pressed & UI_BTN_CONFIRM)
    return focus_only(list, list->focus) ? UI_EVENT_NONE : press(list, list->focus, 1);
  if (kind == UI_SETTING_TOGGLE && (in->pressed & UI_BTN_RIGHT))
    return activate(list, 1);
  if (kind == UI_SETTING_CHOICE && (in->repeat & UI_BTN_RIGHT))
    return activate(list, 1);
  if (in->repeat & UI_BTN_LEFT)
    return kind == UI_SETTING_CHOICE ? activate(list, -1) : UI_EVENT_CANCELLED;
  if (in->pressed & UI_BTN_CANCEL)
    return UI_EVENT_CANCELLED;
  if (ui_touch_tap(in))
    return handle_tap(list, in->touch.x, in->touch.y);
  return UI_EVENT_NONE;
}
