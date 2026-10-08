/**
 * @file ui_options_column.h
 * @brief C05 OptionsColumn: the Triangle menu of a console (SPEC.md C05)
 *
 * Interactive component (SPEC 2.0): a struct plus init, draw and input. A screen owns one,
 * opens it, refreshes its rows every frame while it is open (ui_options_column_set_rows), and
 * forwards input to it. It slides in from the right over UI_OPTS_SLIDE_MS; the layers behind it
 * dim to UI_OPTS_BEHIND_PCT through ui_options_column_behind_alpha() and
 * ui_layer_set_alpha(), which the screen applies around them.
 *
 * The column only reports what the user chose. What a row does, and whether the column closes
 * after it, is the screen's decision (Re-pair keeps it open behind its confirm popup).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui/ui_component.h"
#include "ui/ui_theme.h"

/** One row the screen offers. @label is borrowed and must outlive the frame. */
typedef struct ui_options_row_t {
  int id;  ///< the screen's own number for the action; returned in column->activated
  const char *label;
  bool disabled;  ///< drawn at UI_OPTS_DISABLED_PCT; cannot be activated
} UiOptionsRow;

typedef struct ui_options_column_t {
  bool open;
  char name[UI_OPTS_NAME_MAX];  ///< the console's name, shortened to fit the title
  char name_src[UI_OPTS_NAME_MAX];
  UiOptionsRow rows[UI_OPTS_MAX_ROWS];
  int count;
  int focus;
  int activated;  ///< the id of the row of the last UI_EVENT_ACTIVATED

  UiRect row_visible[UI_OPTS_MAX_ROWS];  ///< the focus bar of each row
  UiRect row_hit[UI_OPTS_MAX_ROWS];      ///< 352 x 56
  UiRect column_hit;                     ///< the column above the hint row; other taps close it

  /* Slide: progress runs from slide_from to 1 (open) or 0 (closed) over UI_OPTS_SLIDE_MS. */
  uint64_t slide_start_us;
  float slide_from;
} UiOptionsColumn;

/** ui_options_column_init() - Closed and empty; computes the column's rects. */
void ui_options_column_init(UiOptionsColumn *col);

/**
 * ui_options_column_open() - Open the column on its first row and start the slide in.
 * Call ui_options_column_set_rows() before the next draw.
 */
void ui_options_column_open(UiOptionsColumn *col);

/** ui_options_column_close() - Close the column (it slides out). Safe when closed. */
void ui_options_column_close(UiOptionsColumn *col);

/** ui_options_column_close_now() - Close without sliding out (the screen is being left). */
void ui_options_column_close_now(UiOptionsColumn *col);

/**
 * ui_options_column_set_rows() - Set the console name and the rows; the focus is kept, clamped.
 * @name:  Title; shortened with an ellipsis to fit. The shortening is redone only when it changes.
 * @rows:  Up to UI_OPTS_MAX_ROWS rows; more are ignored.
 */
void ui_options_column_set_rows(UiOptionsColumn *col, const char *name, const UiOptionsRow *rows,
                                int count);

/** ui_options_column_is_open() - True from open() until closed, even while it slides out. */
static inline bool ui_options_column_is_open(const UiOptionsColumn *col) {
  return col->open;
}

/**
 * ui_options_column_behind_alpha() - Opacity for the layers behind the column: 1 when it is
 * closed, UI_OPTS_BEHIND_PCT when it is fully in, in between while it slides.
 */
float ui_options_column_behind_alpha(const UiOptionsColumn *col);

/**
 * ui_options_column_draw() - Draw the feathered panel, title, rows and focus bar.
 * Paper cost: edge gradient 1, name 1, "Options" 1, a label per row, a divider under every row
 * but the focused one, and for the focused row 4 (glow 1, bar 3).
 */
void ui_options_column_draw(const UiOptionsColumn *col);

/**
 * ui_options_column_input() - Up/Down move (no wrap), Confirm activates, a tap on a row
 * activates it, Triangle or Cancel closes, and so does any tap outside the column (the hint row
 * included).
 * @return UI_EVENT_MOVED, UI_EVENT_ACTIVATED (the id is in col->activated), UI_EVENT_CANCELLED
 *         (the column has been closed), or UI_EVENT_NONE
 */
UiEvent ui_options_column_input(UiOptionsColumn *col, const UiInput *in);
