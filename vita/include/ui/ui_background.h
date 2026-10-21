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
 * ui_background_draw_sharp() - Draw the selected scene at full resolution whatever the blur level.
 * @slow: same meaning as for ui_background_draw().
 *
 * For the splash (#398): the same draws and the same time-based animation as ui_background_draw()
 * at level None, so the picture continues unbroken into Home, but it neither reads the blur
 * setting nor creates or renders a blur target, and needs no ui_background_prepare(): the caller
 * resets the pool (vita2d_start_drawing() or vita2d_pool_reset()). Performs no heap allocation.
 */
void ui_background_draw_sharp(bool slow);

/**
 * ui_background_draw_crossfade() - Draw the sharp scene with the user's blur level faded in over
 * it.
 * @blur_k: 0 (the sharp scene only) to 1 (exactly what ui_background_draw() draws).
 *
 * For the splash dissolve (#398), with @blur_k = ui_splash_home_opacity(). In between it draws the
 * sharp scene, then the blur level's target at alpha @blur_k and its veil colours at their own
 * alpha times @blur_k; blur None draws nothing extra. At 0 it never touches the blur targets. If
 * the targets could not be created it keeps the sharp scene and logs one error with the blur level.
 * Needs the chain rendered this frame: from the dissolve start the caller uses
 * ui_background_prepare() (not a bare pool reset), which also creates the targets on that first
 * frame. Performs no heap allocation.
 */
void ui_background_draw_crossfade(float blur_k);

/**
 * ui_background_draw_home_vignette() - Draw the Home vignette over the scene.
 *
 * Three gradient layers (right edge, left edge, top and bottom). Home calls it before drawing
 * its own content. Their alpha is scaled by ui_layer_factor(), so they fade with the rest of Home.
 */
void ui_background_draw_home_vignette(void);
