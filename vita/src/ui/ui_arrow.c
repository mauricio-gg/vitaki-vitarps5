/**
 * @file ui_arrow.c
 * @brief Baked right-pointing arrow (see ui_arrow.h)
 */

#include "ui/ui_arrow.h"

#include "ui/ui_bake.h"

/** What the alpha function needs to know about the arrow it draws. */
typedef struct arrow_spec_t {
  int w;
  int h;
  float head;
  float stroke;
} ArrowSpec;

/** Alpha of the arrow described by @ctx (an ArrowSpec): a stroke of constant width. The tip and the
 * shaft's tail sit half a stroke inside the texture so the round ends are not cut. */
static float arrow_alpha(float px, float py, const void *ctx) {
  const ArrowSpec *spec = (const ArrowSpec *)ctx;
  const float inset = spec->stroke / 2.0f;
  const float tail_x = inset;
  const float tip_x = (float)spec->w - inset;
  const float mid_y = (float)spec->h / 2.0f;
  const float back_x = tip_x - spec->head;
  const float d_shaft = ui_bake_distance_to_segment(px, py, tail_x, mid_y, tip_x, mid_y);
  const float d_upper =
      ui_bake_distance_to_segment(px, py, back_x, mid_y - spec->head, tip_x, mid_y);
  const float d_lower =
      ui_bake_distance_to_segment(px, py, back_x, mid_y + spec->head, tip_x, mid_y);
  float d = d_shaft < d_upper ? d_shaft : d_upper;
  d = d < d_lower ? d : d_lower;
  return spec->stroke / 2.0f + 0.5f - d;
}

vita2d_texture *ui_arrow_bake(int w, int h, float head, float stroke) {
  const ArrowSpec spec = {.w = w, .h = h, .head = head, .stroke = stroke};
  return ui_bake_white(w, h, arrow_alpha, &spec);
}
