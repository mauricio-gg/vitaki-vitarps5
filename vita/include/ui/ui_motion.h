/**
 * @file ui_motion.h
 * @brief Time-based motion helpers for the XMB UI (SPEC.md section 1.3)
 *
 * Pure inline functions with no SDK dependency, so the easing can be checked natively.
 * Every XMB animation is a function of elapsed time: a component stores the start
 * timestamp (ui_anim_now_us()) and asks for its progress each frame. Nothing here
 * allocates, and nothing blocks input.
 */

#pragma once

/** Iterations of the bisection that inverts the curve's x(s); 2^-14 is far below a pixel. */
#define UI_BEZIER_ITERATIONS 14

/** One coordinate of a cubic bezier with end points 0 and 1 and inner points @p1, @p2. */
static inline float ui_bezier_coord(float s, float p1, float p2) {
  float inv = 1.0f - s;
  return 3.0f * inv * inv * s * p1 + 3.0f * inv * s * s * p2 + s * s * s;
}

/**
 * ui_bezier_ease() - CSS cubic-bezier(x1, y1, x2, y2) timing function.
 * @t:  Linear progress, clamped to 0..1.
 * @x1: First control point x, in 0..1 (the curve must stay a function of time).
 * @y1: First control point y.
 * @x2: Second control point x, in 0..1.
 * @y2: Second control point y.
 *
 * Finds the curve parameter whose x equals @t by bisection, then returns its y. Returns
 * exactly 0 at t = 0 and exactly 1 at t = 1.
 */
static inline float ui_bezier_ease(float t, float x1, float y1, float x2, float y2) {
  if (t <= 0.0f)
    return 0.0f;
  if (t >= 1.0f)
    return 1.0f;

  float lo = 0.0f;
  float hi = 1.0f;
  for (int i = 0; i < UI_BEZIER_ITERATIONS; i++) {
    float mid = (lo + hi) * 0.5f;
    if (ui_bezier_coord(mid, x1, x2) < t)
      lo = mid;
    else
      hi = mid;
  }
  return ui_bezier_coord((lo + hi) * 0.5f, y1, y2);
}

/**
 * ui_motion_progress() - Linear progress of an animation, clamped to 0..1.
 * @elapsed_ms:  Milliseconds since the animation was started.
 * @delay_ms:    Milliseconds to wait before it begins (0 for none).
 * @duration_ms: Length of the animation; a non-positive value counts as already finished.
 */
static inline float ui_motion_progress(float elapsed_ms, float delay_ms, float duration_ms) {
  if (duration_ms <= 0.0f)
    return 1.0f;
  float t = (elapsed_ms - delay_ms) / duration_ms;
  if (t < 0.0f)
    return 0.0f;
  return t > 1.0f ? 1.0f : t;
}

/**
 * UI_EASE_OUT() - The XMB ease-out curve (ui_theme.h UI_EASE_*) applied to linear progress @t.
 * Expands to a call that needs ui_theme.h included where it is used.
 */
#define UI_EASE_OUT(t) ui_bezier_ease((t), UI_EASE_X1, UI_EASE_Y1, UI_EASE_X2, UI_EASE_Y2)
