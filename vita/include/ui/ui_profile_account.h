/**
 * @file ui_profile_account.h
 * @brief The Account group of the Profile page: which Account ID is used, the Vita's, the typed one
 *        (SPEC.md 3.7, "Account ID (#201)")
 *
 * The rows of the group are data in ui_profile.c; this module holds what they say and do: the words
 * of each value, which row carries the in-use check, the choice between the Vita's ID and the
 * typed one, and the system keyboard that takes the typed one.
 *
 * Frame order for the Profile page: ui_profile_account_busy() is read before
 * ui_profile_account_poll(), so the frame the keyboard closes in still counts as busy and the page
 * ignores input in it, as it does for the phone login's keyboard.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** ui_profile_account_busy() - True while the system keyboard for the typed ID is open. */
bool ui_profile_account_busy(void);

/**
 * ui_profile_account_poll() - Finish the keyboard when the user has closed it: check the text,
 * keep a valid ID, save the config and toast the outcome. Call every frame the page runs.
 */
void ui_profile_account_poll(void);

/** ui_profile_account_in_use_text() - The Account ID in use, or "Not Set"; borrowed. */
const char *ui_profile_account_in_use_text(void);

/* Row values and descriptions. The returned strings are borrowed and stay valid until the config
 * changes; the descriptions are static and change only with the state of the IDs. */

/** The choice row's value: "Vita" or "Manual", whichever is chosen. */
const char *ui_profile_account_source_text(void);
/** The choice row's description. */
const char *ui_profile_account_source_description(void);
/** The Vita Account ID row's value: the full ID, or the placeholder when there is none. */
const char *ui_profile_account_vita_text(void);
/** The Vita Account ID row's value colour: dim for the placeholder, 0 for the row's own. */
uint32_t ui_profile_account_vita_color(void);
/** The Manual Account ID row's value: the full typed ID, or "Not set". */
const char *ui_profile_account_manual_text(void);
/** The Manual Account ID row's value colour: dim for "Not set", 0 for the row's own. */
uint32_t ui_profile_account_manual_color(void);
/** The Manual Account ID row's description: how to type an ID, worded for "none saved" or "one is".
 */
const char *ui_profile_account_manual_description(void);
/** The Manual Account ID row's hint verb: "Enter ID" or "Edit ID". */
const char *ui_profile_account_manual_hint(void);

/** True when the Vita's ID is the one in use, so its row carries the check. */
bool ui_profile_account_vita_in_use(void);
/** True when the typed ID is the one in use, so its row carries the check. */
bool ui_profile_account_manual_in_use(void);

/**
 * ui_profile_account_change_source() - The choice row was stepped. There are two values, so either
 * way leads to the other. To Vita: save the choice and toast. To Manual with a typed ID saved:
 * the same. To Manual with none: open the keyboard instead; the choice changes only when a valid
 * ID is typed.
 */
void ui_profile_account_change_source(int step);

/** ui_profile_account_edit_manual() - Open the system keyboard on the typed ID. */
void ui_profile_account_edit_manual(void);
