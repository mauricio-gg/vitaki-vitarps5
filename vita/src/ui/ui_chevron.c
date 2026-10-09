/**
 * @file ui_chevron.c
 * @brief Baked left and right chevrons (see ui_chevron.h)
 */

#include "ui/ui_chevron.h"

#include "ui/ui_bake.h"

/** The mock's grid: the right chevron is (9,5) (16,12) (9,19) on 24 x 24; the left one mirrors it.
 */
#define CHEVRON_GRID 24.0f
#define CHEVRON_TIP_X 16.0f
#define CHEVRON_BACK_X 9.0f
#define CHEVRON_TOP_Y 5.0f
#define CHEVRON_MID_Y 12.0f
#define CHEVRON_BOTTOM_Y 19.0f

/** What the alpha function needs to know about the chevron it draws. */
typedef struct chevron_spec_t {
  UiChevronDir dir;
  int art;
  float stroke;
} ChevronSpec;

/** Alpha of the chevron described by @ctx (a ChevronSpec): a stroke of constant width. */
static float chevron_alpha(float px, float py, const void *ctx) {
  const ChevronSpec *spec = (const ChevronSpec *)ctx;
  const bool right = spec->dir == UI_CHEVRON_RIGHT;
  const float scale = (float)spec->art / CHEVRON_GRID;
  const float tip_x = (right ? CHEVRON_TIP_X : CHEVRON_GRID - CHEVRON_TIP_X) * scale;
  const float back_x = (right ? CHEVRON_BACK_X : CHEVRON_GRID - CHEVRON_BACK_X) * scale;
  const float d1 = ui_bake_distance_to_segment(px, py, back_x, CHEVRON_TOP_Y * scale, tip_x,
                                               CHEVRON_MID_Y * scale);
  const float d2 = ui_bake_distance_to_segment(px, py, tip_x, CHEVRON_MID_Y * scale, back_x,
                                               CHEVRON_BOTTOM_Y * scale);
  const float d = d1 < d2 ? d1 : d2;
  return spec->stroke / 2.0f + 0.5f - d;
}

vita2d_texture *ui_chevron_bake(UiChevronDir dir, int art, float stroke) {
  const ChevronSpec spec = {.dir = dir, .art = art, .stroke = stroke};
  return ui_bake_white(art, art, chevron_alpha, &spec);
}
