/**
 * @file ui_group_list.h
 * @brief C07 GroupList: the text-only list of groups on the left of a page (SPEC.md C07)
 *
 * Rows are UI_GROUP_ROW_H high and UI_PAGE_GROUP_W wide, T20 labels. The current group is white
 * and has a UI_GROUP_BAR_W bar at the row's left edge. While the list has focus the current group
 * is drawn in T28 with a glow. Paper cost: one draw per label, one for the bar, one for the glow.
 *
 * Key input is whatever the screen passes in: the screen gives the list the D-pad and Confirm
 * only while the list has focus, and L and R always.
 */

#pragma once

#include <stdbool.h>

#include "ui/ui_component.h"
#include "ui/ui_theme.h"

typedef struct ui_group_list_t {
  const char *const *names;  ///< borrowed, static strings
  int count;                 ///< at most UI_GROUP_MAX
  int current;               ///< the group whose rows the pane shows
  bool focused;              ///< the list has focus (set by the screen)
  UiRect visible[UI_GROUP_MAX];
  UiRect hit[UI_GROUP_MAX];
  int focus_label_w[UI_GROUP_MAX];  ///< width of each label in the focused (T28) face
} UiGroupList;

/** ui_group_list_init() - Set up @count groups (capped at UI_GROUP_MAX) with group 0 current. */
void ui_group_list_init(UiGroupList *list, const char *const *names, int count);

/** ui_group_list_set_current() - Make group @index current (clamped to the list). */
void ui_group_list_set_current(UiGroupList *list, int index);

/** ui_group_list_draw() - Draw the labels, the current-group bar and the focus glow. No state
 * change. */
void ui_group_list_draw(const UiGroupList *list);

/**
 * ui_group_list_input() - Move between groups, open the pane, back out.
 * @return UI_EVENT_MOVED when the current group changed (Up/Down, L/R wrapping, or a tap on any
 *         group, which reports MOVED even when it is the current one), UI_EVENT_ACTIVATED on
 *         Confirm or Right (go to the pane), UI_EVENT_CANCELLED on Cancel, else UI_EVENT_NONE.
 *         Up and Down do not wrap; L and R do.
 */
UiEvent ui_group_list_input(UiGroupList *list, const UiInput *in);
