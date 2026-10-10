/**
 * @file ui_arrow.h
 * @brief Baked right-pointing arrow (SPEC.md C20 Callout "L1 -> <output>")
 *
 * Roboto has no U+2192, so the callout's arrow is a white texture baked once and tinted at draw
 * time: a horizontal shaft ending in a two-stroke head, with round ends.
 */

#pragma once

#include <vita2d.h>

/**
 * ui_arrow_bake() - Bake a right-pointing arrow into a new @w x @h white texture.
 * @w:      Texture width in pixels.
 * @h:      Texture height in pixels; the shaft runs along the middle row.
 * @head:   How far the head's strokes reach back from the tip, horizontally and vertically.
 * @stroke: Line thickness in texture pixels.
 *
 * @return the texture, owned by the caller for the life of the app, or NULL when it could not be
 *         allocated (logged by ui_bake_white())
 */
vita2d_texture *ui_arrow_bake(int w, int h, float head, float stroke);
