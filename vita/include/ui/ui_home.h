/**
 * @file ui_home.h
 * @brief The XMB Home screen (SPEC.md section 3.1)
 *
 * Owns a CategoryBar and an XmbList and forwards input to them. Consoles come from
 * the console card cache (ui_console_cards.c); Settings, Controller and Profile are
 * fixed lists whose Confirm opens the screen for that area: the Settings page for a Settings
 * item, the existing screens for Controller and Profile.
 */

#pragma once

#include "host.h"
#include "ui/ui_types.h"

/**
 * ui_home_init() - Load the Home textures and set up the components.
 * Call once from init_ui(), after load_textures() and ui_glow_init().
 */
void ui_home_init(void);

/**
 * ui_home_on_enter() - Make Home the active screen cleanly.
 *
 * Returns the focus manager to the main-content zone (unless a modal is open) and
 * collapses the old wave sidebar, so input can never be left routed to a sidebar
 * that Home does not draw. Call on the frame the UI switches to Home.
 */
void ui_home_on_enter(void);

/**
 * ui_home_frame() - Run one frame of Home: refresh data, handle input, draw.
 * @return the screen to show next (UI_SCREEN_TYPE_MAIN to stay on Home)
 */
UIScreenType ui_home_frame(void);

/**
 * ui_home_select_settings_group() - Put Home on the Settings category with @group's item focused.
 * Call when the Settings page hands back to Home, so Home shows the group the page was on.
 */
void ui_home_select_settings_group(int group);

/**
 * ui_home_focus_console() - Ask Home to focus @host's row the next time it runs, on the Consoles
 * category. For the PIN screen: a console that has just been paired moves up the list (paired
 * consoles sort first), and Home should land on it. A console that is not in the list (a
 * filter hides it) leaves the focus alone.
 */
void ui_home_focus_console(const VitaChiakiHost *host);
