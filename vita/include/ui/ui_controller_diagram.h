/**
 * @file ui_controller_diagram.h
 * @brief PS Vita controller diagram for the Controller page
 *
 * Draws the front or the rear art of the Vita at the box it is given. When the art did not load it
 * falls back to a procedural drawing (body, buttons, rear pad) on a card. The Controller page
 * draws everything else: callouts, the zone grids, the footers and the buttons.
 */

#pragma once

#include <vita2d.h>

#include "ui_types.h"

/** The art, and which side is shown. */
typedef struct diagram_state_t {
  ControllerViewMode mode;  ///< CTRL_VIEW_FRONT or CTRL_VIEW_BACK
  vita2d_texture *texture_front;
  vita2d_texture *texture_back;
} DiagramState;

/** ui_diagram_init() - Load the front and rear art. Call once. */
void ui_diagram_init(DiagramState *state);

/**
 * ui_diagram_render() - Draw the art for @state->mode, scaled to fit the box and centred in it.
 * @x, @y: Top-left corner of the box.
 * @w, @h: Size of the box; the art keeps its shape.
 * Paper cost 1 with the art.
 */
void ui_diagram_render(DiagramState *state, int x, int y, int w, int h);
