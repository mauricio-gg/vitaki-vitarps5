/**
 * @file ui_background_glyphs.c
 * @brief The Glyphs background: gradient plus falling outlined symbols (ticket #375)
 *
 * A port of paintGlyphs, FALLERS and symPath (docs/design/ui-mocks/xmb-wave.js). Each symbol is
 * stroked with thin quads along its outline: closed outlines (triangle, square) use mitred joins
 * so neighbouring quads never overlap, the circle is a ring of UI_GLYPH_CIRCLE_SEGMENTS segments,
 * and the X is two open strokes whose ends are extended by half the stroke width (the mock's
 * round caps) and may overlap at the centre. Everything lives in static arrays; no heap.
 */

#include <math.h>
#include <stdint.h>
#include <vita2d.h>
#include <shared.h>

#include "context.h"
#include "ui/ui_background_glyphs.h"
#include "ui/ui_constants.h"
#include "ui/ui_theme.h"

#define VERTS_PER_QUAD 6
#define GRADIENT_VERTS 6
#define GLYPH_MAX_POINTS 4
#define GLYPH_MAX_SYMBOL_VERTS (UI_GLYPH_CIRCLE_SEGMENTS * VERTS_PER_QUAD)
#define GLYPH_SYMBOL_VERTS_CAP (UI_GLYPH_COUNT * GLYPH_MAX_SYMBOL_VERTS)

#define TWO_PI_D 6.283185307179586
#define DEG_TO_RAD 0.017453292519943295
#define TWO_POW_32 4294967296.0
#define HALF_STROKE (UI_GLYPH_STROKE_W * 0.5f)

/* The mock's seed keys: r(k) for each field of a faller (key 8 is unused there too). */
enum {
  SEED_X = 1,
  SEED_Y0,
  SEED_VX,
  SEED_VY,
  SEED_SCALE,
  SEED_ROT,
  SEED_ROT_SPEED,
  SEED_UNUSED,
  SEED_LAYER,
  SEED_PHASE,
  SEED_SWAY
};

typedef struct {
  double x, y0, vx, vy, scale, rot, rot_speed, phase, sway, layer;
  int shape;
} Faller;

typedef struct {
  float x, y;
} Point;

static Faller s_fallers[UI_GLYPH_COUNT];
static vita2d_color_vertex s_gradient[GRADIENT_VERTS];
static vita2d_color_vertex s_symbols[GLYPH_SYMBOL_VERTS_CAP];
static unsigned int s_symbol_count = 0;

/* ============================================================================
 * Seeds
 * ============================================================================ */

/**
 * The mock's r(k) for faller @p i: its integer hash, evaluated with the same double arithmetic
 * JavaScript uses (the multiply overflows 2^53, so a 32-bit integer multiply would differ).
 * Returns a value in [0, 1).
 */
static double seed_rand(int i, int k) {
  double h = (double)(i + 1) * UI_GLYPH_HASH_A + (double)k * UI_GLYPH_HASH_B;
  uint32_t u = (uint32_t)fmod(h, TWO_POW_32);
  int32_t x = (int32_t)(u ^ (u >> UI_GLYPH_HASH_SHIFT_1));
  double m = fmod((double)x * UI_GLYPH_HASH_C, TWO_POW_32);
  if (m < 0.0)
    m += TWO_POW_32;
  uint32_t mu = (uint32_t)m;
  return (double)(mu ^ (mu >> UI_GLYPH_HASH_SHIFT_2)) / TWO_POW_32;
}

void ui_glyphs_init(void) {
  for (int i = 0; i < UI_GLYPH_COUNT; i++) {
    Faller *f = &s_fallers[i];
    f->x = seed_rand(i, SEED_X) * VITA_WIDTH;
    f->y0 = seed_rand(i, SEED_Y0) * UI_GLYPH_Y0_RANGE;
    f->vx = (seed_rand(i, SEED_VX) - 0.5) * UI_GLYPH_VX_SPAN;
    f->vy = (seed_rand(i, SEED_VY) + UI_GLYPH_VY_BIAS) * UI_GLYPH_VY_SPAN;
    f->scale = UI_GLYPH_SCALE_BASE + seed_rand(i, SEED_SCALE) * UI_GLYPH_SCALE_SPAN;
    f->rot = seed_rand(i, SEED_ROT) * UI_GLYPH_ROT_RANGE_DEG;
    f->rot_speed = seed_rand(i, SEED_ROT_SPEED) - 0.5;
    f->layer = seed_rand(i, SEED_LAYER) < UI_GLYPH_LAYER_SPLIT ? UI_GLYPH_SLOW_LAYER : 1.0;
    f->phase = seed_rand(i, SEED_PHASE) * UI_GLYPH_PHASE_RANGE;
    f->sway = UI_GLYPH_SWAY_BASE + seed_rand(i, SEED_SWAY);
    f->shape = i % UI_GLYPH_SHAPES;
  }
  LOGD("UI/BACKGROUND Glyphs draws %d symbols", UI_GLYPH_COUNT);
}

/* ============================================================================
 * Gradient
 * ============================================================================ */

static vita2d_color_vertex vertex(float x, float y, uint32_t colour) {
  vita2d_color_vertex v = {x, y, UI_BG_Z, colour};
  return v;
}

/** Linear mix of two theme colours at @p t (0 = a, 1 = b), opaque. */
static uint32_t mix_rgb(UiRgb a, UiRgb b, float t) {
  UiRgb c = {(uint8_t)((float)a.r + ((float)b.r - (float)a.r) * t + 0.5f),
             (uint8_t)((float)a.g + ((float)b.g - (float)a.g) * t + 0.5f),
             (uint8_t)((float)a.b + ((float)b.b - (float)a.b) * t + 0.5f)};
  return ui_rgb_alpha(c, 0xFF);
}

void ui_glyphs_build_gradient(void) {
  const UiTheme *theme = ui_theme_current();
  const float w = (float)VITA_WIDTH;
  const float h = (float)VITA_HEIGHT;
  /* The mock's createLinearGradient(0, h, w, 0): the gradient axis runs from the bottom-left
   * corner (a) to the top-right corner (b). The other two corners project onto that axis at
   * h*h / (w*w + h*h) (top-left) and w*w / (w*w + h*h) (bottom-right); two triangles with those
   * corner colours reproduce a linear gradient exactly. */
  const float len2 = w * w + h * h;
  const float t_top_left = h * h / len2;
  const float t_bottom_right = w * w / len2;
  const uint32_t a = mix_rgb(theme->glyph_bg_a, theme->glyph_bg_b, 0.0f);
  const uint32_t b = mix_rgb(theme->glyph_bg_a, theme->glyph_bg_b, 1.0f);
  const uint32_t tl = mix_rgb(theme->glyph_bg_a, theme->glyph_bg_b, t_top_left);
  const uint32_t br = mix_rgb(theme->glyph_bg_a, theme->glyph_bg_b, t_bottom_right);
  s_gradient[0] = vertex(0.0f, 0.0f, tl);
  s_gradient[1] = vertex(w, 0.0f, b);
  s_gradient[2] = vertex(0.0f, h, a);
  s_gradient[3] = vertex(w, 0.0f, b);
  s_gradient[4] = vertex(w, h, br);
  s_gradient[5] = vertex(0.0f, h, a);
}

/* ============================================================================
 * Stroking
 * ============================================================================ */

/** Appends the quad a0-a1-b1-b0 (two triangles) to @p out; returns the new vertex count. */
static unsigned int put_quad(vita2d_color_vertex *out, unsigned int n, Point a0, Point a1, Point b0,
                             Point b1, uint32_t colour) {
  out[n++] = vertex(a0.x, a0.y, colour);
  out[n++] = vertex(a1.x, a1.y, colour);
  out[n++] = vertex(b0.x, b0.y, colour);
  out[n++] = vertex(a1.x, a1.y, colour);
  out[n++] = vertex(b1.x, b1.y, colour);
  out[n++] = vertex(b0.x, b0.y, colour);
  return n;
}

/** The unit direction from @p a to @p b. The points are never equal for the shapes used here. */
static Point direction(Point a, Point b) {
  float dx = b.x - a.x;
  float dy = b.y - a.y;
  float len = sqrtf(dx * dx + dy * dy);
  Point d = {dx / len, dy / len};
  return d;
}

/**
 * Strokes a closed polygon of @p count points with mitred joins: at every corner the outer and
 * inner points sit on the bisector of the two edge normals, so adjacent quads share an edge and
 * the translucent colour is never drawn twice. Returns the new vertex count.
 */
static unsigned int stroke_closed(vita2d_color_vertex *out, unsigned int n, const Point *pts,
                                  int count, uint32_t colour) {
  Point outer[GLYPH_MAX_POINTS];
  Point inner[GLYPH_MAX_POINTS];
  for (int i = 0; i < count; i++) {
    Point prev = pts[(i + count - 1) % count];
    Point next = pts[(i + 1) % count];
    Point d0 = direction(prev, pts[i]);
    Point d1 = direction(pts[i], next);
    Point n0 = {-d0.y, d0.x};
    Point n1 = {-d1.y, d1.x};
    Point m = {n0.x + n1.x, n0.y + n1.y};
    float ml = sqrtf(m.x * m.x + m.y * m.y);
    m.x /= ml;
    m.y /= ml;
    float reach = HALF_STROKE / (m.x * n0.x + m.y * n0.y);
    outer[i] = (Point){pts[i].x + m.x * reach, pts[i].y + m.y * reach};
    inner[i] = (Point){pts[i].x - m.x * reach, pts[i].y - m.y * reach};
  }
  for (int i = 0; i < count; i++) {
    int j = (i + 1) % count;
    n = put_quad(out, n, outer[i], inner[i], outer[j], inner[j], colour);
  }
  return n;
}

/** Strokes a circle of radius @p r around (cx, cy) as a ring of segments. */
static unsigned int stroke_circle(vita2d_color_vertex *out, unsigned int n, float cx, float cy,
                                  float r, uint32_t colour) {
  const float r_out = r + HALF_STROKE;
  const float r_in = r - HALF_STROKE;
  for (int i = 0; i < UI_GLYPH_CIRCLE_SEGMENTS; i++) {
    float a0 = (float)(TWO_PI_D * i / UI_GLYPH_CIRCLE_SEGMENTS);
    float a1 = (float)(TWO_PI_D * (i + 1) / UI_GLYPH_CIRCLE_SEGMENTS);
    Point o0 = {cx + cosf(a0) * r_out, cy + sinf(a0) * r_out};
    Point i0 = {cx + cosf(a0) * r_in, cy + sinf(a0) * r_in};
    Point o1 = {cx + cosf(a1) * r_out, cy + sinf(a1) * r_out};
    Point i1 = {cx + cosf(a1) * r_in, cy + sinf(a1) * r_in};
    n = put_quad(out, n, o0, i0, o1, i1, colour);
  }
  return n;
}

/** Strokes the open segment a-b, extended by half the stroke width at both ends. */
static unsigned int stroke_segment(vita2d_color_vertex *out, unsigned int n, Point a, Point b,
                                   uint32_t colour) {
  Point d = direction(a, b);
  Point a_ext = {a.x - d.x * HALF_STROKE, a.y - d.y * HALF_STROKE};
  Point b_ext = {b.x + d.x * HALF_STROKE, b.y + d.y * HALF_STROKE};
  Point nrm = {-d.y * HALF_STROKE, d.x * HALF_STROKE};
  return put_quad(
      out, n, (Point){a_ext.x + nrm.x, a_ext.y + nrm.y}, (Point){a_ext.x - nrm.x, a_ext.y - nrm.y},
      (Point){b_ext.x + nrm.x, b_ext.y + nrm.y}, (Point){b_ext.x - nrm.x, b_ext.y - nrm.y}, colour);
}

/** Rotates the local point (x, y) by (c, s) and moves it to (cx, cy). */
static Point place(float x, float y, float c, float s, float cx, float cy) {
  Point p = {cx + x * c - y * s, cy + x * s + y * c};
  return p;
}

/**
 * Appends one symbol of radius @p r, rotated by angle (c = cos, s = sin) around (cx, cy), to
 * @p out starting at vertex @p n. Returns the new vertex count.
 */
static unsigned int stroke_symbol(vita2d_color_vertex *out, unsigned int n, int shape, float r,
                                  float c, float s, float cx, float cy) {
  Point pts[GLYPH_MAX_POINTS];
  switch (shape) {
    case 0: /* triangle, apex up */
      pts[0] = place(0.0f, -r, c, s, cx, cy);
      pts[1] = place(r * UI_GLYPH_TRI_HALF_W, r * UI_GLYPH_TRI_BASE_Y, c, s, cx, cy);
      pts[2] = place(-r * UI_GLYPH_TRI_HALF_W, r * UI_GLYPH_TRI_BASE_Y, c, s, cx, cy);
      return stroke_closed(out, n, pts, 3, UI_GLYPH_TRIANGLE);
    case 1: /* circle: rotation changes nothing */
      return stroke_circle(out, n, cx, cy, r, UI_GLYPH_CIRCLE);
    case 2: /* X: two diagonals */
      n = stroke_segment(out, n, place(-r, -r, c, s, cx, cy), place(r, r, c, s, cx, cy),
                         UI_GLYPH_X);
      return stroke_segment(out, n, place(r, -r, c, s, cx, cy), place(-r, r, c, s, cx, cy),
                            UI_GLYPH_X);
    default: { /* square */
      float h = r * UI_GLYPH_SQUARE_HALF;
      pts[0] = place(-h, -h, c, s, cx, cy);
      pts[1] = place(h, -h, c, s, cx, cy);
      pts[2] = place(h, h, c, s, cx, cy);
      pts[3] = place(-h, h, c, s, cx, cy);
      return stroke_closed(out, n, pts, 4, UI_GLYPH_SQUARE);
    }
  }
}

/* ============================================================================
 * Update and accessors
 * ============================================================================ */

void ui_glyphs_update(double now_ms) {
  const double tick = now_ms / UI_GLYPH_TICK_MS;
  unsigned int n = 0;
  for (int i = 0; i < UI_GLYPH_COUNT; i++) {
    const Faller *f = &s_fallers[i];
    const double travel = UI_GLYPH_MOTION_MUL * f->layer * tick;
    double y = fmod(f->y0 + f->vy * travel, UI_GLYPH_FALL_WRAP) + UI_GLYPH_FALL_TOP;
    double x = fmod(f->x + f->vx * travel, UI_GLYPH_X_WRAP);
    double sx = fmod(x + UI_GLYPH_X_WRAP, UI_GLYPH_X_WRAP) + UI_GLYPH_X_OFFSET +
                sin(f->phase + f->sway * tick / UI_GLYPH_SWAY_TICKS) * UI_GLYPH_SWAY_AMP;
    double angle =
        fmod(f->rot + f->rot_speed * UI_GLYPH_MOTION_MUL * tick, UI_GLYPH_ROT_RANGE_DEG) *
        DEG_TO_RAD;
    float r = (float)(UI_GLYPH_RADIUS_BASE * f->scale * UI_GLYPH_SIZE_MUL);
    n = stroke_symbol(s_symbols, n, f->shape, r, (float)cos(angle), (float)sin(angle), (float)sx,
                      (float)y);
  }
  s_symbol_count = n;
}

const vita2d_color_vertex *ui_glyphs_gradient(unsigned int *count) {
  *count = GRADIENT_VERTS;
  return s_gradient;
}

const vita2d_color_vertex *ui_glyphs_symbols(unsigned int *count) {
  *count = s_symbol_count;
  return s_symbols;
}
