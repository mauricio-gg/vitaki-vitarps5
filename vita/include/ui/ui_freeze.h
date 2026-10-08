/**
 * @file ui_freeze.h
 * @brief The popup background freeze (SPEC.md C11, FEASIBILITY.md section 4)
 *
 * While a popup is open the screen behind it does not animate and is not drawn live: it is
 * captured once, at half resolution, and every popup frame draws that copy scaled to the full
 * screen (1 draw). Any screen can use it; the popup component (ui_popup.c) drives it.
 *
 * How a capture fits a frame (the menu frame setup, docs/design/architecture.md):
 *   1. A screen opens a popup: ui_freeze_request(). The state is REQUESTED.
 *   2. That frame is drawn as usual, minus the screen's hint row and minus the popup
 *      (ui_freeze_is_capturing() tells the screen to leave them out).
 *   3. draw_ui() swaps buffers and then calls ui_freeze_frame_end(), which waits for the GPU to
 *      finish the frame and averages the finished 960 x 544 frame into the 480 x 272 copy. The
 *      state becomes READY, or UNAVAILABLE when the copy could not be made.
 *   4. From the next frame draw_ui() skips the wave (no prepare pass, no background draw) and
 *      draws ui_freeze_draw() in its place; the screen draws only the popup layer on top.
 *      When UNAVAILABLE the screen behind keeps drawing live and the popup goes over the scrim.
 *   5. The screen closes the popup: ui_freeze_release(). The copy stays in use until the end of
 *      that frame (the frame was already started on the copy), then the state is IDLE again.
 *
 * The texture is created on the first capture and kept for the life of the app.
 */

#pragma once

#include <stdbool.h>

/**
 * ui_freeze_request() - Capture the frame being drawn now, at its end.
 * Does nothing while a freeze is already READY or UNAVAILABLE (a popup opened over a popup keeps
 * the copy it has), and cancels a release that was asked for earlier in the same frame.
 */
void ui_freeze_request(void);

/**
 * ui_freeze_release() - The popup is closed; go back to the live screen after this frame.
 * Safe to call when nothing is frozen.
 */
void ui_freeze_release(void);

/**
 * ui_freeze_is_capturing() - True on the frame that will be captured: the screen must leave out
 * its hint row and the popup.
 */
bool ui_freeze_is_capturing(void);

/**
 * ui_freeze_is_ready() - True while the copy stands in for the screen behind the popup: draw_ui()
 * draws it instead of the wave, and the screen draws only its popup layer.
 */
bool ui_freeze_is_ready(void);

/** ui_freeze_draw() - Draw the copy scaled to the full screen (1 draw). */
void ui_freeze_draw(void);

/**
 * ui_freeze_frame_end() - Finish the freeze bookkeeping of a frame. Call from draw_ui() right
 * after vita2d_swap_buffers(), every frame.
 *
 * Makes the capture when one is requested (logs "UI/POPUP_FREEZE" with how long it took) and
 * applies a release.
 */
void ui_freeze_frame_end(void);
