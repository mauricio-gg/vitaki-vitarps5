/**
 * @file ui_spinner.c
 * @brief C16 Spinner (SPEC.md C16)
 */

#include "ui/ui_spinner.h"

#include <math.h>

#include <vita2d.h>
#include <psp2/kernel/processmgr.h>

#include "ui/ui_bake.h"
#include "ui/ui_theme.h"

#define DEG_TO_RAD ((float)M_PI / 180.0f)

typedef struct spinner_spec_t {
  int size;
  int turn_ms;
} SpinnerSpec;

static const SpinnerSpec SPINNER_SPECS[UI_SPINNER_COUNT] = {
    [UI_SPINNER_LARGE] = {UI_SPINNER_BIG, UI_SPINNER_BIG_MS},
    [UI_SPINNER_INLINE] = {UI_SPINNER_SMALL, UI_SPINNER_SMALL_MS},
};

static vita2d_texture *s_arcs[UI_SPINNER_COUNT];

/**
 * arc_alpha() - Coverage of one pixel of the arc: a UI_LW2 wide ring on the edge of the
 * texture, open over the top 360 - UI_SPINNER_ARC_DEG degrees (the mock's transparent top border).
 * @ctx: The texture size (int *).
 */
static float arc_alpha(float px, float py, const void *ctx) {
  const float size = (float)*(const int *)ctx;
  const float half_stroke = (float)UI_LW2 / 2.0f;
  const float r_mid = size / 2.0f - half_stroke;
  const float dx = px - size / 2.0f;
  const float dy = py - size / 2.0f;

  /* Angle clockwise from straight up, -180..180. */
  const float angle = atan2f(dx, -dy) / DEG_TO_RAD;
  if (fabsf(angle) < (360.0f - (float)UI_SPINNER_ARC_DEG) / 2.0f)
    return 0.0f;
  return half_stroke + 0.5f - fabsf(sqrtf(dx * dx + dy * dy) - r_mid);
}

void ui_spinner_init(void) {
  for (int i = 0; i < UI_SPINNER_COUNT; i++) {
    if (!s_arcs[i]) {
      int size = SPINNER_SPECS[i].size;
      s_arcs[i] = ui_bake_white(size, size, arc_alpha, &size);
    }
  }
}

void ui_spinner_draw(UiSpinnerSize size, int cx, int cy) {
  if (size < 0 || size >= UI_SPINNER_COUNT || !s_arcs[size])
    return;
  const uint64_t turn_us = (uint64_t)SPINNER_SPECS[size].turn_ms * 1000ULL;
  const float turns = (float)(sceKernelGetProcessTimeWide() % turn_us) / (float)turn_us;
  const float half = (float)SPINNER_SPECS[size].size / 2.0f;
  vita2d_draw_texture_tint_rotate_hotspot(s_arcs[size], (float)cx, (float)cy,
                                          turns * 2.0f * (float)M_PI, half, half, UI_TEXT);
}
