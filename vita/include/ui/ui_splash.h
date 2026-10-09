/**
 * @file ui_splash.h
 * @brief Splash screen: the logo assembles from particles while the app loads (SPEC.md 3.9)
 *
 * Main thread only. The caller owns the vita2d scene (start, end, swap); every draw function here
 * draws into the scene that is already open. A splash frame is 3 draws (black, logo, all the
 * particles in one triangle list from vita2d's per-frame pool; nothing is allocated per frame).
 *
 * Life cycle: ui_splash_start() -> per frame ui_splash_poll_skip() + ui_splash_draw() until the
 * caller has finished loading (ui_splash_loading_done()) and ui_splash_ready_to_exit() is true ->
 * per frame, over the Home screen, ui_splash_draw_exit() until it returns true.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <vita2d.h>

/**
 * ui_splash_start() - Begin the splash.
 * @logo: The loaded logo texture (not owned; it must outlive the splash). NULL, or a texture the
 *        sampler cannot read, gives a plain black splash that still runs its clock.
 *
 * Reads the logo's pixels once, picks the particles into a block allocated here and starts the
 * clock. Logs the particle count; a failure is logged as an error.
 */
void ui_splash_start(vita2d_texture *logo);

/** ui_splash_active() - True from ui_splash_start() until ui_splash_draw_exit() returns true. */
bool ui_splash_active(void);

/**
 * ui_splash_poll_skip() - Read the controller and the front touch panel; a newly pressed button
 * or a new touch skips the assembly (jumps the clock to ui_splash_assembled's state). It never
 * ends the splash and does not touch context.ui_state.
 */
void ui_splash_poll_skip(void);

/** ui_splash_assembled() - True once the assembly time has passed (or been skipped to). */
bool ui_splash_assembled(void);

/**
 * ui_splash_draw() - Draw the splash at full opacity for the current time into the open scene.
 */
void ui_splash_draw(void);

/** ui_splash_loading_done() - Tell the splash the app has finished loading. */
void ui_splash_loading_done(void);

/**
 * ui_splash_ready_to_exit() - True when loading is done and the assembly is complete, i.e. the
 * time to stop calling ui_splash_draw() and start calling ui_splash_draw_exit().
 */
bool ui_splash_ready_to_exit(void);

/**
 * ui_splash_draw_exit() - Draw the splash as an overlay fading out over what is already in the
 * open scene (Home). The fade starts at the first call after ui_splash_ready_to_exit().
 *
 * Returns true, after freeing the particles and ending the splash, on the frame the fade has
 * finished (nothing is drawn then). Before the splash is ready to exit it draws at full opacity
 * and returns false.
 */
bool ui_splash_draw_exit(void);

/** ui_splash_start_us() - Process time (microseconds) at which ui_splash_start() ran. */
uint64_t ui_splash_start_us(void);

/** ui_splash_frames_drawn() - Frames drawn by ui_splash_draw() and ui_splash_draw_exit(). */
uint32_t ui_splash_frames_drawn(void);

/** ui_splash_elapsed_us() - Real time since the start, frozen when the splash ends. */
uint64_t ui_splash_elapsed_us(void);
