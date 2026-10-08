/**
 * @file ui_shapes.c
 * @brief Baked rounded shapes (SPEC.md section 1.3)
 *
 * A shape texture is a rounded rectangle of (2 * cap + MID_W) pixels across, cut into
 * a left cap, a MID_W-wide uniform middle and a right cap. Drawing stretches one
 * middle column. The middle is MID_W wide (not 1) so the stretched column never
 * samples a cap pixel.
 */

#include <math.h>
#include <stdbool.h>

#include <vita2d.h>

#include "context.h"
#include "ui/ui_shapes.h"
#include "ui/ui_theme.h"

/** Width of the uniform middle band baked between the caps. */
#define MID_W 3
/** Stroke width of the outline and border variants. */
#define OUTLINE_PX UI_LW1

/** How one shape texture is built. */
typedef struct shape_spec_t {
  int height;   /**< 3-slice: the shape height; 9-slice: unused (square texture) */
  int cap;      /**< corner radius and slice size */
  bool outline; /**< only the 1 px edge ring, not the fill */
} ShapeSpec;

static const ShapeSpec SHAPE3_SPECS[UI_SHAPE3_COUNT] = {
    [UI_SHAPE3_BAR_48] = {UI_SHAPE_H_BAR, UI_R_SM, false},
    [UI_SHAPE3_BAR_56] = {UI_SHAPE_H_BAR_LARGE, UI_R_SM, false},
    [UI_SHAPE3_PILL_48] = {UI_SHAPE_H_BUTTON, UI_SHAPE_H_BUTTON / 2, false},
    [UI_SHAPE3_PILL_32] = {UI_SHAPE_H_PILL, UI_SHAPE_H_PILL / 2, false},
    [UI_SHAPE3_PILL_32_OUTLINE] = {UI_SHAPE_H_PILL, UI_SHAPE_H_PILL / 2, true},
    [UI_SHAPE3_PILL_24] = {UI_SHAPE_H_TRACK, UI_SHAPE_H_TRACK / 2, false},
};

static const ShapeSpec SHAPE9_SPECS[UI_SHAPE9_COUNT] = {
    [UI_SHAPE9_SM] = {0, UI_R_SM, false},
    [UI_SHAPE9_MD] = {0, UI_R_MD, false},
    [UI_SHAPE9_MD_BORDER] = {0, UI_R_MD, true},
};

static vita2d_texture *s_shape3[UI_SHAPE3_COUNT];
static vita2d_texture *s_shape9[UI_SHAPE9_COUNT];

/** Clamp @v to 0..1. */
static float clamp01(float v) {
  return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/**
 * coverage() - Anti-aliased coverage (0..1) of the pixel centred at (px, py) for a rounded
 * rectangle of @w x @h with corner radius @r, from its signed distance to the edge.
 * For an outline, the shape shrunk by OUTLINE_PX is subtracted, leaving a ring.
 */
static float coverage(float px, float py, int w, int h, int r, bool outline) {
  float qx = fabsf(px - (float)w / 2.0f) - ((float)w / 2.0f - (float)r);
  float qy = fabsf(py - (float)h / 2.0f) - ((float)h / 2.0f - (float)r);
  float ox = qx > 0.0f ? qx : 0.0f;
  float oy = qy > 0.0f ? qy : 0.0f;
  float inside = qx > qy ? qx : qy;
  float d = sqrtf(ox * ox + oy * oy) + (inside < 0.0f ? inside : 0.0f) - (float)r;

  float outer = clamp01(0.5f - d);
  if (!outline)
    return outer;
  return outer - clamp01(0.5f - (d + (float)OUTLINE_PX));
}

/** Create a @w x @h white texture whose alpha is the rounded-rectangle coverage. */
static vita2d_texture *bake_shape(int w, int h, int cap, bool outline) {
  vita2d_texture *tex = vita2d_create_empty_texture((unsigned int)w, (unsigned int)h);
  if (!tex) {
    LOGE("ui_shapes: could not allocate %dx%d shape texture", w, h);
    return NULL;
  }
  const int stride_px = (int)vita2d_texture_get_stride(tex) / (int)sizeof(uint32_t);
  uint32_t *pixels = (uint32_t *)vita2d_texture_get_datap(tex);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      float a = coverage((float)x + 0.5f, (float)y + 0.5f, w, h, cap, outline);
      pixels[y * stride_px + x] = RGBA8(255, 255, 255, (int)(a * 255.0f + 0.5f));
    }
  }
  vita2d_texture_set_filters(tex, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
  return tex;
}

void ui_shapes_init(void) {
  for (int i = 0; i < UI_SHAPE3_COUNT; i++) {
    if (!s_shape3[i])
      s_shape3[i] = bake_shape(SHAPE3_SPECS[i].cap * 2 + MID_W, SHAPE3_SPECS[i].height,
                               SHAPE3_SPECS[i].cap, SHAPE3_SPECS[i].outline);
  }
  for (int i = 0; i < UI_SHAPE9_COUNT; i++) {
    int side = SHAPE9_SPECS[i].cap * 2 + MID_W;
    if (!s_shape9[i])
      s_shape9[i] = bake_shape(side, side, SHAPE9_SPECS[i].cap, SHAPE9_SPECS[i].outline);
  }
}

int ui_shape3_height(UiShape3 shape) {
  if (shape < 0 || shape >= UI_SHAPE3_COUNT)
    return 0;
  return SHAPE3_SPECS[shape].height;
}

void ui_shape3_draw(UiShape3 shape, int x, int y, int w, uint32_t color) {
  if (shape < 0 || shape >= UI_SHAPE3_COUNT || !s_shape3[shape])
    return;
  const vita2d_texture *tex = s_shape3[shape];
  const int cap = SHAPE3_SPECS[shape].cap;
  const int h = SHAPE3_SPECS[shape].height;
  if (w < cap * 2)
    w = cap * 2;

  vita2d_draw_texture_tint_part(tex, (float)x, (float)y, 0.0f, 0.0f, (float)cap, (float)h, color);
  if (w > cap * 2) {
    vita2d_draw_texture_tint_part_scale(tex, (float)(x + cap), (float)y, (float)(cap + 1), 0.0f,
                                        1.0f, (float)h, (float)(w - cap * 2), 1.0f, color);
  }
  vita2d_draw_texture_tint_part(tex, (float)(x + w - cap), (float)y, (float)(cap + MID_W), 0.0f,
                                (float)cap, (float)h, color);
}

void ui_shape9_draw(UiShape9 shape, UiRect r, uint32_t color) {
  if (shape < 0 || shape >= UI_SHAPE9_COUNT || !s_shape9[shape])
    return;
  const vita2d_texture *tex = s_shape9[shape];
  const int cap = SHAPE9_SPECS[shape].cap;
  const bool skip_centre = SHAPE9_SPECS[shape].outline;
  if (r.w < cap * 2)
    r.w = cap * 2;
  if (r.h < cap * 2)
    r.h = cap * 2;

  /* Source offsets and sizes of the three bands, and where they land on screen. */
  const int src[3] = {0, cap + 1, cap + MID_W};
  const int src_len[3] = {cap, 1, cap};
  const int dst_x_len[3] = {cap, r.w - cap * 2, cap};
  const int dst_y_len[3] = {cap, r.h - cap * 2, cap};
  const int dst_x[3] = {r.x, r.x + cap, r.x + r.w - cap};
  const int dst_y[3] = {r.y, r.y + cap, r.y + r.h - cap};

  for (int row = 0; row < 3; row++) {
    if (dst_y_len[row] <= 0)
      continue;
    for (int col = 0; col < 3; col++) {
      if (dst_x_len[col] <= 0 || (skip_centre && row == 1 && col == 1))
        continue;
      vita2d_draw_texture_tint_part_scale(
          tex, (float)dst_x[col], (float)dst_y[row], (float)src[col], (float)src[row],
          (float)src_len[col], (float)src_len[row], (float)dst_x_len[col] / (float)src_len[col],
          (float)dst_y_len[row] / (float)src_len[row], color);
    }
  }
}
