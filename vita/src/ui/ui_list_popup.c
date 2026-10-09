/**
 * @file ui_list_popup.c
 * @brief C12 ListPopup (SPEC.md C12)
 */

#include "ui/ui_list_popup.h"

#include <string.h>

#include <vita2d.h>

#include "ui/ui_internal.h"
#include "ui/ui_scroll_indicator.h"
#include "ui/ui_shapes.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"

#define CHECK_ICON_PATH "app0:/assets/icons/popup_check.png"

static vita2d_texture *s_icon_check = NULL;

void ui_list_popup_init(void) {
  s_icon_check = ui_load_png_linear(CHECK_ICON_PATH);
}

/* ============================================================================
 * Layout
 * ============================================================================ */

/** Copy @src into @dst (UI_LISTPOP_TEXT_MAX bytes), cut off if too long. */
static void copy_text(char dst[UI_LISTPOP_TEXT_MAX], const char *src) {
  strncpy(dst, src ? src : "", UI_LISTPOP_TEXT_MAX - 1);
  dst[UI_LISTPOP_TEXT_MAX - 1] = '\0';
}

/** The first row on screen: the focused row is kept near the middle of the viewport. */
static int first_visible(const UiListPopup *list) {
  int first = list->focus - list->visible / 2 + 1;
  if (first > list->count - list->visible)
    first = list->count - list->visible;
  return first < 0 ? 0 : first;
}

/** The rect of grid cell @i. */
static UiRect cell_rect(const UiListPopup *list, int i) {
  const int col = i % UI_LISTPOP_GRID_COLS;
  const int row = i / UI_LISTPOP_GRID_COLS;
  return (UiRect){list->viewport.x + col * (UI_LISTPOP_CELL_W + UI_LISTPOP_CELL_GAP),
                  list->viewport.y + row * (UI_LISTPOP_CELL_H + UI_LISTPOP_CELL_GAP),
                  UI_LISTPOP_CELL_W, UI_LISTPOP_CELL_H};
}

/** The rect of list row @i, which must be on screen. */
static UiRect row_rect(const UiListPopup *list, int i) {
  return (UiRect){list->viewport.x, list->viewport.y + (i - first_visible(list)) * UI_LISTPOP_ROW_H,
                  list->viewport.w, UI_LISTPOP_ROW_H};
}

/** Place the grid or the list in the popup's content area. */
static void layout(UiListPopup *list) {
  const UiRect content = list->popup.content;
  const bool centred = list->popup.size == UI_POPUP_SIZE_S;

  if (list->grid) {
    const int rows = (list->count + UI_LISTPOP_GRID_COLS - 1) / UI_LISTPOP_GRID_COLS;
    const int w =
        UI_LISTPOP_GRID_COLS * UI_LISTPOP_CELL_W + (UI_LISTPOP_GRID_COLS - 1) * UI_LISTPOP_CELL_GAP;
    const int h = rows * UI_LISTPOP_CELL_H + (rows - 1) * UI_LISTPOP_CELL_GAP;
    list->viewport =
        (UiRect){content.x + (content.w - w) / 2, content.y + UI_LISTPOP_TOP_GAP, w, h};
    return;
  }

  const int room = content.h - (centred ? 0 : UI_LISTPOP_TOP_GAP);
  int visible = room / UI_LISTPOP_ROW_H;
  if (visible > UI_LISTPOP_MAX_VISIBLE)
    visible = UI_LISTPOP_MAX_VISIBLE;
  list->visible = list->count < visible ? list->count : visible;

  const int h = list->visible * UI_LISTPOP_ROW_H;
  const int y = centred ? content.y + (content.h - h) / 2 : content.y + UI_LISTPOP_TOP_GAP;
  list->viewport = (UiRect){content.x, y, content.w, h};
}

void ui_list_popup_open(UiListPopup *list, const UiListPopupSpec *spec) {
  memset(list, 0, sizeof(*list));
  list->grid = spec->grid;
  list->count = spec->count < UI_LISTPOP_MAX_ROWS ? spec->count : UI_LISTPOP_MAX_ROWS;
  if (list->count < 0)
    list->count = 0;
  for (int i = 0; i < list->count; i++) {
    copy_text(list->rows[i].label, spec->rows[i].label);
    copy_text(list->rows[i].right_label, spec->rows[i].right_label);
    list->rows[i].icon = spec->rows[i].icon;
    list->rows[i].current = spec->rows[i].current;
  }
  list->focus = spec->focus < list->count ? spec->focus : list->count - 1;
  if (list->focus < 0)
    list->focus = 0;

  ui_popup_open(&list->popup, &(UiPopupSpec){
                                  .size = spec->size,
                                  .title = spec->title,
                                  .subtitle = spec->subtitle,
                                  .cancel_button = -1,
                                  .cancel_label = spec->cancel_label,
                                  .confirm_label = spec->confirm_label,
                              });
  layout(list);
}

void ui_list_popup_close(UiListPopup *list) {
  ui_popup_close(&list->popup);
}

/* ============================================================================
 * Draw
 * ============================================================================ */

/** Draw the check art @size px square at (@x, @y). */
static void draw_check(int x, int y, int size, uint32_t color) {
  if (!s_icon_check)
    return;
  const float scale = (float)size / (float)vita2d_texture_get_height(s_icon_check);
  vita2d_draw_texture_tint_scale(s_icon_check, (float)x, (float)y, scale, scale,
                                 ui_layer_color(color));
}

void ui_list_popup_draw_row(const UiListRow *row, bool focused, UiRect r) {
  ui_list_popup_draw_row_inset(row, focused, r, 0);
}

void ui_list_popup_draw_row_inset(const UiListRow *row, bool focused, UiRect r, int label_inset) {
  const int text_x = r.x + UI_LISTPOP_ROW_PAD + label_inset;
  const int right = r.x + r.w - UI_LISTPOP_ROW_PAD;

  if (focused) {
    const UiRect label = {text_x, r.y + (r.h - UI_T20_LINE) / 2,
                          ui_text_face_width(UI_FACE_T20, row->label), UI_T20_LINE};
    ui_glow_draw_rect(label, UI_ROW_GLOW,
                      ui_color_scale_alpha(UI_GLOW, (float)UI_ROW_GLOW_PCT / 100.0f));
    ui_shape3_draw(UI_SHAPE3_BAR_48, r.x, r.y, r.w, UI_FILL_FOCUS);
  } else {
    vita2d_draw_rectangle((float)r.x, (float)(r.y + r.h - UI_LW1), (float)r.w, (float)UI_LW1,
                          ui_layer_color(UI_LINE_FAINT));
  }
  ui_text_draw_face_centered_v(UI_FACE_T20, text_x, r.y, r.h, focused ? UI_TEXT : UI_TEXT_2,
                               row->label);

  int label_right = right;
  if (row->current) {
    draw_check(right - UI_LISTPOP_CHECK, r.y + (r.h - UI_LISTPOP_CHECK) / 2, UI_LISTPOP_CHECK,
               focused ? UI_TEXT : UI_TEXT_2);
    label_right -= UI_LISTPOP_CHECK + UI_LISTPOP_CHECK_GAP;
  }
  if (row->right_label[0]) {
    ui_text_draw_face_centered_v(UI_FACE_T16,
                                 label_right - ui_text_face_width(UI_FACE_T16, row->right_label),
                                 r.y, r.h, UI_TEXT_3, row->right_label);
  }
}

/** Draw grid cell @i, @dy pixels below its place (the matrix in SPEC C12: focus and current are
 * independent). */
static void draw_cell(const UiListPopup *list, int i, int dy) {
  const UiListRow *row = &list->rows[i];
  const bool focused = i == list->focus;
  UiRect r = cell_rect(list, i);
  r.y += dy;
  const int content_h = UI_LISTPOP_CELL_ICON + UI_LISTPOP_CELL_LABEL_GAP + UI_T16_LINE;
  const int icon_y = r.y + (r.h - content_h) / 2;
  const int cx = r.x + r.w / 2;

  if (focused)
    ui_shape1_draw(UI_SHAPE1_GRID_CELL, r.x, r.y, UI_FILL_FOCUS);

  if (row->icon) {
    const int pct = focused ? UI_LISTPOP_CELL_ICON_FOCUS_PCT : 100;
    const int size = UI_LISTPOP_CELL_ICON * pct / 100;
    const UiRect icon = {cx - size / 2, icon_y + (UI_LISTPOP_CELL_ICON - size) / 2, size, size};
    if (focused) {
      ui_glow_draw_rect(icon, UI_LISTPOP_CELL_GLOW,
                        ui_color_scale_alpha(UI_GLOW, (float)UI_ROW_GLOW_PCT / 100.0f));
    }
    const float scale = (float)size / (float)vita2d_texture_get_height(row->icon);
    const uint32_t tint =
        focused ? UI_TEXT : ui_color_scale_alpha(UI_TEXT, (float)UI_LISTPOP_CELL_ICON_PCT / 100.0f);
    vita2d_draw_texture_tint_scale(row->icon, (float)icon.x, (float)icon.y, scale, scale,
                                   ui_layer_color(tint));
  }

  const int label_y = icon_y + UI_LISTPOP_CELL_ICON + UI_LISTPOP_CELL_LABEL_GAP;
  ui_text_draw_face_centered_v(UI_FACE_T16, cx - ui_text_face_width(UI_FACE_T16, row->label) / 2,
                               label_y, UI_T16_LINE, focused ? UI_TEXT : UI_TEXT_2, row->label);

  if (row->current) {
    draw_check(r.x + r.w - UI_LISTPOP_CELL_CHECK_INSET - UI_LISTPOP_CELL_CHECK,
               r.y + UI_LISTPOP_CELL_CHECK_INSET, UI_LISTPOP_CELL_CHECK,
               focused ? UI_TEXT : UI_TEXT_3);
  }
}

void ui_list_popup_draw(const UiListPopup *list) {
  if (!ui_list_popup_is_open(list) || list->popup.enter_start_us == 0)
    return;
  ui_popup_draw(&list->popup);

  int dy;
  float k;
  ui_popup_enter(&list->popup, &dy, &k);
  ui_layer_set_alpha(k);

  if (list->grid) {
    for (int i = 0; i < list->count; i++)
      draw_cell(list, i, dy);
  } else {
    const int first = first_visible(list);
    for (int i = first; i < first + list->visible; i++) {
      UiRect r = row_rect(list, i);
      r.y += dy;
      ui_list_popup_draw_row(&list->rows[i], i == list->focus, r);
    }
    ui_scroll_indicator_draw(list->viewport.x + list->viewport.w - UI_SCROLL_W,
                             list->viewport.y + dy, list->viewport.h, list->count, list->visible,
                             first);
  }
  ui_layer_set_alpha(1.0f);
}

/* ============================================================================
 * Input
 * ============================================================================ */

/** Move the focus to @row if it is a different, valid row. */
static bool set_focus(UiListPopup *list, int row) {
  if (row < 0)
    row = 0;
  if (row > list->count - 1)
    row = list->count - 1;
  if (row == list->focus)
    return false;
  list->focus = row;
  return true;
}

/** D-pad movement. A list moves by one row; the grid by one cell, or one row of cells. */
static bool move_focus(UiListPopup *list, const UiInput *in) {
  const int cols = list->grid ? UI_LISTPOP_GRID_COLS : 1;
  const int col = list->focus % cols;

  if ((in->repeat & UI_BTN_UP) && list->focus - cols >= 0)
    return set_focus(list, list->focus - cols);
  if ((in->repeat & UI_BTN_DOWN) && list->focus + cols < list->count)
    return set_focus(list, list->focus + cols);
  if (list->grid && (in->repeat & UI_BTN_LEFT) && col > 0)
    return set_focus(list, list->focus - 1);
  if (list->grid && (in->repeat & UI_BTN_RIGHT) && col < cols - 1 && list->focus + 1 < list->count)
    return set_focus(list, list->focus + 1);
  return false;
}

/** Follow a vertical swipe that began on the list: one row per UI_ROW_SWIPE_PX from touch-down. */
static bool follow_swipe(UiListPopup *list, const UiTouch *touch) {
  if (touch->pressed) {
    list->swipe_active = ui_rect_contains(list->viewport, touch->x, touch->y);
    list->swipe_base = list->focus;
  }
  if (!list->swipe_active)
    return false;
  if (!touch->down) {
    list->swipe_active = false;
    return false;
  }
  if (!touch->dragged)
    return false;
  return set_focus(list, list->swipe_base + (int)(-touch->dy / (float)UI_ROW_SWIPE_PX));
}

/** The row or cell under a tap at (@x, @y), or -1. */
static int row_at(const UiListPopup *list, float x, float y) {
  if (list->grid) {
    for (int i = 0; i < list->count; i++) {
      if (ui_rect_contains(cell_rect(list, i), x, y))
        return i;
    }
    return -1;
  }
  const int first = first_visible(list);
  for (int i = first; i < first + list->visible; i++) {
    if (ui_rect_contains(row_rect(list, i), x, y))
      return i;
  }
  return -1;
}

UiEvent ui_list_popup_input(UiListPopup *list, const UiInput *in) {
  if (!ui_list_popup_is_open(list))
    return UI_EVENT_NONE;

  /* The C11 part: starts the enter motion, and reports Cancel and a tap outside the card. */
  if (ui_popup_input(&list->popup, in) == UI_EVENT_CANCELLED)
    return UI_EVENT_CANCELLED;
  if (list->count == 0)
    return UI_EVENT_NONE;

  if (!list->grid && follow_swipe(list, &in->touch))
    return UI_EVENT_MOVED;
  if (move_focus(list, in))
    return UI_EVENT_MOVED;
  if (in->pressed & UI_BTN_CONFIRM) {
    list->activated = list->focus;
    return UI_EVENT_ACTIVATED;
  }
  if (ui_touch_tap(in)) {
    const int row = row_at(list, in->touch.x, in->touch.y);
    if (row >= 0) {
      list->focus = row;
      list->activated = row;
      return UI_EVENT_ACTIVATED;
    }
  }
  return UI_EVENT_NONE;
}

int ui_list_popup_hints(const UiListPopup *list, UiHintItem out[UI_HINT_MAX_ITEMS]) {
  return ui_popup_hints(&list->popup, out);
}
