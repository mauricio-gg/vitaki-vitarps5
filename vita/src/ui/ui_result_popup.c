/**
 * @file ui_result_popup.c
 * @brief C14 ResultPopup (SPEC.md C14)
 */

#include "ui/ui_result_popup.h"

#include "ui/ui_internal.h"
#include "ui/ui_theme.h"

#define ICON_DIR "app0:/assets/icons/"

/** The first button is the one Cancel and a tap outside stand for. */
#define CANCEL_BUTTON 0

static vita2d_texture *s_icon_check = NULL;
static vita2d_texture *s_icon_warn = NULL;

void ui_result_popup_init(void) {
  s_icon_check = ui_load_png_linear(ICON_DIR "popup_check.png");
  s_icon_warn = ui_load_png_linear(ICON_DIR "popup_warn.png");
}

void ui_result_popup_open(UiPopup *popup, const UiResultCopy *copy, const char *body) {
  const bool ok = copy->tone == UI_RESULT_TONE_OK;
  UiPopupSpec spec = {
      .size = UI_POPUP_SIZE_S,
      .icon = ok ? s_icon_check : s_icon_warn,
      .icon_color = ok ? UI_OK : UI_ERR,
      .title = copy->title,
      .body = body,
      .button_count = copy->button_count,
      .default_focus = copy->primary_button,
      .cancel_button = CANCEL_BUTTON,
  };
  for (int i = 0; i < copy->button_count; i++)
    spec.buttons[i] = copy->buttons[i];
  ui_popup_open(popup, &spec);
}

int ui_result_popup_choice(const UiPopup *popup, UiEvent event) {
  if (event == UI_EVENT_ACTIVATED)
    return popup->activated;
  if (event == UI_EVENT_CANCELLED)
    return CANCEL_BUTTON;
  return -1;
}
