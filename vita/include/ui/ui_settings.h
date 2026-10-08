/**
 * @file ui_settings.h
 * @brief The XMB Settings page (SPEC.md section 3.6)
 *
 * A C07 page frame with a GroupList on the left and a C08 SettingRow pane on the right, a
 * description line for the focused row and the hint row. Every row is described by data (label,
 * kind, description, how to read its value, how to change it) in the group tables of ui_settings.c.
 * Values save as soon as they change.
 */

#pragma once

#include "ui/ui_types.h"

/** ui_settings_init() - Set up the components. Call once from init_ui(), after ui_shapes_init(),
 * ui_home_init() and ui_connecting_init() (which loads the page icons). */
void ui_settings_init(void);

/**
 * ui_settings_open() - Make @group the current group, with the focus on the first row of its pane
 * (or on the group list when the group has no rows). Home calls this before it switches to
 * UI_SCREEN_TYPE_SETTINGS.
 */
void ui_settings_open(int group);

/**
 * ui_settings_frame() - Run one frame of the page: handle input, draw.
 * @return the screen to show next (UI_SCREEN_TYPE_SETTINGS to stay, UI_SCREEN_TYPE_MAIN to go back
 *         to Home, which then shows the Settings category on the current group)
 */
UIScreenType ui_settings_frame(void);
