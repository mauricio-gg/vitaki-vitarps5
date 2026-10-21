/**
 * @file ui_splash_timeline.c
 * @brief The splash's timeline maths (see ui_splash_timeline.h)
 */

#include "ui/ui_splash_timeline.h"

#include "ui/ui_splash_particles.h"

/** The larger of two floats. */
static float max_f(float a, float b) {
  return a > b ? a : b;
}

float ui_splash_logo_in_alpha(const UiSplashTiming *t, float elapsed_ms) {
  return ui_splash_ease_out_cubic(elapsed_ms / t->logo_in_ms);
}

float ui_splash_logo_in_scale(const UiSplashTiming *t, float elapsed_ms) {
  return t->logo_scale0 + (1.0f - t->logo_scale0) * ui_splash_logo_in_alpha(t, elapsed_ms);
}

float ui_splash_dissolve_start_ms(const UiSplashTiming *t, float loaded_ms) {
  return max_f(loaded_ms, t->min_hold_ms);
}

float ui_splash_skip_dissolve_start_ms(const UiSplashTiming *t, float now_ms,
                                       float dissolve_start_ms) {
  const float skipped = max_f(now_ms, t->logo_in_ms);
  return skipped < dissolve_start_ms ? skipped : dissolve_start_ms;
}

float ui_splash_dissolve_clock_ms(const UiSplashTiming *t, float since_d_ms) {
  const float clock = t->dissolve_ms - since_d_ms;
  if (clock < 0.0f)
    return 0.0f;
  return clock > t->dissolve_ms ? t->dissolve_ms : clock;
}

bool ui_splash_dissolve_done(const UiSplashTiming *t, float since_d_ms) {
  return since_d_ms >= t->dissolve_ms;
}

float ui_splash_timeline_home_opacity(const UiSplashTiming *t, float since_d_ms) {
  if (since_d_ms <= t->ui_start_ms)
    return 0.0f;
  return ui_splash_ease_out_cubic((since_d_ms - t->ui_start_ms) / t->ui_fade_ms);
}

bool ui_splash_timeline_home_input_enabled(const UiSplashTiming *t, float since_d_ms) {
  return since_d_ms >= t->ui_start_ms + t->ui_fade_ms;
}
