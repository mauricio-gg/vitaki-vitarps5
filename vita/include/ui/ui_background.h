/**
 * @file ui_background.h
 * @brief C27 Background: the animated picture behind every screen (SPEC.md C27)
 *
 * Two scenes, chosen by context.config.background (read every frame), both drawn as coloured
 * vertices (no texture). The vertices are recomputed on the CPU at 30 Hz into static arrays and
 * copied into vita2d's per-frame pool memory to draw; only the selected scene is computed.
 *  - Waves: a vertical gradient, a horizon glow, five ribbons and 36 dust points. 12 draw calls
 *    at blur level None.
 *  - Glyphs: a corner-to-corner theme gradient and 8 falling outlined symbols (triangle, circle,
 *    X, square; ui_background_glyphs.c). 2 draw calls at blur level None, 402 vertices.
 * The Home vignette adds 3 draw calls over either scene.
 *
 * Blur levels Soft, Strong and Dark (config_background_blur(), read every frame) render the same
 * scene into a 480x272 target, average it 2:1 down to 240x136 (Soft) or on to 60x34 (Strong and
 * Dark), and draw that upscaled with bilinear filtering plus a veil: 2, 3 and 2 draw calls on
 * the screen. The target passes (1 for the wave, 1 to 3 for the averaging) run in
 * ui_background_prepare(), before the main scene opens, because vita2d can only draw into a
 * render target in a scene of its own.
 */

#pragma once

#include <stdbool.h>

/**
 * ui_background_init() - Build the static geometry (gradients, glow, dust, glyph seeds, vignette).
 * Call once from init_ui(). Safe to call before the first frame is drawn.
 */
void ui_background_init(void);

/**
 * ui_background_prepare() - Start the menu frame's pool and render the blur targets if due.
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
 * ui_background_draw() - Draw the selected scene over the whole screen.
 * @slow: true on the Connecting and Reconnecting screens; the vertices then update at half rate.
 *
 * Call first in the frame, after the screen is cleared. At level None it recomputes the scene's
 * moving vertices (ribbons and dust, or the symbols) when the update interval has passed, then
 * issues the draws. At the blur levels it draws the target rendered by ui_background_prepare()
 * upscaled, then the veil. Performs no heap allocation.
 */
void ui_background_draw(bool slow);

/**
 * ui_background_draw_home_vignette() - Draw the Home vignette over the scene.
 *
 * Three gradient layers (right edge, left edge, top and bottom). Home calls it before drawing
 * its own content.
 */
void ui_background_draw_home_vignette(void);
