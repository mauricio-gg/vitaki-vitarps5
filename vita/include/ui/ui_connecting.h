/**
 * @file ui_connecting.h
 * @brief The Connecting / Waking screen (SPEC.md section 3.4)
 *
 * Draws the page for the connect in progress and reads its input. The connect logic (wake
 * polling, cooldown deferral, starting the connection thread) stays with the caller,
 * ui_screen_draw_waking() in ui_screens.c.
 *
 * Paper cost: wash 1, title 3, top bar 5, art 7, steps 2 per step (4 for the current one),
 * Cancel button 8 (focused), hint row 2, plus 5 for the Network Unstable pill.
 */

#pragma once

#include <stdbool.h>

/** ui_connecting_init() - Load the room icon and bake the ring, halo and spinners. Call once at
 * start-up, after the fonts and shapes are ready. */
void ui_connecting_init(void);

/**
 * ui_connecting_frame() - Draw one frame of the screen and read its input.
 *
 * @return true when the user asked to cancel: the Cancel hint or the Cancel button was tapped,
 *         or the logical Cancel button was pressed, or Confirm was pressed on the focused
 *         Cancel button. The caller then stops the connect.
 */
bool ui_connecting_frame(void);
