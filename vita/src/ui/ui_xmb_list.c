/**
 * @file ui_xmb_list.c
 * @brief C02 XmbList (SPEC.md C02)
 */

#include "ui/ui_xmb_list.h"

#include "ui/ui_text.h"

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
}

void ui_xmb_list_set_items(UiXmbList *list, const UiXmbItem *items, int count) {
  list->items = items;
  list->count = count > UI_LIST_MAX_ITEMS ? UI_LIST_MAX_ITEMS : count;
  if (list->focus >= list->count)
    list->focus = list->count > 0 ? list->count - 1 : 0;
  layout_rows(list);
}

void ui_xmb_list_set_focus(UiXmbList *list, int index) {
  if (index >= list->count)
    index = list->count - 1;
  list->focus = index < 0 ? 0 : index;
  layout_rows(list);
}

/** Draw the status line (dot, label, route label) with its text top at @text_y. */
static void draw_status_line(const UiXmbItem *item, UiFace face, int text_y, float k) {
  int line_h = ui_text_face_line_height(face);
  int x = UI_LIST_TEXT_X;

  if (item->status_dot) {
    vita2d_draw_fill_circle(x + UI_LIST_DOT_R, text_y + line_h / 2, UI_LIST_DOT_R,
                            ui_color_scale_alpha(item->status_color, k));
    x += UI_LIST_DOT_R * 2 + UI_LIST_DOT_GAP;
  }
  ui_text_draw_face_centered_v(face, x, text_y, line_h, ui_color_scale_alpha(item->status_color, k),
                               item->status);
  if (item->route) {
    x += ui_text_face_width(face, item->status) + UI_LIST_ROUTE_GAP;
    ui_text_draw_face_centered_v(face, x, text_y, line_h, ui_color_scale_alpha(UI_INTERNET, k),
                                 item->route);
  }
}

/** Draw one row whose top edge is @top. */
static void draw_row(const UiXmbItem *item, int top, bool focused) {
  float k = row_fade(top);
  if (item->dim_row)
    k *= (float)UI_LIST_DIM_PCT / 100.0f;
  if (k <= 0.0f)
    return;

  const int icon_x = UI_LIST_ICON_CX - UI_ITEM_ICON / 2;
  const int icon_y = top + (UI_LIST_ICON_BOX - UI_ITEM_ICON) / 2;

  if (focused) {
    vita2d_texture *glow = ui_glow_texture();
    if (glow) {
      vita2d_draw_texture_tint(glow, (float)(UI_LIST_ICON_CX - UI_LIST_GLOW / 2),
                               (float)(top + UI_LIST_ICON_BOX / 2 - UI_LIST_GLOW / 2),
                               ui_color_scale_alpha(UI_WHITE_PCT(UI_LIST_GLOW_PCT), k));
    }
  }

  if (item->icon) {
    float icon_k = item->dim_icon ? k * (float)UI_LIST_DIM_PCT / 100.0f : k;
    vita2d_draw_texture_tint(item->icon, (float)icon_x, (float)icon_y,
                             ui_color_scale_alpha(UI_TEXT, icon_k));
  }

  UiFace name_face = focused ? UI_FACE_T20 : UI_FACE_T16;
  UiFace status_face = focused ? UI_FACE_T16 : UI_FACE_T14;
  int name_h = ui_text_face_line_height(name_face);
  int status_h = item->status ? ui_text_face_line_height(status_face) : 0;
  int text_top = top + (UI_LIST_ROW_H - (name_h + status_h)) / 2;

  ui_text_draw_face_centered_v(name_face, UI_LIST_TEXT_X, text_top, name_h,
                               ui_color_scale_alpha(focused ? UI_TEXT : UI_TEXT_2, k), item->name);
  if (item->status)
    draw_status_line(item, status_face, text_top + name_h, k);
}

void ui_xmb_list_draw(const UiXmbList *list) {
  for (int i = list->focus; i < list->count; i++) {
    int top = row_top(i, list->focus);
    if (top >= LIST_BOTTOM)
      break;
    draw_row(&list->items[i], top, i == list->focus);
  }
}

UiEvent ui_xmb_list_input(UiXmbList *list, const UiInput *in) {
  if (list->count == 0)
    return UI_EVENT_NONE;

  if ((in->repeat & UI_BTN_UP) && list->focus > 0) {
    ui_xmb_list_set_focus(list, list->focus - 1);
    return UI_EVENT_MOVED;
  }
  if ((in->repeat & UI_BTN_DOWN) && list->focus < list->count - 1) {
    ui_xmb_list_set_focus(list, list->focus + 1);
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
      ui_xmb_list_set_focus(list, i);
      return UI_EVENT_MOVED;
    }
  }
  return UI_EVENT_NONE;
}
