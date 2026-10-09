/**
 * @file ui_profile_login.h
 * @brief The "Phone Login Assist" pane of the Profile page (SPEC.md 3.7, C18, C22)
 *
 * While a PSN phone login runs, the PlayStation Network group shows this pane instead of its rows:
 * a QR code to scan at the left, and beside it a column with three steps and the Code and URL
 * lines; two buttons sit under both. The face buttons do the work (Confirm enters the code, Start
 * shows or hides the QR code, Square cancels the login); the buttons and the QR code are touch
 * targets, so no control takes the controller focus. The login itself (psn_auth) is unchanged.
 *
 * Frame order for the Profile page: ui_profile_login_poll() and ui_profile_login_update() first;
 * while ui_profile_login_busy() the page ignores input; otherwise ui_profile_login_input(), then
 * ui_profile_login_draw() and ui_profile_login_hints().
 */

#pragma once

#include <stdbool.h>

#include "ui/ui_component.h"
#include "ui/ui_hint_row.h"

/** ui_profile_login_showing() - True while a phone login is active. */
bool ui_profile_login_showing(void);

/** ui_profile_login_busy() - True while the system keyboard is open. */
bool ui_profile_login_busy(void);

/**
 * ui_profile_login_poll() - Finish the keyboard when the user has closed it: submit the text,
 * save the config and refresh the internet consoles on success, toast the outcome. Call every
 * frame the page runs.
 */
void ui_profile_login_poll(void);

/**
 * ui_profile_login_update() - Follow the login: show the QR code when a login starts and keep
 * the QR code on the current authorize URL, reporting once with a toast when it cannot be drawn.
 * Call every frame the page runs.
 */
void ui_profile_login_update(void);

/** ui_profile_login_input() - Act on @in while the pane shows (see the file comment). */
void ui_profile_login_input(const UiInput *in);

/** ui_profile_login_hints() - Fill @out with the pane's hints and return how many. */
int ui_profile_login_hints(UiHintItem out[UI_HINT_MAX_ITEMS]);

/** ui_profile_login_draw() - Draw the pane. Paper cost 33 with the QR code shown or hidden. */
void ui_profile_login_draw(void);
