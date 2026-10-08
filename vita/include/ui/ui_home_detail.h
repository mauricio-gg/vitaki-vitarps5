/**
 * @file ui_home_detail.h
 * @brief The Home screen's detail panel content (SPEC.md C04, section 3.1)
 *
 * Turns the focused Home item into a UiDetailContent from today's data and draws it with the
 * C04 panel: a console from the card cache, a Settings group from the config, a controller
 * preset from its mapping slot, a Profile group from the account and connection facts.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "psn_auth.h"
#include "ui/ui_xmb_list.h"

/** Which Home category the list shows. The values equal the CategoryBar order. */
typedef enum ui_home_detail_source_t {
  UI_HOME_DETAIL_CONSOLES = 0,
  UI_HOME_DETAIL_SETTINGS,
  UI_HOME_DETAIL_CONTROLLER,
  UI_HOME_DETAIL_PROFILE,
} UiHomeDetailSource;

/**
 * Settings groups, in the order of Home's Settings list. The row of the focused Settings item
 * is the group index.
 */
typedef enum ui_settings_group_t {
  UI_SETTINGS_GROUP_VIDEO = 0,
  UI_SETTINGS_GROUP_NETWORK,
  UI_SETTINGS_GROUP_DISPLAY,
  UI_SETTINGS_GROUP_CONTROLS,
  UI_SETTINGS_GROUP_ADVANCED,
  UI_SETTINGS_GROUP_COUNT,
} UiSettingsGroup;

/** Profile groups, in the order of Home's Profile list. */
typedef enum ui_profile_group_t {
  UI_PROFILE_GROUP_ACCOUNT = 0,
  UI_PROFILE_GROUP_CONNECTION,
  UI_PROFILE_GROUP_PSN,
  UI_PROFILE_GROUP_COUNT,
} UiProfileGroup;

/** Controller presets (custom mapping slots), in the order of Home's Controller list. */
#define UI_CONTROLLER_PRESET_COUNT 3

/**
 * ui_home_detail_draw() - Draw the detail panel for the focused row of @list.
 * @source:     The category @list shows.
 * @list:       Home's list; its focused item supplies the title, status and description.
 * @filter_row: Consoles only: the list's first row is the Filter item, not a console.
 *
 * With the Filter row focused the panel shows "Filter", what it does and the console count
 * (4 draws). Draws nothing when the list is empty. Reads config, the card cache and PSN state;
 * changes none.
 */
void ui_home_detail_draw(UiHomeDetailSource source, const UiXmbList *list, bool filter_row);

/**
 * ui_psn_auth_color() - Colour of the PSN Auth value, shared by Home's info panel and the Profile
 * page: OK when signed in, WARN while refreshing, ERR for the states that need action (not
 * authenticated, token expired, error text); 0 for the rest (the row's own colour).
 */
uint32_t ui_psn_auth_color(PsnAuthState state);
