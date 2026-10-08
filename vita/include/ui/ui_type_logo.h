/**
 * @file ui_type_logo.h
 * @brief The console type logo (PS5 or PS4 wordmark), white, at any height
 *
 * Display-only draw helper shared by the detail panel (C04) and the Connecting screen. The
 * textures are the app's PS5_logo.png and ps4.png; only the wordmark rows are drawn.
 */

#pragma once

#include <stdint.h>

#include <vita2d.h>

/** Which wordmark to crop from the logo texture. */
typedef enum ui_type_logo_t {
  UI_TYPE_LOGO_PS5 = 0,
  UI_TYPE_LOGO_PS4,
} UiTypeLogo;

/** ui_type_logo_width() - Width of the wordmark drawn @h pixels high. */
int ui_type_logo_width(UiTypeLogo kind, int h);

/**
 * ui_type_logo_draw() - Draw the wordmark with its top-left corner at (@x, @y), @h pixels high.
 * @logo:  PS5_logo.png or ps4.png to match @kind; NULL draws nothing.
 * @color: ABGR tint (the art is white). One draw.
 */
void ui_type_logo_draw(vita2d_texture *logo, UiTypeLogo kind, int x, int y, int h, uint32_t color);
