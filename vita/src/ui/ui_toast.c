/**
 * @file ui_toast.c
 * @brief C15 Toast (SPEC.md C15)
 */

#include "ui/ui_toast.h"

#include <stdint.h>

#include <vita2d.h>

#include "ui/ui_animation.h"
#include "ui/ui_chrome_layout.h"
#include "ui/ui_component.h"
#include "ui/ui_constants.h"
#include "ui/ui_internal.h"
#include "ui/ui_motion.h"
#include "ui/ui_shapes.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"

#define ICON_DIR "app0:/assets/icons/"

static vita2d_texture *s_icon_check = NULL;
static vita2d_texture *s_icon_warn = NULL;

/** The one toast. It is live while @start_us is not 0. */
static struct {
  char text[UI_TOAST_TEXT_MAX];
  UiToastTone tone;
  uint64_t start_us;
  int box_w;   ///< the pill's width
  int text_w;  ///< the shortened text's width
} s_toast;

void ui_toast_init(void) {
  s_icon_check = ui_load_png_linear(ICON_DIR "popup_check.png");
  s_icon_warn = ui_load_png_linear(ICON_DIR "popup_warn.png");
}

/** Width function for ui_ellipsize_to_fit(): the toast's face. */
static int measure_text(const char *text, void *ctx) {
  (void)ctx;
  return ui_text_face_width(UI_FACE_T20, text);
}

/** The icon of @tone, or NULL for a plain toast (or one whose art did not load). */
static vita2d_texture *tone_icon(UiToastTone tone) {
  if (tone == UI_TOAST_OK)
    return s_icon_check;
  return tone == UI_TOAST_ERR ? s_icon_warn : NULL;
}

void ui_toast_show(const char *text, UiToastTone tone) {
  if (!text)
    return;
  const int icon_w = tone_icon(tone) ? UI_TOAST_ICON + UI_TOAST_ICON_GAP : 0;
  const int text_max_w = UI_TOAST_MAX_W - 2 * UI_TOAST_PAD - icon_w;

  ui_ellipsize_to_fit(text, text_max_w, measure_text, NULL, s_toast.text, sizeof(s_toast.text));
  s_toast.tone = tone;
  s_toast.text_w = ui_text_face_width(UI_FACE_T20, s_toast.text);
  s_toast.box_w = 2 * UI_TOAST_PAD + icon_w + s_toast.text_w;
  s_toast.start_us = ui_anim_now_us();
}

/** How far into its life the toast is, in ms. */
static float toast_age_ms(void) {
  return ui_anim_elapsed_ms(s_toast.start_us);
}

bool ui_toast_active(void) {
  if (s_toast.start_us != 0 && toast_age_ms() >= (float)(UI_TOAST_VISIBLE_MS + UI_TOAST_EXIT_MS))
    s_toast.start_us = 0;
  return s_toast.start_us != 0;
}

void ui_toast_draw(void) {
  if (!ui_toast_active())
    return;

  /* In: rise and fade over UI_TOAST_ENTER_MS. Out: fade only, after UI_TOAST_VISIBLE_MS. */
  const float age = toast_age_ms();
  const float in = UI_EASE_OUT(ui_motion_progress(age, 0.0f, (float)UI_TOAST_ENTER_MS));
  const float out =
      1.0f - ui_motion_progress(age, (float)UI_TOAST_VISIBLE_MS, (float)UI_TOAST_EXIT_MS);
  const int dy = (int)((1.0f - in) * (float)UI_RISE_PX + 0.5f);

  const int x = (VITA_WIDTH - s_toast.box_w) / 2;
  const int y = UI_TOAST_Y + dy;
  ui_layer_set_alpha(in * out);
  ui_shape3_draw(UI_SHAPE3_PILL_48, x, y, s_toast.box_w, UI_PANEL);
  ui_shape3_draw(UI_SHAPE3_PILL_48_OUTLINE, x, y, s_toast.box_w, UI_LINE);

  int text_x = x + UI_TOAST_PAD;
  vita2d_texture *icon = tone_icon(s_toast.tone);
  if (icon) {
    const float scale = (float)UI_TOAST_ICON / (float)vita2d_texture_get_height(icon);
    vita2d_draw_texture_tint_scale(icon, (float)text_x,
                                   (float)(y + (UI_TOAST_H - UI_TOAST_ICON) / 2), scale, scale,
                                   ui_layer_color(s_toast.tone == UI_TOAST_OK ? UI_OK : UI_ERR));
    text_x += UI_TOAST_ICON + UI_TOAST_ICON_GAP;
  }
  ui_text_draw_face_centered_v(UI_FACE_T20, text_x, y, UI_TOAST_H, UI_TEXT, s_toast.text);
  ui_layer_set_alpha(1.0f);
}
