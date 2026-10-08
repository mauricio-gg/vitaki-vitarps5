/**
 * @file ui_bake.c
 * @brief Bake a white texture from a per-pixel alpha function (see ui_bake.h)
 */

#include "ui/ui_bake.h"

#include "context.h"

vita2d_texture *ui_bake_white(int w, int h, UiBakeAlphaFn alpha, const void *ctx) {
  vita2d_texture *tex = vita2d_create_empty_texture((unsigned int)w, (unsigned int)h);
  if (!tex) {
    LOGE("ui_bake: could not allocate %dx%d texture", w, h);
    return NULL;
  }
  const int stride_px = (int)vita2d_texture_get_stride(tex) / (int)sizeof(uint32_t);
  uint32_t *pixels = (uint32_t *)vita2d_texture_get_datap(tex);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      float a = alpha((float)x + 0.5f, (float)y + 0.5f, ctx);
      a = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
      pixels[y * stride_px + x] = RGBA8(255, 255, 255, (int)(a * 255.0f + 0.5f));
    }
  }
  vita2d_texture_set_filters(tex, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
  return tex;
}
