/**
 * @file ui_splash.h
 * @brief Splash screen: the logo over the app's own background, then it dissolves (SPEC.md 3.9)
 *
 * Main thread only. The caller owns the vita2d scene (start, end, swap) and draws the app's
 * background (sharp, ui_background_draw_sharp(); from D the user's blur level fades in over it,
 * ui_background_draw_crossfade()) and, once it shows, Home (faded with ui_global_set_alpha());
 * every draw function here draws only the splash's own layers into the scene that is already open:
 * the crisp logo (1 draw) and the particles (1 draw, one triangle list from vita2d's per-frame
 * pool; nothing is allocated per frame).
 *
 * Timeline: the logo fades in over the first 0.45 s and holds. D = max(loading done, 1.0 s) is
 * when the dissolve starts; it runs 1.02 s, the #366 particle motion played backwards. Home starts
 * to show at D + 0.40 s, is fully opaque and takes input at D + 0.70 s. The splash ends at
 * D + 1.02 s. A new button press or touch during the hold, once loading is done, moves D to now
 * (never before the logo has faded in); otherwise presses do nothing.
 *
 * Life cycle: ui_splash_start() -> per frame ui_splash_poll_skip() + ui_splash_draw() until the
 * caller has finished loading (ui_splash_loading_done()) and ui_splash_ready_to_exit() is true ->
 * per frame, together with Home, ui_splash_draw_exit() until it returns true.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <vita2d.h>

#include "ui/ui_asset_preload.h"

/**
 * ui_splash_start() - Begin the splash.
 * @logo:   The logo texture, drawn every frame (not owned; it must outlive the splash).
 * @pixels: The logo's decoded pixels, read once to pick the particles (not owned; the caller
 *          frees them after this returns). Sampling from RAM matters: reading a texture's own
 *          memory, which is uncached GPU memory, costs hundreds of milliseconds.
 *
 * If either is NULL (or @pixels is empty) there is no logo or no particles (an error is logged) but
 * the splash still runs its timeline. Picks the particles into a block allocated here and starts
 * the clock. Logs the particle count.
 */
void ui_splash_start(vita2d_texture *logo, const UiAssetPixels *pixels);

/** ui_splash_sample_us() - How long the particle sampling in ui_splash_start() took. */
uint64_t ui_splash_sample_us(void);

/** ui_splash_active() - True from ui_splash_start() until ui_splash_draw_exit() returns true. */
bool ui_splash_active(void);

/**
 * ui_splash_poll_skip() - Read the controller and the front touch panel; a newly pressed button
 * or a new touch, once loading is done and before the dissolve has started, moves D to now (but
 * not before the logo's fade-in has finished). At any other time it does nothing. It never ends
 * the splash and does not touch context.ui_state.
 */
void ui_splash_poll_skip(void);

/** ui_splash_draw() - Draw the logo (fading in or holding) into the open scene. */
void ui_splash_draw(void);

/**
 * ui_splash_loading_done() - Tell the splash the app has finished loading. Fixes D =
 * max(now, minimum hold), counted from the start.
 */
void ui_splash_loading_done(void);

/**
 * ui_splash_ready_to_exit() - True once loading is done and D has been reached, i.e. the time to
 * stop calling ui_splash_draw() and start calling ui_splash_draw_exit() with Home in the scene.
 */
bool ui_splash_ready_to_exit(void);

/**
 * ui_splash_draw_exit() - Draw the splash's layers as an overlay on what is already in the open
 * scene (the background and Home). Before D it draws the hold; from D the dissolve.
 *
 * Returns true, after freeing the particles, ending the splash and logging PIPE/SPLASH_DISSOLVE,
 * on the frame the dissolve has finished (nothing is drawn then).
 */
bool ui_splash_draw_exit(void);

/**
 * ui_splash_home_opacity() - How opaque Home is this frame, 0..1: 0 until D + 0.40 s, then an
 * ease-out cubic to 1 at D + 0.70 s. Always 1 when the splash is not active.
 */
float ui_splash_home_opacity(void);

/**
 * ui_splash_home_input_enabled() - True when Home may act on buttons and touch: from D + 0.70 s
 * on, and always when the splash is not active. The caller blocks inputs on every frame this is
 * false, so what is still held at that moment stays blocked until released.
 */
bool ui_splash_home_input_enabled(void);

/** ui_splash_start_us() - Process time (microseconds) at which ui_splash_start() ran. */
uint64_t ui_splash_start_us(void);

/** ui_splash_frames_drawn() - Frames drawn by ui_splash_draw() and ui_splash_draw_exit(). */
uint32_t ui_splash_frames_drawn(void);

/** ui_splash_elapsed_us() - Real time since the start, frozen when the splash ends. */
uint64_t ui_splash_elapsed_us(void);
