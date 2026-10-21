/**
 * @file ui_splash_timeline.h
 * @brief The splash's timeline maths (SPEC.md 3.9, issue #398): pure functions of time
 *
 * Pure: no SDK dependency, so the rules that matter (when the dissolve may start, when Home may
 * take input, what a skip may do) can be checked natively. Every tunable arrives in
 * UiSplashTiming (ui_splash.c fills it from the UI_SPLASH_* constants in ui_theme.h). All times are
 * milliseconds. "Elapsed" is time since the splash started; "since D" is time since the dissolve
 * started, where D = max(loading done, min hold).
 */

#pragma once

#include <stdbool.h>

/** The timeline's tunables; see the UI_SPLASH_* block in ui_theme.h. */
typedef struct {
  float logo_in_ms;  /**< The logo fades in (and scales up) over this long from the start. */
  float logo_scale0; /**< Logo size at the start, as a fraction of its final size. */
  float min_hold_ms; /**< The dissolve never starts before this much time has elapsed. */
  float dissolve_ms; /**< Length of the dissolve (the last particle is gone by then). */
  float ui_start_ms; /**< Home starts to show this long after D. */
  float ui_fade_ms;  /**< Home's fade-in length; it takes input when the fade is done. */
} UiSplashTiming;

/** ui_splash_logo_in_alpha() - Logo opacity 0..1 at @elapsed_ms (ease-out cubic, then 1). */
float ui_splash_logo_in_alpha(const UiSplashTiming *t, float elapsed_ms);

/** ui_splash_logo_in_scale() - Logo size as a fraction of its final size at @elapsed_ms. */
float ui_splash_logo_in_scale(const UiSplashTiming *t, float elapsed_ms);

/** ui_splash_dissolve_start_ms() - D for loading that finished at @loaded_ms: max(L, min hold). */
float ui_splash_dissolve_start_ms(const UiSplashTiming *t, float loaded_ms);

/**
 * ui_splash_skip_dissolve_start_ms() - D after a press at @now_ms.
 * @dissolve_start_ms: D as it stands.
 *
 * Only call this when loading is already done (a press while loading does nothing). Moves D to
 * @now_ms, but never earlier than the end of the logo's fade-in and never later than D itself; a
 * press at or after D (the dissolve is running) leaves D alone.
 */
float ui_splash_skip_dissolve_start_ms(const UiSplashTiming *t, float now_ms,
                                       float dissolve_start_ms);

/**
 * ui_splash_dissolve_clock_ms() - The #366 particle clock during the dissolve.
 * @since_d_ms: Time since D (negative or zero gives the full length).
 *
 * dissolve_ms - since_d_ms, clamped to 0..dissolve_ms: runs the old assembly clock backwards.
 */
float ui_splash_dissolve_clock_ms(const UiSplashTiming *t, float since_d_ms);

/** ui_splash_dissolve_done() - True once the dissolve has run its full length. */
bool ui_splash_dissolve_done(const UiSplashTiming *t, float since_d_ms);

/**
 * ui_splash_timeline_home_opacity() - Home's opacity 0..1 @since_d_ms after D: 0 until ui_start_ms,
 * then ease-out cubic over ui_fade_ms, then 1.
 */
float ui_splash_timeline_home_opacity(const UiSplashTiming *t, float since_d_ms);

/** ui_splash_timeline_home_input_enabled() - True from ui_start_ms + ui_fade_ms after D on, never
 * before. */
bool ui_splash_timeline_home_input_enabled(const UiSplashTiming *t, float since_d_ms);
