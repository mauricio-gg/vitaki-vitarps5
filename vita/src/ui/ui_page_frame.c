/**
 * @file ui_page_frame.c
 * @brief C07 PageShell frame (SPEC.md C07)
 */

#include "ui/ui_page_frame.h"

#include <stdio.h>

#include <vita2d.h>

#include "context.h"
#include "ui/ui_constants.h"
#include "ui/ui_chevron.h"
#include "ui/ui_internal.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"

#define PAGE_ICON_PATH_MAX 64

static const char *const PAGE_ICON_FILES[UI_PAGE_ICON_COUNT] = {
    [UI_PAGE_ICON_LAN] = "lan",         [UI_PAGE_ICON_GLOBE] = "globe",
    [UI_PAGE_ICON_MOON] = "moon",       [UI_PAGE_ICON_WIFI] = "wifi",
    [UI_PAGE_ICON_GEAR] = "gear",       [UI_PAGE_ICON_LOCK] = "lock",
    [UI_PAGE_ICON_PROFILE] = "profile", [UI_PAGE_ICON_CONTROLLER] = "controller",
};

static vita2d_texture *s_icons[UI_PAGE_ICON_COUNT];
static vita2d_texture *s_back_icon;
static const UiRect BACK_HIT = {0, UI_TITLE_Y, UI_TAP_MIN, UI_TAP_MIN};

void ui_page_frame_init(void) {
  char path[PAGE_ICON_PATH_MAX];
  for (int i = 0; i < UI_PAGE_ICON_COUNT; i++) {
    snprintf(path, sizeof(path), "app0:/assets/icons/page_%s.png", PAGE_ICON_FILES[i]);
    s_icons[i] = ui_load_png_linear(path);
  }
  s_back_icon = ui_chevron_bake(UI_CHEVRON_LEFT, UI_PAGE_BACK_ART, UI_PAGE_BACK_STROKE);
}

vita2d_texture *ui_page_frame_icon(UiPageIcon icon) {
  if (icon < 0 || icon >= UI_PAGE_ICON_COUNT)
    return NULL;
  return s_icons[icon];
}

void ui_page_frame_draw(UiPageIcon icon, const char *title) {
  vita2d_draw_rectangle(0.0f, 0.0f, (float)VITA_WIDTH, (float)VITA_HEIGHT, UI_PAGE_WASH);

  if (icon >= 0 && icon < UI_PAGE_ICON_COUNT && s_icons[icon]) {
    vita2d_draw_texture_tint(s_icons[icon], (float)UI_MARGIN_X,
                             (float)(UI_TITLE_Y + (UI_PAGE_TITLE_H - UI_PAGE_ICON) / 2), UI_TEXT);
  }
  ui_text_draw_face_centered_v(UI_FACE_T28, UI_PAGE_TITLE_X, UI_TITLE_Y, UI_PAGE_TITLE_H, UI_TEXT,
                               title);
  vita2d_draw_rectangle((float)UI_MARGIN_X, (float)UI_RULE_Y,
                        (float)(UI_CONTENT_RIGHT - UI_MARGIN_X), (float)UI_LW1, UI_LINE);
}

void ui_page_frame_back_draw(void) {
  if (!s_back_icon)
    return;
  vita2d_draw_texture_tint(s_back_icon, (float)(BACK_HIT.x + (BACK_HIT.w - UI_PAGE_BACK_ART) / 2),
                           (float)(BACK_HIT.y + (BACK_HIT.h - UI_PAGE_BACK_ART) / 2), UI_TEXT_2);
}

bool ui_page_frame_back_tapped(const UiInput *in) {
  return in && ui_touch_tap(in) && ui_rect_contains(BACK_HIT, in->touch.x, in->touch.y);
}
