/**
 * @file ui_xmb_list.h
 * @brief C02 XmbList: the vertical item column under the focused category (SPEC.md C02)
 *
 * Generic over items: each row has an icon, a name and an optional status line (dot
 * and label in a status colour, plus an optional route label). The focused row sits
 * at UI_LIST_FOCUS_Y with a gap after it; rows above the focus are not drawn yet
 * (slide and fade arrive with the animation ticket). The icon never scales.
 */

#pragma once

#include <vita2d.h>

#include "ui/ui_component.h"
#include "ui/ui_theme.h"

/** One row. Strings and the icon are borrowed and must outlive the frame they are drawn in. */
typedef struct ui_xmb_item_t {
  vita2d_texture *icon;  ///< 38 px flat white art; NULL draws no icon
  const char *name;
  const char *status;     ///< status label, or NULL for a row without a status line
  uint32_t status_color;  ///< colour of the dot and the status label
  bool status_dot;        ///< draw the filled status dot before the label
  const char *route;  ///< optional route label drawn after the status (INTERNET colour), or NULL
  bool dim_icon;      ///< icon at UI_LIST_DIM_PCT (Unpaired, Unavailable)
  bool dim_row;       ///< whole row at UI_LIST_DIM_PCT (Cooldown)
} UiXmbItem;

typedef struct ui_xmb_list_t {
  const UiXmbItem *items;  ///< borrowed; owned by the screen
  int count;
  int focus;                          ///< focused row, always valid when count > 0
  UiRect visible[UI_LIST_MAX_ITEMS];  ///< row rect for the current focus
  UiRect hit[UI_LIST_MAX_ITEMS];      ///< touch rect; an empty rect for rows that are not shown
} UiXmbList;

/** ui_xmb_list_init() - Empty list with focus 0. */
void ui_xmb_list_init(UiXmbList *list);

/**
 * ui_xmb_list_set_items() - Point the list at @count items and re-lay out the rows.
 * The focus is kept and clamped to the new range. @count is capped at UI_LIST_MAX_ITEMS.
 */
void ui_xmb_list_set_items(UiXmbList *list, const UiXmbItem *items, int count);

/** ui_xmb_list_set_focus() - Focus row @index (clamped) and re-lay out the rows. */
void ui_xmb_list_set_focus(UiXmbList *list, int index);

/** ui_xmb_list_draw() - Draw the focused row and the rows below it. No state change. */
void ui_xmb_list_draw(const UiXmbList *list);

/**
 * ui_xmb_list_input() - Up/Down move (no wrap), Confirm activates, taps focus or activate.
 * @return UI_EVENT_MOVED when the focus changed, UI_EVENT_ACTIVATED on Confirm or a tap on
 *         the focused row, otherwise UI_EVENT_NONE. A tap on another row only focuses it.
 */
UiEvent ui_xmb_list_input(UiXmbList *list, const UiInput *in);
