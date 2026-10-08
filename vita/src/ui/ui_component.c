/**
 * @file ui_component.c
 * @brief Shared draw resources for XMB components (SPEC.md section 2.0, glow rule)
 */

#include <vita2d.h>
#include <psp2/kernel/clib.h>

#include "ui/ui_component.h"
#include "ui/ui_theme.h"

/** Shared white glow texture; created once by ui_glow_init(). */
static vita2d_texture *s_glow = NULL;

/**
 * ui_glow_init() - Bake the shared glow: white, alpha falling off quadratically from
 * the centre to zero at the texture edge, so the glow never reaches the border and is
 * never clipped (SPEC glow rule).
 */
void ui_glow_init(void) {
  if (s_glow)
    return;

  s_glow = vita2d_create_empty_texture(UI_LIST_GLOW, UI_LIST_GLOW);
  if (!s_glow) {
    sceClibPrintf("[ERROR] ui_glow_init: could not allocate %dx%d glow texture\n", UI_LIST_GLOW,
                  UI_LIST_GLOW);
    return;
  }

  const float radius = (float)UI_LIST_GLOW / 2.0f;
  const int stride_px = vita2d_texture_get_stride(s_glow) / (int)sizeof(uint32_t);
  uint32_t *pixels = (uint32_t *)vita2d_texture_get_datap(s_glow);

  for (int y = 0; y < UI_LIST_GLOW; y++) {
    for (int x = 0; x < UI_LIST_GLOW; x++) {
      float dx = ((float)x + 0.5f) - radius;
      float dy = ((float)y + 0.5f) - radius;
      float d = __builtin_sqrtf(dx * dx + dy * dy) / radius;
      float falloff = d >= 1.0f ? 0.0f : (1.0f - d) * (1.0f - d);
      pixels[y * stride_px + x] = RGBA8(255, 255, 255, (int)(falloff * 255.0f + 0.5f));
    }
  }
  vita2d_texture_set_filters(s_glow, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
}

vita2d_texture *ui_glow_texture(void) {
  return s_glow;
}

void ui_glow_draw_rect(UiRect around, int pad, uint32_t color) {
  if (!s_glow)
    return;
  const float scale_x = (float)(around.w + 2 * pad) / (float)UI_LIST_GLOW;
  const float scale_y = (float)(around.h + 2 * pad) / (float)UI_LIST_GLOW;
  vita2d_draw_texture_tint_scale(s_glow, (float)(around.x - pad), (float)(around.y - pad), scale_x,
                                 scale_y, color);
}
