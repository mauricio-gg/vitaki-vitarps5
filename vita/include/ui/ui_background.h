/**
 * @file ui_background.h
 * @brief C27 Background: the animated wave behind every screen (SPEC.md C27)
 *
 * A vertical gradient, a horizon glow, five ribbons and 36 dust points, all drawn as coloured
 * vertices (no texture). The vertices are recomputed on the CPU at 30 Hz into static arrays and
 * copied into vita2d's per-frame pool memory to draw. About 12 draw calls for the wave at blur
 * level None; the Home vignette adds 3.
 */

#pragma once

#include <stdbool.h>

/**
 * ui_background_init() - Build the static geometry (gradient, glow, dust, vignette).
 * Call once from init_ui(). Safe to call before the first frame is drawn.
 */
void ui_background_init(void);

/**
 * ui_background_draw() - Draw the wave over the whole screen.
 * @slow: true on the Connecting and Reconnecting screens; the vertices then update at half rate.
 *
 * Call first in the frame, after the screen is cleared. Recomputes the ribbon and dust vertices
 * when the update interval has passed, then issues the draws. Performs no heap allocation.
 */
void ui_background_draw(bool slow);

/**
 * ui_background_draw_home_vignette() - Draw the Home vignette over the wave.
 *
 * Three gradient layers (right edge, left edge, top and bottom). Home calls it before drawing
 * its own content.
 */
void ui_background_draw_home_vignette(void);
