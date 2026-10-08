/**
 * @file ui_toast.h
 * @brief C15 Toast: a short message in a pill at the bottom of the screen (SPEC.md C15)
 *
 * Display-only, with one static timer: there is one toast at a time and a new one replaces the
 * current one. A screen calls ui_toast_show() when something happened and ui_toast_draw() last in
 * its frame, every frame; it draws nothing when no toast is live. The text is measured when the
 * toast is shown, never per frame, and nothing is allocated per frame.
 *
 * Paper cost, while a toast shows: pill fill 3, pill border 3, icon 1 (only with a tone), text 1.
 */

#pragma once

#include <stdbool.h>

/** What a toast is about; it picks the optional icon and its colour. */
typedef enum ui_toast_tone_t {
  UI_TOAST_PLAIN = 0,  ///< no icon
  UI_TOAST_OK,         ///< check icon, OK colour
  UI_TOAST_ERR,        ///< warning icon, ERR colour
} UiToastTone;

/** ui_toast_init() - Load the tone icons. Call once at start-up, after vita2d and ui_shapes_init().
 */
void ui_toast_init(void);

/**
 * ui_toast_show() - Show @text as the toast, replacing the current one, and start its timer.
 * @text: UTF-8, copied; shortened with an ellipsis when it does not fit one line of
 *        UI_TOAST_MAX_W.
 * @tone: The icon and its colour.
 */
void ui_toast_show(const char *text, UiToastTone tone);

/** ui_toast_active() - True from ui_toast_show() until the toast has faded out. A screen whose
 * content sits under the toast (the page description) hides it meanwhile. */
bool ui_toast_active(void);

/** ui_toast_draw() - Draw the live toast at its place in the motion; nothing when there is none. */
void ui_toast_draw(void);
