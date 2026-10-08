/**
 * @file ui_gradient.h
 * @brief A horizontal colour gradient over a rectangle, in one draw
 *
 * vita2d has no gradient primitive; this issues one triangle strip with a colour at each stop,
 * like the Home vignette (ui_background.c). The vertices go into vita2d's per-frame pool, so
 * nothing is allocated.
 */

#pragma once

#include <stdint.h>

/** One stop of a horizontal gradient: an x position and the colour there. */
typedef struct ui_gradient_stop_t {
  int x;
  uint32_t color;  ///< ABGR; the colour is interpolated between stops, alpha included
} UiGradientStop;

/** Most stops ui_gradient_draw_h() takes. */
#define UI_GRADIENT_MAX_STOPS 4

/**
 * ui_gradient_draw_h() - Fill y..y+h from the first stop's x to the last stop's x (1 draw).
 * @stops: Stops in increasing x order.
 * @count: Number of stops, 2 to UI_GRADIENT_MAX_STOPS; anything else draws nothing.
 *
 * The colours are multiplied by the current layer opacity (ui_layer_set_alpha()).
 */
void ui_gradient_draw_h(const UiGradientStop *stops, int count, int y, int h);
