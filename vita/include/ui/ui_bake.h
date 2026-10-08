/**
 * @file ui_bake.h
 * @brief Bake a white texture from a per-pixel alpha function (once, at init)
 *
 * Shapes, rings, halos and spinner arcs are generated here at start-up, never per frame, and
 * tinted at draw time (FEASIBILITY.md section 1).
 */

#pragma once

#include <vita2d.h>

/** Alpha (0..1) of the pixel whose centre is (@px, @py), in pixels from the texture's corner. */
typedef float (*UiBakeAlphaFn)(float px, float py, const void *ctx);

/**
 * ui_bake_white() - Create a @w x @h texture, white, with the alpha @alpha returns per pixel.
 * @ctx: Passed to @alpha.
 *
 * The texture is filtered bilinearly (it is rotated or scaled at draw time).
 *
 * @return the texture, or NULL when it could not be allocated (logged)
 */
vita2d_texture *ui_bake_white(int w, int h, UiBakeAlphaFn alpha, const void *ctx);

/** ui_bake_distance_to_segment() - Distance from (@px, @py) to the segment (@ax, @ay)-(@bx, @by),
 * for alpha functions that draw a stroke (chevrons). */
float ui_bake_distance_to_segment(float px, float py, float ax, float ay, float bx, float by);
