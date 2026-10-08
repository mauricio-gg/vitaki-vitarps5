/**
 * @file ui_top_bar.h
 * @brief C23 TopBar: logo, cooldown banner slot, Wi-Fi, battery and clock (SPEC.md C23)
 *
 * Display-only. y UI_TOP_Y, height UI_TOPBAR_H, x UI_MARGIN_X..UI_CONTENT_RIGHT. System
 * state (Wi-Fi link, battery percent, local time) is read about once per second and
 * kept as cached text, so a frame formats nothing.
 */

#pragma once

/**
 * ui_top_bar_init() - Load the Wi-Fi and battery glyphs. Call once at start-up, after
 * load_textures() (the logo texture) and ui_text_init().
 */
void ui_top_bar_init(void);

/**
 * ui_top_bar_draw() - Draw the top bar.
 * @banner_reason: Reason for the cooldown banner, or NULL for no banner. While a banner
 *                 shows, Wi-Fi and battery are hidden and the clock stays. Only the
 *                 reason is shortened (with an ellipsis) when the banner would not fit.
 *
 * Paper cost: 5 draws (logo, Wi-Fi, battery, percent, clock); with the banner, 9
 * (logo, banner pill 7, clock).
 */
void ui_top_bar_draw(const char *banner_reason);
