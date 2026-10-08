/**
 * @file ui_type_logo.c
 * @brief The console type logo (see ui_type_logo.h)
 */

#include "ui/ui_type_logo.h"

#include "ui/ui_theme.h"

/** Source crop of @kind's wordmark. */
typedef struct logo_crop_t {
  int y;
  int w;
  int h;
} LogoCrop;

static LogoCrop crop_of(UiTypeLogo kind) {
  if (kind == UI_TYPE_LOGO_PS5)
    return (LogoCrop){UI_LOGO_PS5_SRC_Y, UI_LOGO_PS5_SRC_W, UI_LOGO_PS5_SRC_H};
  return (LogoCrop){UI_LOGO_PS4_SRC_Y, UI_LOGO_PS4_SRC_W, UI_LOGO_PS4_SRC_H};
}

int ui_type_logo_width(UiTypeLogo kind, int h) {
  const LogoCrop crop = crop_of(kind);
  return (crop.w * h + crop.h / 2) / crop.h;
}

void ui_type_logo_draw(vita2d_texture *logo, UiTypeLogo kind, int x, int y, int h, uint32_t color) {
  if (!logo)
    return;
  const LogoCrop crop = crop_of(kind);
  const float scale = (float)h / (float)crop.h;
  vita2d_draw_texture_tint_part_scale(logo, (float)x, (float)y, 0.0f, (float)crop.y, (float)crop.w,
                                      (float)crop.h, scale, scale, color);
}
