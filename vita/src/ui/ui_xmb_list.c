/**
 * @file ui_xmb_list.c
 * @brief C02 XmbList (SPEC.md C02)
 */

#include "ui/ui_xmb_list.h"

#include "ui/ui_animation.h"
#include "ui/ui_gesture.h"
#include "ui/ui_motion.h"
#include "ui/ui_text.h"

/** Where a row is in its motion: top edge in pixels and opacity (0..1). */
typedef struct row_pose_t {
  float top;
  float alpha;
} RowPose;

/** Bottom edge of the list viewport, where rows have faded out completely. */
#define LIST_BOTTOM (UI_LIST_Y + UI_LIST_H)

/** Top y of row @index while @focus is focused; rows above the focus are not placed. */
static int row_top(int index, int focus) {
  if (index == focus)
    return UI_LIST_FOCUS_Y;
  return UI_LIST_FOCUS_Y + UI_LIST_ROW_H + UI_LIST_FOCUS_GAP + (index - focus - 1) * UI_LIST_ROW_H;
}

/**
 * row_fade() - Opacity (0..1) of a row whose top is @top: full until the last
 * UI_LIST_FADE_H pixels of the viewport, then linear to zero at its bottom edge.
 * Measured a little below the row top so the text, not the empty box edge, fades.
 */
static float row_fade(int top) {
  int probe = top + UI_S2;
  int fade_start = LIST_BOTTOM - UI_LIST_FADE_H;
  if (probe <= fade_start)
    return 1.0f;
  if (probe >= LIST_BOTTOM)
    return 0.0f;
  return (float)(LIST_BOTTOM - probe) / (float)UI_LIST_FADE_H;
}

/** Settled pose of row @index while @focus is focused: rows above the focus are faded out. */
static RowPose row_target(int index, int focus) {
  if (index < focus)
    return (RowPose){(float)(UI_LIST_FOCUS_Y - UI_LIST_SLIDE * (focus - index)), 0.0f};
  return (RowPose){(float)row_top(index, focus), 1.0f};
}

/** Eased progress (0..1) of the focus slide; 1 when no slide is running. */
static float slide_progress(const UiXmbList *list) {
  if (!list->slide_start_us)
    return 1.0f;
  return UI_EASE_OUT(ui_motion_progress(ui_anim_elapsed_ms(list->slide_start_us), 0.0f, UI_D2_MS));
}

/** Pose of row @index at slide progress @p: from where the slide started to the settled pose. */
static RowPose row_pose(const UiXmbList *list, int index, float p) {
  RowPose target = row_target(index, list->focus);
  if (p >= 1.0f)
    return target;
  return (RowPose){list->from_top[index] + (target.top - list->from_top[index]) * p,
                   list->from_alpha[index] + (target.alpha - list->from_alpha[index]) * p};
}

static void layout_rows(UiXmbList *list) {
  for (int i = 0; i < list->count; i++) {
    UiRect empty = {0, 0, 0, 0};
    if (i < list->focus) {
      list->visible[i] = empty;
      list->hit[i] = empty;
      continue;
    }
    int top = row_top(i, list->focus);
    list->visible[i] = (UiRect){UI_LIST_X, top, UI_LIST_W, UI_LIST_ROW_H};
    list->hit[i] = row_fade(top) > 0.0f ? list->visible[i] : empty;
  }
}

void ui_xmb_list_init(UiXmbList *list) {
  list->items = NULL;
  list->count = 0;
  list->focus = 0;
  list->slide_start_us = 0;
  list->cascade_start_us = 0;
  list->swipe_active = false;
  list->swipe_base = 0;
}

void ui_xmb_list_set_items(UiXmbList *list, const UiXmbItem *items, int count) {
  int old_count = list->count;
  list->items = items;
  list->count = count > UI_LIST_MAX_ITEMS ? UI_LIST_MAX_ITEMS : count;
  if (list->focus >= list->count)
    list->focus = list->count > 0 ? list->count - 1 : 0;
  /* Rows that appear while a slide runs have no start pose yet: they start in place. */
  for (int i = old_count; i < list->count; i++) {
    RowPose target = row_target(i, list->focus);
    list->from_top[i] = target.top;
    list->from_alpha[i] = target.alpha;
  }
  layout_rows(list);
}

/** Clamp @index into the list; 0 for an empty list. */
static int clamp_focus(const UiXmbList *list, int index) {
  if (index >= list->count)
    index = list->count - 1;
  return index < 0 ? 0 : index;
}

void ui_xmb_list_set_focus(UiXmbList *list, int index) {
  list->focus = clamp_focus(list, index);
  list->slide_start_us = 0;
  layout_rows(list);
}

/**
 * slide_focus_to() - Move the focus to @index and slide every row to its new place.
 * Each row starts from the pose it has right now, so a move during a slide never snaps.
 */
static void slide_focus_to(UiXmbList *list, int index) {
  float p = slide_progress(list);
  for (int i = 0; i < list->count; i++) {
    RowPose now = row_pose(list, i, p);
    list->from_top[i] = now.top;
    list->from_alpha[i] = now.alpha;
  }
  list->focus = clamp_focus(list, index);
  list->slide_start_us = ui_anim_now_us();
  layout_rows(list);
}

void ui_xmb_list_cascade_in(UiXmbList *list) {
  list->slide_start_us = 0;
  list->cascade_start_us = ui_anim_now_us();
}

/** Draw the status line (dot, label, route label) with its text top at @text_y. */
static void draw_status_line(const UiXmbItem *item, UiFace face, int text_y, float k) {
  int line_h = ui_text_face_line_height(face);
  int x = UI_LIST_TEXT_X;

  if (item->status_dot) {
    vita2d_draw_fill_circle(x + UI_LIST_DOT_R, text_y + line_h / 2, UI_LIST_DOT_R,
                            ui_layer_color(ui_color_scale_alpha(item->status_color, k)));
    x += UI_LIST_DOT_R * 2 + UI_LIST_DOT_GAP;
  }
  ui_text_draw_face_centered_v(face, x, text_y, line_h, ui_color_scale_alpha(item->status_color, k),
                               item->status);
  if (item->status_glyph) {
    /* The glyph is baked at UI_FACE_GLYPH_H and drawn 1:1, centred in the line; the tail follows
     * its real width. */
    x += ui_text_face_width(face, item->status) + UI_LIST_GLYPH_GAP;
    vita2d_draw_texture_tint(
        item->status_glyph, (float)x,
        (float)(text_y + (line_h - (int)vita2d_texture_get_height(item->status_glyph)) / 2),
        ui_layer_color(ui_color_scale_alpha(item->status_color, k)));
    x += (int)vita2d_texture_get_width(item->status_glyph) + UI_LIST_GLYPH_GAP;
    if (item->status_tail)
      ui_text_draw_face_centered_v(face, x, text_y, line_h,
                                   ui_color_scale_alpha(item->status_color, k), item->status_tail);
  }
  if (item->route) {
    x += ui_text_face_width(face, item->status) + UI_LIST_ROUTE_GAP;
    ui_text_draw_face_centered_v(face, x, text_y, line_h, ui_color_scale_alpha(UI_INTERNET, k),
                                 item->route);
  }
}

/** Draw one row whose top edge is @top, at opacity @alpha (0..1); @glow scales the focus glow. */
static void draw_row(const UiXmbItem *item, int top, float alpha, float glow_k, bool focused) {
  float k = alpha * row_fade(top);
  if (item->dim_row)
    k *= (float)UI_LIST_DIM_PCT / 100.0f;
  if (k <= 0.0f)
    return;

  const int icon_x = UI_LIST_ICON_CX - UI_ITEM_ICON / 2;
  const int icon_y = top + (UI_LIST_ICON_BOX - UI_ITEM_ICON) / 2;

  if (focused) {
    vita2d_texture *glow = ui_glow_texture();
    if (glow) {
      vita2d_draw_texture_tint(
          glow, (float)(UI_LIST_ICON_CX - UI_LIST_GLOW / 2),
          (float)(top + UI_LIST_ICON_BOX / 2 - UI_LIST_GLOW / 2),
          ui_layer_color(ui_color_scale_alpha(UI_WHITE_PCT(UI_LIST_GLOW_PCT), k * glow_k)));
    }
  }

  if (item->icon) {
    float icon_k = item->dim_icon ? k * (float)UI_LIST_DIM_PCT / 100.0f : k;
    vita2d_draw_texture_tint(item->icon, (float)icon_x, (float)icon_y,
                             ui_layer_color(ui_color_scale_alpha(UI_TEXT, icon_k)));
  }

  UiFace name_face = focused ? UI_FACE_T20_REGULAR : UI_FACE_T16;
  UiFace status_face = focused ? UI_FACE_T16 : UI_FACE_T14;
  int name_h = ui_text_face_line_height(name_face);
  int status_h = item->status ? ui_text_face_line_height(status_face) : 0;
  int text_top = top + (UI_LIST_ROW_H - (name_h + status_h)) / 2;

  if (focused) {
    /* Soft glow behind the focused title, drawn before it and faded with the row like the icon. */
    const UiRect title = {UI_LIST_TEXT_X, text_top, ui_text_face_width(name_face, item->name),
                          name_h};
    ui_glow_draw_rect(
        title, UI_LIST_TITLE_GLOW,
        ui_color_scale_alpha(UI_GLOW, (float)UI_LIST_TITLE_GLOW_PCT / 100.0f * k * glow_k));
  }

  ui_text_draw_face_centered_v(name_face, UI_LIST_TEXT_X, text_top, name_h,
                               ui_color_scale_alpha(focused ? UI_TEXT : UI_TEXT_2, k), item->name);
  if (item->status)
    draw_status_line(item, status_face, text_top + name_h, k);
}

/** Cascade factor (0..1, eased) of row @index; 1 when no cascade is running. */
static float cascade_factor(const UiXmbList *list, int index, float cascade_ms) {
  if (!list->cascade_start_us)
    return 1.0f;
  int step = index < UI_CASCADE_MAX_ROWS ? index : UI_CASCADE_MAX_ROWS;
  return UI_EASE_OUT(ui_motion_progress(cascade_ms, (float)(step * UI_CASCADE_STEP_MS), UI_D3_MS));
}

void ui_xmb_list_draw(const UiXmbList *list) {
  const float p = slide_progress(list);
  const float cascade_ms =
      list->cascade_start_us ? ui_anim_elapsed_ms(list->cascade_start_us) : 0.0f;

  for (int i = 0; i < list->count; i++) {
    RowPose pose = row_pose(list, i, p);
    float rise = cascade_factor(list, i, cascade_ms);
    float top = pose.top + (1.0f - rise) * (float)UI_RISE_PX;
    float alpha = pose.alpha * rise;
    if (alpha <= 0.0f || top >= (float)LIST_BOTTOM)
      continue;
    draw_row(&list->items[i], (int)(top + 0.5f), alpha, p, i == list->focus);
  }
}

/**
 * follow_swipe() - Follow a vertical swipe: focus = the focus when the swipe began + whole rows
 * of finger travel (finger up = next row), clamped, no wrap. The swipe may start anywhere on the
 * screen; it counts only when it locked to the vertical axis.
 * @return UI_EVENT_MOVED when the focus changed.
 */
static UiEvent follow_swipe(UiXmbList *list, const UiTouch *touch) {
  if (!touch->down || touch->swipe_axis != UI_GESTURE_AXIS_VERTICAL) {
    list->swipe_active = false;
    return UI_EVENT_NONE;
  }
  if (!list->swipe_active) {
    list->swipe_active = true;
    list->swipe_base = list->focus;
  }
  const int target =
      clamp_focus(list, list->swipe_base + ui_gesture_swipe_steps(-touch->dy, UI_LIST_SWIPE_PX));
  if (target == list->focus)
    return UI_EVENT_NONE;
  slide_focus_to(list, target);
  return UI_EVENT_MOVED;
}

UiEvent ui_xmb_list_input(UiXmbList *list, const UiInput *in) {
  if (list->count == 0)
    return UI_EVENT_NONE;

  if (follow_swipe(list, &in->touch) == UI_EVENT_MOVED)
    return UI_EVENT_MOVED;

  if ((in->repeat & UI_BTN_UP) && list->focus > 0) {
    slide_focus_to(list, list->focus - 1);
    return UI_EVENT_MOVED;
  }
  if ((in->repeat & UI_BTN_DOWN) && list->focus < list->count - 1) {
    slide_focus_to(list, list->focus + 1);
    return UI_EVENT_MOVED;
  }
  if (in->pressed & UI_BTN_CONFIRM)
    return UI_EVENT_ACTIVATED;

  if (ui_touch_tap(in)) {
    for (int i = list->focus; i < list->count; i++) {
      if (!ui_rect_contains(list->hit[i], in->touch.x, in->touch.y))
        continue;
      if (i == list->focus)
        return UI_EVENT_ACTIVATED;
      slide_focus_to(list, i);
      return UI_EVENT_MOVED;
    }
  }
  return UI_EVENT_NONE;
}
