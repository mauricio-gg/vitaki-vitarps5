/**
 * @file ui_connecting_ring.c
 * @brief C03 ConnectingRing (SPEC.md C03)
 */

#include "ui/ui_connecting_ring.h"

#include <math.h>

#include "ui/ui_bake.h"
#include "ui/ui_theme.h"

/** Side of the ring texture: the ring plus its glow on every side. */
#define RING_TEX (UI_RING_SIZE + 2 * UI_RING_GLOW)

static vita2d_texture *s_ring;
static vita2d_texture *s_halo;

/**
 * ring_alpha() - A UI_RING_W wide ring on the edge of the UI_RING_SIZE circle, and outside it
 * a glow that falls off quadratically to nothing over UI_RING_GLOW pixels at UI_RING_GLOW_PCT.
 */
static float ring_alpha(float px, float py, const void *ctx) {
  (void)ctx;
  const float centre = (float)RING_TEX / 2.0f;
  const float dx = px - centre;
  const float dy = py - centre;
  const float d = sqrtf(dx * dx + dy * dy);
  const float outer = (float)UI_RING_SIZE / 2.0f;
  const float half_stroke = (float)UI_RING_W / 2.0f;

  const float ring = half_stroke + 0.5f - fabsf(d - (outer - half_stroke));
  float glow = 0.0f;
  if (d > outer) {
    const float t = (d - outer) / (float)UI_RING_GLOW;
    glow = t < 1.0f ? (1.0f - t) * (1.0f - t) * (float)UI_RING_GLOW_PCT / 100.0f : 0.0f;
  }
  return ring > glow ? ring : glow;
}

/** halo_alpha() - Full at the centre, fading linearly to nothing at UI_RING_HALO_FADE_R. */
static float halo_alpha(float px, float py, const void *ctx) {
  (void)ctx;
  const float centre = (float)UI_RING_HALO / 2.0f;
  const float dx = px - centre;
  const float dy = py - centre;
  return 1.0f - sqrtf(dx * dx + dy * dy) / (float)UI_RING_HALO_FADE_R;
}

void ui_connecting_ring_init(void) {
  if (!s_ring)
    s_ring = ui_bake_white(RING_TEX, RING_TEX, ring_alpha, NULL);
  if (!s_halo)
    s_halo = ui_bake_white(UI_RING_HALO, UI_RING_HALO, halo_alpha, NULL);
}

/** Colour of the ring and glow for @state. */
static uint32_t state_color(UiRingState state) {
  switch (state) {
    case UI_RING_STATE_WARN:
      return UI_WARN;
    case UI_RING_STATE_ERR:
      return UI_ERR;
    default:
      return UI_OK;
  }
}

void ui_draw_connecting_ring(int x, int y, int size, vita2d_texture *room, UiRingState state) {
  const float centre_x = (float)x + (float)size / 2.0f;
  const float centre_y = (float)y + (float)size / 2.0f;
  const float scale = (float)size / (float)UI_RING_SIZE;

  if (s_halo) {
    vita2d_draw_texture_tint(s_halo, centre_x - (float)UI_RING_HALO / 2.0f,
                             centre_y - (float)UI_RING_HALO / 2.0f, UI_HALO);
  }
  if (s_ring) {
    const float half = (float)RING_TEX * scale / 2.0f;
    vita2d_draw_texture_tint_scale(s_ring, centre_x - half, centre_y - half, scale, scale,
                                   state_color(state));
  }
  if (room) {
    const float icon = (float)size * (float)UI_RING_ICON_PCT / 100.0f;
    const float icon_scale = icon / (float)vita2d_texture_get_width(room);
    vita2d_draw_texture_tint_scale(room, centre_x - icon / 2.0f, centre_y - icon / 2.0f, icon_scale,
                                   icon_scale, UI_TEXT);
  }
}
