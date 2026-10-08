/**
 * @file ui_hint_row.h
 * @brief C06 HintRow: the button hints along the bottom of a menu (SPEC.md C06)
 *
 * Display-only draw helper with a hit test. The screen declares its hints as logical
 * actions each frame; the row resolves them to glyphs, so screens never test the
 * Circle Button Confirm setting. Frame order for a screen:
 *   1. ui_hint_row_tap() on last frame's layout, then handle input as usual;
 *   2. ui_hint_row_layout() with this frame's hints, then ui_hint_row_draw().
 *
 * When Network Unstable is active on a menu, the right UI_HINT_ALERT_W pixels hold the
 * unstable pill and hints never run under it.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui/ui_component.h"
#include "ui/ui_theme.h"

/**
 * One hint. @action is a UiButton bit, UI_BTN_L | UI_BTN_R for the combined "L R" hint, or
 * UI_BTN_LEFT | UI_BTN_RIGHT for the D-pad left-right hint. CONFIRM and CANCEL resolve to Cross or
 * Circle from the setting. Only the glyphs a screen uses are baked: Confirm, Cancel, Options,
 * Clear, L, R, D-pad left-right; an action without a glyph draws its label alone.
 */
typedef struct ui_hint_item_t {
  uint32_t action;
  const char *label;  ///< borrowed; must outlive the frame it is drawn in
  bool dim;           ///< drawn at UI_HINT_DIM_PCT; the tap still reports the action
  bool low_priority;  ///< may be dropped, rightmost first, when the row is too narrow
} UiHintItem;

/** Result of laying out one frame's hints: what is drawn and where it can be tapped. */
typedef struct ui_hint_layout_t {
  int count;                            ///< hints kept after the collapse rule
  UiHintItem items[UI_HINT_MAX_ITEMS];  ///< the kept hints, in draw order
  int x[UI_HINT_MAX_ITEMS];             ///< left edge of each kept hint
  UiRect hit[UI_HINT_MAX_ITEMS];        ///< tap rect of each kept hint: its width x UI_HINT_H
  bool alert;                           ///< the unstable pill occupies the right slot
} UiHintLayout;

/** ui_hint_row_init() - Load the L and R badges. Call once at start-up, after load_textures(). */
void ui_hint_row_init(void);

/**
 * ui_hint_row_layout() - Place @count hints centred in the free width (SPEC C06).
 * Items beyond UI_HINT_MAX_ITEMS are ignored. Low-priority hints are dropped from the
 * right until the rest fit; Confirm, Cancel and the main action are not flagged low
 * priority and are never dropped.
 */
void ui_hint_row_layout(UiHintLayout *layout, const UiHintItem *items, int count);

/**
 * ui_hint_row_draw() - Draw the hints of @layout and, when active, the unstable pill.
 * Paper cost: 2 draws per hint with one glyph (3 for L R), plus 5 for the pill.
 */
void ui_hint_row_draw(const UiHintLayout *layout);

/**
 * ui_hint_row_tap() - The action of the hint tapped this frame, or 0.
 * A tap on the combined L R hint reports UI_BTN_L from its left half and UI_BTN_R from
 * its right half.
 */
uint32_t ui_hint_row_tap(const UiHintLayout *layout, const UiInput *in);
