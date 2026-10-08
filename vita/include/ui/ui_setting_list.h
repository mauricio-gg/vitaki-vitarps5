/**
 * @file ui_setting_list.h
 * @brief C08 SettingRow list with C09 Toggle and C10 ChoiceValue: the pane of a page (SPEC.md C08)
 *
 * UI_PAGE_PANE_ROWS rows of UI_ROW_H at UI_PAGE_PANE_X. Each row has a T20 label on the left and a
 * control on the right: a toggle (pill track, round knob, "On"/"Off"), a choice (chevron, value,
 * chevron), an info value (right-aligned text that only takes focus) or an action (a chevron
 * pointing right; it runs when activated). The focused row has a FILL_FOCUS bar, TEXT colours and,
 * while the pane has focus, a glow on the label; the other rows have a LINE_FAINT divider. The
 * pane scrolls to keep the focused row visible.
 *
 * The screen owns the items array and rewrites the values from the config each frame, then calls
 * ui_setting_list_sync(); the list never touches the config. When input asks for a change it
 * reports UI_EVENT_ACTIVATED with the step in the list's @step field and the row in @focus.
 *
 * Motion: a toggle's knob slides over UI_TOGGLE_MS whenever its value changes, whoever changed it;
 * a row that acted shows FILL_ON for UI_ROW_PRESS_MS. Art that is not text (chevrons, knob, toggle
 * track) is baked once by ui_setting_list_init().
 *
 * Paper cost, per row: label 1; unfocused rows add a divider 1, the focused row a bar 3 and, while
 * the pane has focus, a glow 1. A toggle adds track 1 (2 when on), knob 1, text 1; a choice adds
 * 2 chevrons and the value text; an info row adds its value text; an enabled action adds 1
 * chevron.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui/ui_component.h"
#include "ui/ui_theme.h"

typedef enum ui_setting_kind_t {
  UI_SETTING_TOGGLE = 0,
  UI_SETTING_CHOICE,
  UI_SETTING_INFO,    ///< label and a value; takes focus and does nothing else
  UI_SETTING_ACTION,  ///< label and a chevron; Confirm or a tap reports UI_EVENT_ACTIVATED
} UiSettingKind;

/** One row as it is shown right now. Strings are borrowed from the screen. */
typedef struct ui_setting_item_t {
  const char *label;
  UiSettingKind kind;
  bool on;                 ///< toggle: the current value
  const char *value_text;  ///< choice and info: the current value's words
  bool small_value;        ///< info: draw the value in T16 instead of T20
  bool disabled;           ///< action: drawn at UI_ROW_DISABLED_PCT, no chevron, does not run
} UiSettingItem;

typedef struct ui_setting_list_t {
  const UiSettingItem *items;  ///< borrowed; owned by the screen
  int count;                   ///< at most UI_SETTING_MAX_ROWS
  int focus;                   ///< focused row, always valid when count > 0
  int scroll;                  ///< index of the first row on screen
  bool active;                 ///< the pane has focus (set by the screen)
  int step;  ///< after UI_EVENT_ACTIVATED: +1 or -1, the way the row asked to change

  UiRect pane;                           ///< the whole pane, for swipes
  UiRect row[UI_PAGE_PANE_ROWS];         ///< visible and hit rect of each screen slot
  UiRect arrow_left[UI_PAGE_PANE_ROWS];  ///< chevron hit boxes of a choice in that slot
  UiRect arrow_right[UI_PAGE_PANE_ROWS];

  /* Per-row caches, rewritten by ui_setting_list_sync() */
  int label_w[UI_SETTING_MAX_ROWS];
  char value_raw[UI_SETTING_MAX_ROWS][UI_SETTING_VALUE_MAX];  ///< value_text as last seen
  char value_fit[UI_SETTING_MAX_ROWS][UI_SETTING_VALUE_MAX];  ///< shortened to fit its place
  int value_w[UI_SETTING_MAX_ROWS];
  bool shown_on[UI_SETTING_MAX_ROWS];
  uint64_t toggle_start_us[UI_SETTING_MAX_ROWS];  ///< 0 when the knob is at rest

  uint64_t press_start_us;  ///< when a row last acted; 0 for never
  bool swipe_active;        ///< a touch that began on the pane is still down
  int swipe_base;           ///< the focus when that touch began
} UiSettingList;

/** ui_setting_list_init() - Compute the rects and bake the chevrons, knob and toggle track
 * (once; safe to call twice). Call after vita2d is initialised. */
void ui_setting_list_init(UiSettingList *list);

/**
 * ui_setting_list_load() - Show @count rows (capped at UI_SETTING_MAX_ROWS) with the focus on the
 * first. Toggle knobs are placed at once, without sliding.
 */
void ui_setting_list_load(UiSettingList *list, const UiSettingItem *items, int count);

/**
 * ui_setting_list_sync() - Take in values the screen rewrote in the items: start the knob slide of
 * every toggle whose value changed and re-measure every choice or info value that changed.
 */
void ui_setting_list_sync(UiSettingList *list);

/** ui_setting_list_draw() - Draw the rows at their place in the motion. No state change. */
void ui_setting_list_draw(const UiSettingList *list);

/**
 * ui_setting_list_input() - Up/Down move the focus (no wrap), Confirm and Right step the value
 * forward, Left steps a choice back, taps and swipes act on the pane.
 * @return UI_EVENT_MOVED when the focus moved (D-pad or swipe, or a tap on an info row or a
 * disabled action, which only focus), UI_EVENT_ACTIVATED when the focused row should change or run
 *         (the row is @focus, the way is @step; a tap on another row focuses it first),
 *         UI_EVENT_CANCELLED on Cancel or on Left over a toggle, info or action (the screen goes
 *         back to the groups), otherwise UI_EVENT_NONE. A chevron tap steps that way; a row tap
 *         steps forward. Confirm on an info row does nothing; Right acts only on a toggle or a
 *         choice.
 */
UiEvent ui_setting_list_input(UiSettingList *list, const UiInput *in);
