/**
 * @file ui_xmb_list.h
 * @brief C02 XmbList: the vertical item column under the focused category (SPEC.md C02)
 *
 * Generic over items: each row has an icon, a name and an optional status line (dot
 * and label in a status colour, plus an optional route label). The focused row sits
 * at UI_LIST_FOCUS_Y with a gap after it; rows above the focus sit UI_LIST_SLIDE higher
 * per step and are fully faded out. The icon never scales.
 *
 * The focused row draws its icon glow and a soft glow behind its title (one draw each, both
 * faded with the row); an unfocused row costs one icon draw and one text run per line.
 *
 * Motion (SPEC 3.1), all time-based and free of extra draws or allocation:
 *   - A focus change from input slides every row to its new place over UI_D2_MS. A change
 *     during a slide restarts from where each row is now, so rows never snap.
 *   - ui_xmb_list_cascade_in() fades the rows in one after another (row i after
 *     UI_CASCADE_STEP_MS x min(i, UI_CASCADE_MAX_ROWS)), each rising UI_RISE_PX over UI_D3_MS.
 *   - visible[] and hit[] always hold the settled layout, so a tap during a slide hits the
 *     row where it will end up.
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
  vita2d_texture *status_glyph;  ///< optional face symbol (baked at UI_FACE_GLYPH_H), drawn 1:1 not
                                 ///< scaled, after the status label
  const char *status_tail;       ///< optional text after the glyph (needs status_glyph)
  bool dim_icon;                 ///< icon at UI_LIST_DIM_PCT (Unpaired, Unavailable)
  bool dim_row;                  ///< whole row at UI_LIST_DIM_PCT (Cooldown)
} UiXmbItem;

typedef struct ui_xmb_list_t {
  const UiXmbItem *items;  ///< borrowed; owned by the screen
  int count;
  int focus;                          ///< focused row, always valid when count > 0
  UiRect visible[UI_LIST_MAX_ITEMS];  ///< row rect for the current focus
  UiRect hit[UI_LIST_MAX_ITEMS];      ///< touch rect; an empty rect for rows that are not shown

  /* Motion state. A zero start timestamp means that motion is not running. */
  uint64_t slide_start_us;              ///< start of the current focus slide
  float from_top[UI_LIST_MAX_ITEMS];    ///< row top (px) when the slide started
  float from_alpha[UI_LIST_MAX_ITEMS];  ///< row opacity (0..1) when the slide started
  uint64_t cascade_start_us;            ///< start of the cascade-in

  /* Vertical swipe (SPEC C02): one row per UI_LIST_ROW_H of finger travel from touch-down. */
  bool swipe_active;  ///< the current touch went down on the list and may swipe it
  int swipe_base;     ///< focus when that touch went down; the swipe moves relative to it
} UiXmbList;

/** ui_xmb_list_init() - Empty list with focus 0. */
void ui_xmb_list_init(UiXmbList *list);

/**
 * ui_xmb_list_set_items() - Point the list at @count items and re-lay out the rows.
 * The focus is kept and clamped to the new range. @count is capped at UI_LIST_MAX_ITEMS.
 */
void ui_xmb_list_set_items(UiXmbList *list, const UiXmbItem *items, int count);

/**
 * ui_xmb_list_set_focus() - Focus row @index (clamped), re-lay out the rows and show them in
 * place at once (any slide in progress is dropped). Input-driven moves slide instead.
 */
void ui_xmb_list_set_focus(UiXmbList *list, int index);

/** ui_xmb_list_cascade_in() - Start the cascade-in of the rows, from the settled layout. */
void ui_xmb_list_cascade_in(UiXmbList *list);

/** ui_xmb_list_draw() - Draw the rows at their current place in the motion. No state change. */
void ui_xmb_list_draw(const UiXmbList *list);

/**
 * ui_xmb_list_input() - Up/Down move (no wrap), Confirm activates, taps focus or activate, and a
 * vertical swipe that starts on the list moves one row per UI_LIST_ROW_H (finger up = next row).
 * A touch that goes down inside the category strip (it overlaps the top of the viewport) belongs
 * to the category swipe and never swipes the list.
 * @return UI_EVENT_MOVED when the focus changed, UI_EVENT_ACTIVATED on Confirm or a tap on
 *         the focused row, otherwise UI_EVENT_NONE. A tap on another row only focuses it.
 */
UiEvent ui_xmb_list_input(UiXmbList *list, const UiInput *in);
