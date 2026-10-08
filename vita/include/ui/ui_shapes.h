/**
 * @file ui_shapes.h
 * @brief Baked rounded shapes (SPEC.md section 1.3, FEASIBILITY.md section 1)
 *
 * Every rounded shape is a small white texture generated once at init and tinted at
 * draw time; there is never a per-pixel-radius fill. Shapes of fixed height are
 * 3-slice textures (left cap, stretched middle, right cap: 3 draws at any width);
 * shapes of variable height are 9-slice (9 draws at any size). Edges are
 * anti-aliased. Textures use point sampling, so the slices are pixel exact.
 */

#pragma once

#include <stdint.h>

#include "ui/ui_component.h"

/** Fixed-height 3-slice shapes. The height is part of the name. */
typedef enum ui_shape3_t {
  UI_SHAPE3_BAR_48 = 0,      /**< focus bar, R_SM caps */
  UI_SHAPE3_BAR_56,          /**< large focus bar, R_SM caps */
  UI_SHAPE3_PILL_48,         /**< button and toast, pill caps */
  UI_SHAPE3_PILL_48_OUTLINE, /**< 1 px outline of the 48 px pill (text button border) */
  UI_SHAPE3_PILL_32,         /**< pill, pill caps */
  UI_SHAPE3_PILL_32_OUTLINE, /**< 1 px outline of the 32 px pill (warn pill) */
  UI_SHAPE3_PILL_24,         /**< toggle track, pill caps */
  UI_SHAPE3_COUNT
} UiShape3;

/** Variable-height 9-slice shapes. */
typedef enum ui_shape9_t {
  UI_SHAPE9_SM = 0,    /**< R_SM 8: icon cells, QR plate, option rows */
  UI_SHAPE9_MD,        /**< R_MD 16: popups, stats panel */
  UI_SHAPE9_MD_BORDER, /**< 1 px border of the R_MD shape (centre is empty, so 8 draws) */
  UI_SHAPE9_COUNT
} UiShape9;

/** Fixed-size shapes, one texture and one draw each. */
typedef enum ui_shape1_t {
  UI_SHAPE1_PIN_BOX = 0,    /**< the 56 x 72 PIN box, R_SM corners (C17) */
  UI_SHAPE1_PIN_BOX_BORDER, /**< its 2 px outline */
  UI_SHAPE1_COUNT
} UiShape1;

/**
 * ui_shapes_init() - Generate every shape texture once. Safe to call twice.
 * Call after vita2d is initialised and before the first frame that draws a shape.
 */
void ui_shapes_init(void);

/** ui_shape3_height() - The fixed height of a 3-slice shape in pixels. */
int ui_shape3_height(UiShape3 shape);

/**
 * ui_shape3_draw() - Draw a fixed-height shape at any width (3 draws).
 * @shape: Which shape.
 * @x, @y: Top-left corner.
 * @w:     Width; raised to two caps when smaller.
 * @color: ABGR tint (the texture is white, so this is the visible colour and alpha).
 */
void ui_shape3_draw(UiShape3 shape, int x, int y, int w, uint32_t color);

/**
 * ui_shape9_draw() - Draw a variable-size shape (9 draws, 8 for the border variant).
 * @shape: Which shape.
 * @r:     Target rect; each side is raised to two caps when smaller.
 * @color: ABGR tint.
 */
void ui_shape9_draw(UiShape9 shape, UiRect r, uint32_t color);

/**
 * ui_shape1_draw() - Draw a fixed-size shape at (@x, @y) (1 draw).
 * @shape: Which shape.
 * @color: ABGR tint.
 */
void ui_shape1_draw(UiShape1 shape, int x, int y, uint32_t color);
