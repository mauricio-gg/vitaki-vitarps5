/**
 * @file ui_result_popup.h
 * @brief C14 ResultPopup: a C11 popup with a tone icon, a title, a body and one or two buttons
 *        (SPEC.md C14, section 3.3)
 *
 * A configuration of the popup (ui_popup.h), not a second component. The copy comes from
 * ui_result_copy.h (pairing results today; the connection-failure popup uses the same functions
 * with its own copy). A screen opens it with ui_result_popup_open(), drives it the way it drives
 * any popup (ui_popup_input(), ui_popup_draw(), ui_popup_hints(), ui_popup_close()), and turns
 * the popup's event into a button with ui_result_popup_choice().
 */

#pragma once

#include "ui/ui_popup.h"
#include "ui/ui_result_copy.h"

/** ui_result_popup_init() - Load the check and warning icons. Call once at start-up. */
void ui_result_popup_init(void);

/**
 * ui_result_popup_open() - Open @popup as a result popup.
 * @popup: The popup to open (the screen owns it).
 * @copy:  Tone, title and buttons. The strings must outlive the popup (string literals).
 * @body:  The body text; copied.
 *
 * Size S. The tone picks the icon and its colour (check in OK, warning in ERR). Focus starts on
 * the primary (right-most) button; the first button is the cancel button.
 */
void ui_result_popup_open(UiPopup *popup, const UiResultCopy *copy, const char *body);

/**
 * ui_result_popup_choice() - Which button a popup event stands for.
 * @event: What ui_popup_input() returned.
 *
 * A pressed button is itself. Cancel or a tap outside is the first button: Close on a failure,
 * OK on a success. Returns -1 for any other event.
 */
int ui_result_popup_choice(const UiPopup *popup, UiEvent event);
