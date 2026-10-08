/**
 * @file ui_reconnecting.h
 * @brief The Reconnecting screen, "Optimizing Stream" (SPEC.md section 3.4)
 *
 * Same page shell as Connecting with spinner art only (halo and the 176 px spinner, no ring or
 * room icon) and a status column. There is no input, no hint row and no way to cancel.
 *
 * Paper cost: wash 1, title 3, top bar 5, halo 1, spinner 1, four text lines 4 = 15, plus the
 * wave 12.
 */

#pragma once

/** ui_reconnecting_frame() - Draw one frame of the screen. Needs ui_connecting_init() first
 * (the ring, halo and spinner textures). */
void ui_reconnecting_frame(void);
