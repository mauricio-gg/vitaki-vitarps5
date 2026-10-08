/**
 * @file ui_category_bar.c
 * @brief C01 CategoryBar (SPEC.md C01)
 */

#include "ui/ui_category_bar.h"

#include "ui/ui_text.h"

/** Centre x of category @index while @focus is focused: focus at UI_CAT_X0, then fixed steps. */
static int category_center_x(int index, int focus) {
  int d = index - focus;
  if (d == 0)
    return UI_CAT_X0;
  if (d > 0)
    return UI_CAT_X0 + UI_CAT_FAR + (d - 1) * UI_CAT_STEP;
  return UI_CAT_X0 + d * UI_CAT_STEP;
}

/** Scale of category @index relative to its 48 px art. */
static float category_scale(int index, int focus) {
  if (index == focus)
    return UI_CAT_SCALE_ON;
  return index < focus ? UI_CAT_SCALE_LEFT : 1.0f;
}

/** Recompute every visible and hit rect for the current focus. */
static void layout_rects(UiCategoryBar *bar) {
  for (int i = 0; i < UI_CAT_COUNT; i++) {
    int size = (int)((float)UI_CAT_ART * category_scale(i, bar->focus));
    bar->visible[i].w = size;
    bar->visible[i].h = size;
    bar->visible[i].x = category_center_x(i, bar->focus) - size / 2;
    bar->visible[i].y = UI_CAT_Y - size / 2;
    bar->hit[i] = ui_rect_hit_from_visible(bar->visible[i], UI_CAT_HIT, UI_CAT_HIT);
  }
}

void ui_category_bar_init(UiCategoryBar *bar, vita2d_texture *const icons[UI_CAT_COUNT],
                          const char *const labels[UI_CAT_COUNT]) {
  for (int i = 0; i < UI_CAT_COUNT; i++) {
    bar->icons[i] = icons[i];
    bar->labels[i] = labels[i];
  }
  bar->focus = 0;
  layout_rects(bar);
}

void ui_category_bar_draw(const UiCategoryBar *bar) {
  vita2d_texture *glow = ui_glow_texture();
  if (glow) {
    vita2d_draw_texture_tint(glow, (float)(UI_CAT_X0 - UI_LIST_GLOW / 2),
                             (float)(UI_CAT_Y - UI_LIST_GLOW / 2), UI_GLOW);
  }

  for (int i = 0; i < UI_CAT_COUNT; i++) {
    vita2d_texture *icon = bar->icons[i];
    if (!icon)
      continue;
    float scale = (float)bar->visible[i].w / (float)vita2d_texture_get_width(icon);
    uint32_t tint = i == bar->focus ? UI_TEXT : UI_WHITE_PCT(UI_CAT_UNFOCUSED_PCT);
    vita2d_draw_texture_tint_scale(icon, (float)bar->visible[i].x, (float)bar->visible[i].y, scale,
                                   scale, tint);
  }

  const char *label = bar->labels[bar->focus];
  int label_x = UI_CAT_X0 - ui_text_face_width(UI_FACE_T16, label) / 2;
  int label_y = UI_CAT_Y + UI_CAT_BOX / 2 + UI_CAT_LABEL_GAP;
  ui_text_draw_face_centered_v(UI_FACE_T16, label_x, label_y, UI_T16_LINE, UI_TEXT, label);
}

UiEvent ui_category_bar_input(UiCategoryBar *bar, const UiInput *in) {
  int target = bar->focus;

  if ((in->pressed & UI_BTN_L) || (in->repeat & UI_BTN_LEFT))
    target--;
  else if ((in->pressed & UI_BTN_R) || (in->repeat & UI_BTN_RIGHT))
    target++;
  else if (ui_touch_tap(in)) {
    for (int i = 0; i < UI_CAT_COUNT; i++) {
      if (ui_rect_contains(bar->hit[i], in->touch.x, in->touch.y)) {
        target = i;
        break;
      }
    }
  }

  if (target < 0 || target >= UI_CAT_COUNT || target == bar->focus)
    return UI_EVENT_NONE;

  bar->focus = target;
  layout_rects(bar);
  return UI_EVENT_MOVED;
}
