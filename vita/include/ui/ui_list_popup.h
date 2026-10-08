/**
 * @file ui_list_popup.h
 * @brief C12 ListPopup: a C11 popup whose content is a list of rows or a grid of cells
 *        (SPEC.md C12)
 *
 * Interactive component (SPEC 2.0): a struct plus open (the init), draw and input. A configuration
 * of the popup (ui_popup.h), not a second component: UiListPopup owns a UiPopup with no buttons
 * and draws its rows into the popup's content area. A screen owns one as a member (a zeroed one
 * is closed) and, every frame while it is open:
 *
 *   1. converts a tapped hint to a button press (ui_hint_row_tap) and calls ui_list_popup_input();
 *   2. acts on the event: UI_EVENT_ACTIVATED means row list->activated was chosen,
 *      UI_EVENT_CANCELLED means Cancel or a tap outside the card; the popup stays open until the
 *      screen calls ui_list_popup_close();
 *   3. draws ui_list_popup_draw() (it draws the C11 frame as well) and then the hint row from
 *      ui_list_popup_hints().
 *
 * Opening it freezes the screen behind it exactly as any C11 popup does (ui_freeze.h).
 *
 * List: rows of UI_LISTPOP_ROW_H with a label, an optional right label and an optional check for
 * the current value. It scrolls when there are more rows than fit (an L popup shows
 * UI_LISTPOP_MAX_VISIBLE), keeping the focused row near the middle, with a C24 indicator.
 * Grid: UI_LISTPOP_GRID_COLS cells wide of icon and label; focus and "current" are independent.
 */

#pragma once

#include <stdbool.h>

#include <vita2d.h>

#include "ui/ui_component.h"
#include "ui/ui_hint_row.h"
#include "ui/ui_popup.h"
#include "ui/ui_theme.h"

/** One row or cell. The strings are copied; the icon is borrowed and must outlive the popup. */
typedef struct ui_list_row_t {
  char label[UI_LISTPOP_TEXT_MAX];
  char right_label[UI_LISTPOP_TEXT_MAX];  ///< list only; empty for none
  vita2d_texture *icon;                   ///< grid only, drawn UI_LISTPOP_CELL_ICON px
  bool current;                           ///< the value in use: gets the check mark
} UiListRow;

/** What a list popup shows. Strings in the spec are copied except the hint labels, which are
 * borrowed (use string literals). */
typedef struct ui_list_popup_spec_t {
  UiPopupSize size;  ///< S for two rows, M for the grid, L for up to UI_LISTPOP_MAX_VISIBLE rows
  const char *title;
  const char *subtitle;       ///< optional
  const char *confirm_label;  ///< hint for Confirm; NULL reads "Select"
  const char *cancel_label;   ///< hint for Cancel; NULL reads "Cancel"
  bool grid;                  ///< a grid of cells instead of a list
  const UiListRow *rows;      ///< copied; at most UI_LISTPOP_MAX_ROWS are kept
  int count;
  int focus;  ///< row focused when it opens; clamped
} UiListPopupSpec;

typedef struct ui_list_popup_t {
  UiPopup popup;  ///< the C11 frame; a closed popup means a closed list popup
  bool grid;
  UiListRow rows[UI_LISTPOP_MAX_ROWS];
  int count;
  int focus;
  int activated;  ///< the row of the last UI_EVENT_ACTIVATED

  /* Layout, computed once in ui_list_popup_open() */
  UiRect viewport;  ///< list: the visible rows; grid: the whole grid
  int visible;      ///< list: rows that fit

  /* Vertical swipe on a list: the row focused at touch-down, and whether this touch is one. */
  int swipe_base;
  bool swipe_active;
} UiListPopup;

/** ui_list_popup_init() - Load the check art. Call once at start-up. */
void ui_list_popup_init(void);

/**
 * ui_list_popup_open() - Fill @list from @spec and open it.
 * Requests the background freeze like ui_popup_open(): the frame this is called in must be drawn
 * without the screen's hint row and without the popup (ui_freeze_is_capturing()).
 */
void ui_list_popup_open(UiListPopup *list, const UiListPopupSpec *spec);

/** ui_list_popup_close() - Close @list and release the background freeze. Safe when closed. */
void ui_list_popup_close(UiListPopup *list);

/** ui_list_popup_is_open() - True from ui_list_popup_open() until ui_list_popup_close(). */
static inline bool ui_list_popup_is_open(const UiListPopup *list) {
  return ui_popup_is_open(&list->popup);
}

/**
 * ui_list_popup_draw() - Draw the C11 frame (scrim, card, header) and the rows or cells on it,
 * rising and fading in with it. No state change.
 * Paper cost on top of ui_popup_draw(), list: per row its label (1) and right label (1), a check
 * (1), a divider (1) or, for the focused row, glow 1 and bar 3; C24 adds 2. Grid: per cell icon 1
 * and label 1, a check 1; the focused cell adds glow 1 and fill 1.
 */
void ui_list_popup_draw(const UiListPopup *list);

/**
 * ui_list_popup_input() - Up/Down (grid: all four) move without wrapping, Confirm activates, a tap
 * on a row or cell focuses and activates it, a vertical swipe on a list moves the focus one row
 * per UI_ROW_SWIPE_PX, Cancel or a tap outside the card cancels.
 * @return UI_EVENT_MOVED, UI_EVENT_ACTIVATED (see list->activated), UI_EVENT_CANCELLED or
 *         UI_EVENT_NONE
 */
UiEvent ui_list_popup_input(UiListPopup *list, const UiInput *in);

/** ui_list_popup_hints() - Fill @out with the hint row: Confirm with the confirm label, Cancel
 * with the cancel label. Returns how many. */
int ui_list_popup_hints(const UiListPopup *list, UiHintItem out[UI_HINT_MAX_ITEMS]);
