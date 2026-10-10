/**
 * @file ui_background_glyphs.h
 * @brief The Glyphs background geometry (ticket #375, paintGlyphs in
 * docs/design/ui-mocks/xmb-wave.js)
 *
 * Internal to the background module: ui_background.c owns the draw calls and the pool, this file
 * owns the vertices. Two arrays of coloured vertices, both triangle lists:
 *  - the gradient, 6 vertices: theme glyph_bg_a at the bottom-left corner to glyph_bg_b at the
 * top-right;
 *  - the symbols, UI_GLYPH_COUNT outlined shapes (triangle, circle, X, square) as thin quads.
 * Positions are a pure function of time, so nothing accumulates between updates.
 */

#pragma once

#include <vita2d.h>

/**
 * ui_glyphs_init() - Seed the fallers with the mock's hash and log the symbol count.
 * Call once, before the first ui_glyphs_update().
 */
void ui_glyphs_init(void);

/**
 * ui_glyphs_build_gradient() - Rebuild the gradient from the current theme.
 * Call whenever the theme changed. Cheap (6 vertices).
 */
void ui_glyphs_build_gradient(void);

/**
 * ui_glyphs_update() - Recompute the symbol vertices for time @p now_ms (milliseconds).
 * The motion runs in 30 Hz ticks, as in the mock.
 */
void ui_glyphs_update(double now_ms);

/** ui_glyphs_gradient() - The gradient vertices; their count is stored in @p count. */
const vita2d_color_vertex *ui_glyphs_gradient(unsigned int *count);

/** ui_glyphs_symbols() - The symbol vertices of the last update; their count is stored in @p count.
 */
const vita2d_color_vertex *ui_glyphs_symbols(unsigned int *count);
