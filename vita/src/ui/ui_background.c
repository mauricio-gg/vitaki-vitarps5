/**
 * @file ui_background.c
 * @brief C27 Background: the wave behind every screen (SPEC.md C27, FEASIBILITY.md section 3)
 *
 * A port of paintRibbons (docs/design/ui-mocks/xmb-wave.js) at blur level None. The mock draws
 * the ribbons with additive blending; vita2d blends with normal alpha, so the band alpha is
 * multiplied by UI_BG_BLEND_GAIN to land near the same brightness. No GXM state is touched.
 *
 * Draw calls: gradient + glow (1, one triangle list), 5 ribbon fills, 5 highlight lines,
 * dust (1) = 12. The Home vignette is 3 more.
 *
 * vita2d_draw_array hands the vertex pointer straight to the GPU, so it needs GPU-visible
 * memory that outlives the call. Every draw copies its vertices into vita2d's per-frame pool.
 * The pool is reset by vita2d each frame; nothing here allocates on the heap.
 *
 * The blur levels Low, Medium and High (ticket #302) go in ui_background_draw(): the same
 * geometry is drawn into a render target there instead of the screen.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <vita2d.h>
#include <psp2/kernel/processmgr.h>

#include "context.h"
#include "ui/ui_background.h"
#include "ui/ui_constants.h"
#include "ui/ui_theme.h"

/* ============================================================================
 * Geometry sizes
 * ============================================================================ */

#define VERTS_PER_RECT 6
#define VERTS_PER_FAN_TRIANGLE 3
#define VERTS_PER_STRIP_STOP 2
#define PCT_DIVISOR 100.0f

#define BACKDROP_BAND_COUNT 2
#define BACKDROP_GLOW_VERTS (UI_BG_HORIZON_SEGMENTS * VERTS_PER_FAN_TRIANGLE)
#define BACKDROP_VERTS (BACKDROP_BAND_COUNT * VERTS_PER_RECT + BACKDROP_GLOW_VERTS)

#define RIBBON_STRIP_VERTS (UI_BG_COLS * VERTS_PER_STRIP_STOP)
#define DUST_VERTS (UI_BG_DUST_COUNT * VERTS_PER_RECT)

#define VIG_RIGHT_STOPS 3
#define VIG_LEFT_STOPS 3
#define VIG_VERTICAL_STOPS 4

#define COLOUR_BYTE_MAX 255.0f
#define COLOUR_ROUND 0.5f
#define ALPHA_SHIFT 24
#define TWO_PI 6.2831853f
#define HALF 0.5f

/* ============================================================================
 * State: one static ribbon state, plus the prebuilt geometry
 * ============================================================================ */

typedef struct {
  float x;
  float y;
  float radius;
  float phase;
} DustPoint;

typedef struct {
  float pos;
  uint32_t colour;
} GradientStop;

static vita2d_color_vertex s_backdrop[BACKDROP_VERTS];
static vita2d_color_vertex s_fill[UI_BG_RIBBON_COUNT][RIBBON_STRIP_VERTS];
static vita2d_color_vertex s_line[UI_BG_RIBBON_COUNT][RIBBON_STRIP_VERTS];
static vita2d_color_vertex s_dust_verts[DUST_VERTS];
static DustPoint s_dust[UI_BG_DUST_COUNT];

static vita2d_color_vertex s_vig_right[VIG_RIGHT_STOPS * VERTS_PER_STRIP_STOP];
static vita2d_color_vertex s_vig_left[VIG_LEFT_STOPS * VERTS_PER_STRIP_STOP];
static vita2d_color_vertex s_vig_vertical[VIG_VERTICAL_STOPS * VERTS_PER_STRIP_STOP];

static const uint32_t RIBBON_COLOUR[UI_BG_RIBBON_COLOURS] = {UI_BG_RIBBON_1, UI_BG_RIBBON_2,
                                                             UI_BG_RIBBON_3};

static bool s_ready = false;
static bool s_geometry_valid = false;
static bool s_pool_warned = false;
static uint64_t s_last_update_us = 0;

/* ============================================================================
 * Helpers
 * ============================================================================ */

/** Returns @p colour with its alpha byte replaced by @p alpha (0..1, clamped). */
static uint32_t with_alpha(uint32_t colour, float alpha) {
  if (alpha < 0.0f)
    alpha = 0.0f;
  if (alpha > 1.0f)
    alpha = 1.0f;
  uint32_t a = (uint32_t)(alpha * COLOUR_BYTE_MAX + COLOUR_ROUND);
  return UI_COLOR_CLEAR(colour) | (a << ALPHA_SHIFT);
}

static vita2d_color_vertex vertex(float x, float y, uint32_t colour) {
  vita2d_color_vertex v = {x, y, UI_BG_Z, colour};
  return v;
}

/**
 * Copies @p count vertices into the per-frame pool and draws them. Skips the draw, and says so
 * once, when the pool is out of space.
 */
static void draw_pooled(SceGxmPrimitiveType mode, const vita2d_color_vertex *src,
                        unsigned int count) {
  size_t bytes = count * sizeof(vita2d_color_vertex);
  vita2d_color_vertex *dst = vita2d_pool_memalign(bytes, sizeof(vita2d_color_vertex));
  if (!dst) {
    if (!s_pool_warned) {
      LOGE("UI/BACKGROUND vita2d pool exhausted, skipping draw (%zu bytes)", bytes);
      s_pool_warned = true;
    }
    return;
  }
  memcpy(dst, src, bytes);
  vita2d_draw_array(mode, dst, count);
}

/** Writes two triangles covering the full width between y0 and y1, shaded from c0 to c1. */
static void put_band(vita2d_color_vertex *out, float y0, uint32_t c0, float y1, uint32_t c1) {
  const float w = (float)VITA_WIDTH;
  out[0] = vertex(0.0f, y0, c0);
  out[1] = vertex(w, y0, c0);
  out[2] = vertex(0.0f, y1, c1);
  out[3] = vertex(w, y0, c0);
  out[4] = vertex(w, y1, c1);
  out[5] = vertex(0.0f, y1, c1);
}

/**
 * Writes a triangle strip that fades across the screen through @p stops.
 * @horizontal: true when the stops are x positions (a vertical edge each), false for y positions.
 */
static void put_strip(vita2d_color_vertex *out, const GradientStop *stops, int count,
                      bool horizontal) {
  for (int i = 0; i < count; i++) {
    if (horizontal) {
      out[i * 2] = vertex(stops[i].pos, 0.0f, stops[i].colour);
      out[i * 2 + 1] = vertex(stops[i].pos, (float)VITA_HEIGHT, stops[i].colour);
    } else {
      out[i * 2] = vertex(0.0f, stops[i].pos, stops[i].colour);
      out[i * 2 + 1] = vertex((float)VITA_WIDTH, stops[i].pos, stops[i].colour);
    }
  }
}

/** Converts a percentage of the screen width to pixels. */
static float pct_of_width(int pct) {
  return (float)VITA_WIDTH * (float)pct / PCT_DIVISOR;
}

/** Converts a percentage of the screen height to pixels. */
static float pct_of_height(int pct) {
  return (float)VITA_HEIGHT * (float)pct / PCT_DIVISOR;
}

/* ============================================================================
 * Static geometry (built once)
 * ============================================================================ */

/** Gradient bands plus the horizon glow, as one triangle list (the glow blends over the bands). */
static void build_backdrop(void) {
  const float mid_y = pct_of_height(UI_BG_MID_STOP_PCT);
  put_band(&s_backdrop[0], 0.0f, UI_BG_TOP, mid_y, UI_BG_MID);
  put_band(&s_backdrop[VERTS_PER_RECT], mid_y, UI_BG_MID, (float)VITA_HEIGHT, UI_BG_BOTTOM);

  vita2d_color_vertex *fan = &s_backdrop[BACKDROP_BAND_COUNT * VERTS_PER_RECT];
  const uint32_t edge = UI_COLOR_CLEAR(UI_BG_HORIZON);
  for (int i = 0; i < UI_BG_HORIZON_SEGMENTS; i++) {
    float a0 = TWO_PI * (float)i / (float)UI_BG_HORIZON_SEGMENTS;
    float a1 = TWO_PI * (float)(i + 1) / (float)UI_BG_HORIZON_SEGMENTS;
    fan[i * 3] = vertex(UI_BG_HORIZON_X, UI_BG_HORIZON_Y, UI_BG_HORIZON);
    fan[i * 3 + 1] = vertex(UI_BG_HORIZON_X + cosf(a0) * UI_BG_HORIZON_R,
                            UI_BG_HORIZON_Y + sinf(a0) * UI_BG_HORIZON_R, edge);
    fan[i * 3 + 2] = vertex(UI_BG_HORIZON_X + cosf(a1) * UI_BG_HORIZON_R,
                            UI_BG_HORIZON_Y + sinf(a1) * UI_BG_HORIZON_R, edge);
  }
}

/** Places the dust points once, with the mock's linear generator. */
static void build_dust(void) {
  for (int i = 0; i < UI_BG_DUST_COUNT; i++) {
    float rx =
        (float)((i * UI_BG_DUST_X_MUL + UI_BG_DUST_X_ADD) % UI_BG_DUST_MOD) / (float)UI_BG_DUST_MOD;
    float ry =
        (float)((i * UI_BG_DUST_Y_MUL + UI_BG_DUST_Y_ADD) % UI_BG_DUST_MOD) / (float)UI_BG_DUST_MOD;
    s_dust[i].x = rx * (float)VITA_WIDTH;
    s_dust[i].y = ry * (float)VITA_HEIGHT;
    s_dust[i].radius =
        UI_BG_DUST_SIZE_BASE +
        (float)((i * UI_BG_DUST_SIZE_MUL) % UI_BG_DUST_SIZE_STEPS) / UI_BG_DUST_SIZE_DIV;
    s_dust[i].phase = (float)i * UI_BG_DUST_PHASE_STEP;
  }
}

/** The three Home vignette layers, nearest layer last (CSS lists the top layer first). */
static void build_vignette(void) {
  const GradientStop vertical[VIG_VERTICAL_STOPS] = {
      {0.0f, UI_VIG_TOP},
      {pct_of_height(UI_VIG_TOP_END_PCT), UI_COLOR_CLEAR(UI_VIG_TOP)},
      {pct_of_height(UI_VIG_BOTTOM_START_PCT), UI_COLOR_CLEAR(UI_VIG_BOTTOM)},
      {(float)VITA_HEIGHT, UI_VIG_BOTTOM},
  };
  put_strip(s_vig_vertical, vertical, VIG_VERTICAL_STOPS, false);

  const GradientStop left[VIG_LEFT_STOPS] = {
      {0.0f, UI_VIG_STRONG},
      {pct_of_width(UI_VIG_LEFT_MID_PCT), UI_VIG_3},
      {pct_of_width(UI_VIG_LEFT_END_PCT), UI_COLOR_CLEAR(UI_VIG_3)},
  };
  put_strip(s_vig_left, left, VIG_LEFT_STOPS, true);

  const GradientStop right[VIG_RIGHT_STOPS] = {
      {pct_of_width(100 - UI_VIG_RIGHT_END_PCT), UI_COLOR_CLEAR(UI_VIG_2)},
      {pct_of_width(100 - UI_VIG_RIGHT_MID_PCT), UI_VIG_2},
      {(float)VITA_WIDTH, UI_VIG_1},
  };
  put_strip(s_vig_right, right, VIG_RIGHT_STOPS, true);
}

/* ============================================================================
 * Per-update geometry (30 Hz)
 * ============================================================================ */

/** Horizontal fade of a ribbon fill at screen x: 0 at the edges, 1 between the two stops. */
static float fade_at(float x) {
  float u = x / (float)VITA_WIDTH;
  const float in = (float)UI_BG_FADE_IN_PCT / PCT_DIVISOR;
  const float out = (float)UI_BG_FADE_OUT_PCT / PCT_DIVISOR;
  if (u <= 0.0f || u >= 1.0f)
    return 0.0f;
  if (u < in)
    return u / in;
  if (u < out)
    return 1.0f - (u - in) / (out - in) * (1.0f - UI_BG_FADE_OUT_GAIN);
  return UI_BG_FADE_OUT_GAIN * (1.0f - u) / (1.0f - out);
}

/** Recomputes one ribbon's fill strip and highlight strip for time @p t (ms). */
static void update_ribbon(int i, float t) {
  const float k = (float)i / (float)(UI_BG_RIBBON_COUNT - 1);
  const float amp = UI_BG_AMP_BASE + k * UI_BG_AMP_PER_K;
  const float speed = UI_BG_SPEED_BASE + k * UI_BG_SPEED_PER_K;
  const float base = UI_BG_Y_BASE + (float)i * UI_BG_Y_PER_RIBBON + k * UI_BG_Y_PER_K;
  const float alpha = UI_BG_ALPHA_BASE + k * UI_BG_ALPHA_PER_K;
  const float line_w = UI_BG_LINE_W_BASE + k * UI_BG_LINE_W_PER_K;
  const uint32_t line_colour =
      with_alpha(UI_TEXT, UI_BG_LINE_ALPHA_BASE + k * UI_BG_LINE_ALPHA_PER_K);
  const uint32_t colour = RIBBON_COLOUR[i % UI_BG_RIBBON_COLOURS];
  const float fi = (float)i;

  for (int c = 0; c < UI_BG_COLS; c++) {
    float x = (float)(UI_BG_COL_X0 + c * UI_BG_COL_STEP);
    float yc =
        base +
        sinf(x * UI_BG_WAVE1_FREQ + t * speed * UI_BG_WAVE1_TIME_MUL + fi * UI_BG_WAVE1_PHASE) *
            amp +
        sinf(x * UI_BG_WAVE2_FREQ - t * speed + fi) * amp * UI_BG_WAVE2_AMP;
    float th = UI_BG_THICK_BASE + k * UI_BG_THICK_PER_K +
               sinf(x * UI_BG_THICK_FREQ + fi + t * UI_BG_THICK_TIME) *
                   (UI_BG_THICK_MOD_BASE + k * UI_BG_THICK_MOD_PER_K);
    float mid = yc + sinf(x * UI_BG_LINE_FREQ + t * UI_BG_LINE_TIME + fi) * th * UI_BG_LINE_WOBBLE;
    uint32_t edge = with_alpha(colour, alpha * fade_at(x) * UI_BG_BLEND_GAIN);

    s_fill[i][c * 2] = vertex(x, yc - th * HALF, edge);
    s_fill[i][c * 2 + 1] = vertex(x, yc + th * HALF, edge);
    s_line[i][c * 2] = vertex(x, mid - line_w * HALF, line_colour);
    s_line[i][c * 2 + 1] = vertex(x, mid + line_w * HALF, line_colour);
  }
}

/** Recomputes the twinkling, drifting dust quads for time @p t (ms). */
static void update_dust(float t) {
  for (int i = 0; i < UI_BG_DUST_COUNT; i++) {
    const DustPoint *d = &s_dust[i];
    float twinkle = UI_BG_DUST_TWINKLE_BASE +
                    UI_BG_DUST_TWINKLE_AMP * sinf(t * UI_BG_DUST_TWINKLE_TIME + d->phase);
    uint32_t colour = with_alpha(UI_TEXT, twinkle * UI_BG_DUST_ALPHA);
    float x = fmodf(d->x + t * UI_BG_DUST_DRIFT * d->radius, (float)VITA_WIDTH);
    float y = d->y + sinf(t * UI_BG_DUST_BOB_TIME + d->phase) * UI_BG_DUST_BOB_AMP;
    float r = d->radius;
    vita2d_color_vertex *q = &s_dust_verts[i * VERTS_PER_RECT];
    q[0] = vertex(x - r, y - r, colour);
    q[1] = vertex(x + r, y - r, colour);
    q[2] = vertex(x - r, y + r, colour);
    q[3] = vertex(x + r, y - r, colour);
    q[4] = vertex(x + r, y + r, colour);
    q[5] = vertex(x - r, y + r, colour);
  }
}

/** Recomputes every moving vertex for time @p t (ms). */
static void update_geometry(float t) {
  for (int i = 0; i < UI_BG_RIBBON_COUNT; i++)
    update_ribbon(i, t);
  update_dust(t);
  s_geometry_valid = true;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

void ui_background_init(void) {
  if (s_ready)
    return;
  build_backdrop();
  build_dust();
  build_vignette();
  s_ready = true;
}

void ui_background_draw(bool slow) {
  if (!s_ready)
    return;

  uint64_t now_us = sceKernelGetProcessTimeWide();
  uint64_t interval_us = (slow ? UI_BG_UPDATE_SLOW_US : UI_BG_UPDATE_US) - UI_BG_UPDATE_SLACK_US;
  if (!s_geometry_valid || now_us - s_last_update_us >= interval_us) {
    update_geometry((float)now_us * UI_BG_MS_PER_US);
    s_last_update_us = now_us;
  }

  draw_pooled(SCE_GXM_PRIMITIVE_TRIANGLES, s_backdrop, BACKDROP_VERTS);
  for (int i = 0; i < UI_BG_RIBBON_COUNT; i++) {
    draw_pooled(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, s_fill[i], RIBBON_STRIP_VERTS);
    draw_pooled(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, s_line[i], RIBBON_STRIP_VERTS);
  }
  draw_pooled(SCE_GXM_PRIMITIVE_TRIANGLES, s_dust_verts, DUST_VERTS);
}

void ui_background_draw_home_vignette(void) {
  if (!s_ready)
    return;
  draw_pooled(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, s_vig_vertical,
              VIG_VERTICAL_STOPS * VERTS_PER_STRIP_STOP);
  draw_pooled(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, s_vig_left, VIG_LEFT_STOPS * VERTS_PER_STRIP_STOP);
  draw_pooled(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, s_vig_right,
              VIG_RIGHT_STOPS * VERTS_PER_STRIP_STOP);
}
