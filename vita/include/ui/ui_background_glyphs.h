/**
 * @file ui_background_glyphs.h
 * @brief The Glyphs background geometry (ticket #375, paintGlyphs in
 * docs/design/ui-mocks/xmb-wave.js)
 *
 * Internal to the background module: ui_background.c owns the draw calls and the pool, this file
 * owns the vertices. Coloured vertices, all triangle lists:
 *  - the gradient, 6 vertices: theme glyph_bg_a at the bottom-left corner to glyph_bg_b at the
 * top-right;
 *  - the symbols, UI_GLYPH_COUNT outlined shapes (triangle, circle, X, square) as thin quads;
 *  - the soft symbols (ticket #400): the same outlines with feathered edges, for the blur base
 * target only (see ui_background_glyphs.c). Built only when asked for.
 * Positions are a pure function of time, so nothing accumulates between updates.
 */

#pragma once

#include <stdbool.h>
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
 * The motion runs in 30 Hz ticks, as in the mock. The soft set is recomputed only when
 * @p with_soft is true; otherwise its count reads 0 until the next update that asks for it.
 */
void ui_glyphs_update(double now_ms, bool with_soft);

/** ui_glyphs_gradient() - The gradient vertices; their count is stored in @p count. */
const vita2d_color_vertex *ui_glyphs_gradient(unsigned int *count);

/** ui_glyphs_symbols() - The symbol vertices of the last update; their count is stored in @p count.
 */
const vita2d_color_vertex *ui_glyphs_symbols(unsigned int *count);

/**
 * ui_glyphs_symbols_soft() - The feathered symbol vertices of the last update that asked for
 * them; their count is stored in @p count (0 when it did not).
 */
const vita2d_color_vertex *ui_glyphs_symbols_soft(unsigned int *count);
