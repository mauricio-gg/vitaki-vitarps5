/**
 * @file ui_splash_particles.c
 * @brief Splash particles: sampling and motion (see ui_splash_particles.h)
 */

#include "ui/ui_splash_particles.h"

#include <math.h>
#include <stddef.h>

/* Constants of the maths itself (not tunables): the screen's four sides, the pixel byte layout,
 * the ease-out cubic exponent, the smoothstep polynomial, the bow's peak (4k(1-k) is 1 at k = 1/2)
 * and the half a pixel that rounds to nearest. */
#define SIDE_COUNT 4
#define BYTES_PER_PIXEL 4
#define BYTE_RED 0
#define BYTE_ALPHA 3
#define EASE_EXPONENT 3.0f
#define SMOOTH_A 3.0f
#define SMOOTH_B 2.0f
#define BOW_PEAK 4.0f
#define HALF 0.5f

/** Linear congruential generator of the mock: state in, next number in [0, 1) out. */
static double next_random(uint32_t *state, const UiSplashParams *p) {
  *state = *state * p->lcg_mul + p->lcg_add;
  return (double)*state / p->lcg_range;
}

static float clamp01(float v) {
  return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/** Round to nearest, halves up, like the mock's Math.round. */
static int round_px(float v) {
  return (int)floorf(v + HALF);
}

/** True when display pixel (x, y) maps to a bright, opaque source pixel. */
static bool is_bright(const uint8_t *rgba, int src_w, int src_h, int stride_px,
                      const UiSplashParams *p, int x, int y) {
  /* Centre of the display pixel mapped into the source, nearest neighbour. */
  int sx = (2 * x + 1) * src_w / (2 * p->logo_w);
  int sy = (2 * y + 1) * src_h / (2 * p->logo_h);
  if (sx >= src_w)
    sx = src_w - 1;
  if (sy >= src_h)
    sy = src_h - 1;
  const uint8_t *px = rgba + ((size_t)sy * (size_t)stride_px + (size_t)sx) * BYTES_PER_PIXEL;
  return px[BYTE_ALPHA] > p->alpha_min && px[BYTE_RED] > p->red_min;
}

int ui_splash_sample(const uint8_t *rgba, int src_w, int src_h, int stride_px,
                     const UiSplashParams *p, UiSplashParticle *out, int capacity) {
  if (!rgba || !p || !out || capacity <= 0 || src_w <= 0 || src_h <= 0 || stride_px < src_w ||
      p->logo_w <= 0 || p->logo_h <= 0 || p->cell <= 0)
    return 0;

  uint32_t rng = p->seed;
  int count = 0;

  /* First every cell's pick (the mock draws all of them before any per-particle number). The
   * target is kept in the output array for now. A cell with more than cell_fill of its full area
   * bright gets one pick, chosen at random among its bright pixels by counting, then walking to
   * the chosen one. Edge cells are held to the full cell area, as in the mock. */
  const double needed = (double)p->cell * p->cell * p->cell_fill;
  for (int gy = 0; gy < p->logo_h && count < capacity; gy += p->cell) {
    for (int gx = 0; gx < p->logo_w && count < capacity; gx += p->cell) {
      const int y_end = gy + p->cell < p->logo_h ? gy + p->cell : p->logo_h;
      const int x_end = gx + p->cell < p->logo_w ? gx + p->cell : p->logo_w;
      int hits = 0;
      for (int y = gy; y < y_end; y++)
        for (int x = gx; x < x_end; x++)
          hits += is_bright(rgba, src_w, src_h, stride_px, p, x, y) ? 1 : 0;
      if ((double)hits <= needed)
        continue;

      int pick = (int)floor(next_random(&rng, p) * hits);
      for (int y = gy; y < y_end; y++) {
        for (int x = gx; x < x_end; x++) {
          if (!is_bright(rgba, src_w, src_h, stride_px, p, x, y))
            continue;
          if (pick-- == 0) {
            out[count].tx = (float)(p->logo_x + x);
            out[count].ty = (float)(p->logo_y + y);
            count++;
          }
        }
      }
    }
  }

  /* Then each particle's start, wait, size and bow, in the mock's order. */
  const float w = (float)p->screen_w;
  const float h = (float)p->screen_h;
  for (int i = 0; i < count; i++) {
    UiSplashParticle *q = &out[i];
    const int side = (int)floor(next_random(&rng, p) * SIDE_COUNT);
    const float margin = (float)next_random(&rng, p) * p->margin_range + p->margin_min;
    const float edge = (float)next_random(&rng, p);
    switch (side) {
      case 0:
        q->sx = edge * w;
        q->sy = -margin;
        break;
      case 1:
        q->sx = edge * w;
        q->sy = h + margin;
        break;
      case 2:
        q->sx = -margin;
        q->sy = edge * h;
        break;
      default:
        q->sx = w + margin;
        q->sy = edge * h;
        break;
    }
    q->delay_ms = (float)next_random(&rng, p) * p->stagger_ms;
    q->size = next_random(&rng, p) < p->small_chance ? p->size_small : p->size_large;
    const float curve = ((float)next_random(&rng, p) - HALF) * 2 * p->bow_px;

    /* Unit normal of start->target (0 if they coincide) scaled by the curve, so a frame needs no
     * square root. */
    const float nx = -(q->ty - q->sy);
    const float ny = q->tx - q->sx;
    float len = sqrtf(nx * nx + ny * ny);
    if (len == 0.0f)
      len = 1.0f;
    q->bow_x = nx / len * curve;
    q->bow_y = ny / len * curve;
  }
  return count;
}

/** Flight progress eased out (fast in, soft landing); @u is clamped to 0..1. */
static float ease_out_cubic(float u) {
  return 1.0f - powf(1.0f - clamp01(u), EASE_EXPONENT);
}

bool ui_splash_particle_quad(const UiSplashParticle *p, const UiSplashParams *params, float t_ms,
                             UiSplashQuad *quad) {
  if (!p || !params || !quad || t_ms < p->delay_ms)
    return false;

  const float u = (t_ms - p->delay_ms) / params->flight_ms;
  const float k = ease_out_cubic(u);
  float alpha;
  int grow = 0;
  if (u < 1.0f) {
    alpha = params->alpha_start + (1.0f - params->alpha_start) * k;
  } else {
    /* Arrived: one pixel larger (growing up and left) while it fades into the crisp logo. */
    const float r = (t_ms - p->delay_ms - params->flight_ms) / params->absorb_ms;
    if (r >= 1.0f)
      return false;
    alpha = 1.0f - r;
    grow = 1;
  }

  const float bow = BOW_PEAK * k * (1.0f - k);
  quad->x = round_px(p->sx + (p->tx - p->sx) * k + p->bow_x * bow) - grow;
  quad->y = round_px(p->sy + (p->ty - p->sy) * k + p->bow_y * bow) - grow;
  quad->size = p->size + grow;
  quad->alpha = alpha;
  return true;
}

float ui_splash_logo_alpha(const UiSplashParticle *particles, int count,
                           const UiSplashParams *params, float t_ms) {
  if (!particles || count <= 0 || !params || t_ms >= params->assembly_ms)
    return 1.0f;

  float mean = 0.0f;
  for (int i = 0; i < count; i++)
    mean += clamp01((t_ms - particles[i].delay_ms) / params->flight_ms);
  mean /= (float)count;
  return powf(mean * mean * (SMOOTH_A - SMOOTH_B * mean), params->logo_gamma);
}
