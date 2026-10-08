/**
 * @file ui_background.h
 * @brief C27 Background: the animated wave behind every screen (SPEC.md C27)
 *
 * A vertical gradient, a horizon glow, five ribbons and 36 dust points, all drawn as coloured
 * vertices (no texture). The vertices are recomputed on the CPU at 30 Hz into static arrays and
 * copied into vita2d's per-frame pool memory to draw. About 12 draw calls for the wave at blur
 * level None; the Home vignette adds 3.
 *
 * Blur levels Soft, Strong and Dark (config.background_blur, read every frame) render the same
 * wave into a small render target and draw it upscaled with bilinear filtering plus a veil:
 * 2, 3 and 2 draw calls on the screen. The target pass runs in ui_background_prepare(), before
 * the main scene opens, because vita2d can only draw into a render target in a scene of its own.
 */

#pragma once

#include <stdbool.h>

/**
 * ui_background_init() - Build the static geometry (gradient, glow, dust, vignette).
 * Call once from init_ui(). Safe to call before the first frame is drawn.
 */
void ui_background_init(void);

/**
 * ui_background_prepare() - Start the menu frame's pool and render the blur target if due.
 * @slow: same meaning as for ui_background_draw().
 *
 * Call once per menu frame, immediately before opening the main scene with
 * vita2d_start_drawing_advanced(NULL, 0). It performs the vita2d_pool_reset() that
 * vita2d_start_drawing() would, so the main scene must NOT be opened with vita2d_start_drawing()
 * (a second reset would let the main scene overwrite the vertices the target scene still needs).
 * At blur level None it only resets the pool.
 */
void ui_background_prepare(bool slow);

/**
 * ui_background_draw() - Draw the wave over the whole screen.
 * @slow: true on the Connecting and Reconnecting screens; the vertices then update at half rate.
 *
 * Call first in the frame, after the screen is cleared. At level None it recomputes the ribbon
 * and dust vertices when the update interval has passed, then issues the draws. At the blur
 * levels it draws the target rendered by ui_background_prepare() upscaled, then the veil.
 * Performs no heap allocation.
 */
void ui_background_draw(bool slow);

/**
 * ui_background_draw_home_vignette() - Draw the Home vignette over the wave.
 *
 * Three gradient layers (right edge, left edge, top and bottom). Home calls it before drawing
 * its own content.
 */
void ui_background_draw_home_vignette(void);
