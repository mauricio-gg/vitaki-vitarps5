/**
 * @file ui_profile.h
 * @brief The XMB Profile page (SPEC.md section 3.7)
 *
 * A C07 page frame with a GroupList on the left (Account, Connection, PlayStation Network), the
 * identity block under it, a C08 SettingRow pane on the right, a description line for the focused
 * row, the hint row and the C15 toast. Rows are described by data in the group tables of
 * ui_profile.c, as on the Settings page. While a PSN phone login runs, the PlayStation Network
 * group shows the login pane of ui_profile_login.c in place of its rows.
 */

#pragma once

#include "ui/ui_types.h"

/** ui_profile_init() - Set up the components and bake the avatar ring. Call once from init_ui(),
 * after ui_shapes_init(), ui_connecting_init() (which loads the page icons) and ui_toast_init(). */
void ui_profile_init(void);

/**
 * ui_profile_open() - Make @group (a UI_PROFILE_GROUP_* value) the current group, with the focus
 * on the first row of its pane (or on the group list when the group has no rows). Home, and the
 * old sidebar, call this before they switch to UI_SCREEN_TYPE_PROFILE.
 */
void ui_profile_open(int group);

/**
 * ui_profile_frame() - Run one frame of the page: handle input, draw.
 * @return the screen to show next (UI_SCREEN_TYPE_PROFILE to stay, UI_SCREEN_TYPE_MAIN to go back
 *         to Home, which then shows the Profile category on the current group)
 */
UIScreenType ui_profile_frame(void);
