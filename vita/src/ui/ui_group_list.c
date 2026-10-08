/**
 * @file ui_group_list.c
 * @brief C07 GroupList (SPEC.md C07)
 */

#include "ui/ui_group_list.h"

#include <vita2d.h>

#include "ui/ui_text.h"

void ui_group_list_init(UiGroupList *list, const char *const *names, int count) {
  list->names = names;
  list->count = count > UI_GROUP_MAX ? UI_GROUP_MAX : count;
  list->current = 0;
  list->focused = false;
  for (int i = 0; i < list->count; i++) {
    list->visible[i] = (UiRect){UI_PAGE_GROUP_X, UI_PAGE_BODY_Y + i * UI_GROUP_ROW_H,
                                UI_PAGE_GROUP_W, UI_GROUP_ROW_H};
    list->hit[i] = ui_rect_hit_from_visible(list->visible[i], UI_TAP_MIN, UI_TAP_MIN);
    list->focus_label_w[i] = ui_text_face_width(UI_FACE_T28, names[i]);
  }
}

void ui_group_list_set_current(UiGroupList *list, int index) {
  if (index >= list->count)
    index = list->count - 1;
  list->current = index < 0 ? 0 : index;
}

void ui_group_list_draw(const UiGroupList *list) {
  for (int i = 0; i < list->count; i++) {
    const UiRect row = list->visible[i];
    const bool current = i == list->current;
    const bool big = current && list->focused;
    const UiFace face = big ? UI_FACE_T28 : UI_FACE_T20;
    const int text_x = row.x + UI_GROUP_PAD;

    if (big) {
      const UiRect text = {text_x, row.y + (row.h - UI_T28_LINE) / 2, list->focus_label_w[i],
                           UI_T28_LINE};
      ui_glow_draw_rect(text, UI_GROUP_GLOW,
                        ui_color_scale_alpha(UI_GLOW, (float)UI_GROUP_GLOW_PCT / 100.0f));
    }
    if (current) {
      vita2d_draw_rectangle((float)row.x, (float)(row.y + UI_GROUP_BAR_INSET),
                            (float)UI_GROUP_BAR_W, (float)(row.h - 2 * UI_GROUP_BAR_INSET),
                            UI_TEXT);
    }
    ui_text_draw_face_centered_v(face, text_x, row.y, row.h, current ? UI_TEXT : UI_TEXT_2,
                                 list->names[i]);
  }
}

UiEvent ui_group_list_input(UiGroupList *list, const UiInput *in) {
  if (list->count == 0)
    return UI_EVENT_NONE;

  int target = list->current;
  if (in->pressed & UI_BTN_L)
    target = (list->current + list->count - 1) % list->count;
  else if (in->pressed & UI_BTN_R)
    target = (list->current + 1) % list->count;
  else if ((in->repeat & UI_BTN_UP) && list->current > 0)
    target = list->current - 1;
  else if ((in->repeat & UI_BTN_DOWN) && list->current < list->count - 1)
    target = list->current + 1;
  else if (in->pressed & (UI_BTN_CONFIRM | UI_BTN_RIGHT))
    return UI_EVENT_ACTIVATED;
  else if (in->pressed & UI_BTN_CANCEL)
    return UI_EVENT_CANCELLED;
  else if (ui_touch_tap(in)) {
    for (int i = 0; i < list->count; i++) {
      if (ui_rect_contains(list->hit[i], in->touch.x, in->touch.y)) {
        list->current = i;
        return UI_EVENT_MOVED;
      }
    }
    return UI_EVENT_NONE;
  }

  if (target == list->current)
    return UI_EVENT_NONE;
  list->current = target;
  return UI_EVENT_MOVED;
}
