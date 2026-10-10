/**
 * @file ui_zone_grid.h
 * @brief C21 ZoneGrid: the 6 x 3 touch-zone grid over the controller diagram (SPEC.md C21)
 *
 * The caller computes the grid rect (ui_zone_grid_rect_from_diagram()), sets each cell's label and
 * mapped flag, forwards input and reads the selection on UI_EVENT_ACTIVATED.
 *
 * Draw cost: one baked grid-lines texture, one baked cell texture per mapped, cursor or picked
 * cell (the state texture replaces the mapped one, it never stacks) and one label draw per cell
 * with text. A full grid (18 mapped, labelled, a cursor, 4 picked) is 1 + 18 + 18 = 37 draws.
 *
 * Input contract (editable grid):
 *  - UI_EVENT_MOVED: the cursor or the selection changed.
 *  - UI_EVENT_ACTIVATED: a gesture ended; "assign these cells" = selection.cells[0..count-1]
 *    (never empty). The selection stays drawn as picked until the next gesture, a D-pad move or
 *    ui_zone_grid_clear_selection() (call it when the assign popup closes). A touch gesture reports
 *    only here, on release; the caller must not also act on the same tap (no double open).
 * Read-only grid: no cursor, no selection; a tap on it reports UI_EVENT_ACTIVATED.
 *
 * The caller must stop forwarding input while a popup is open.
 */

#pragma once

#include <stdbool.h>

#include "ui/ui_component.h"
#include "ui/ui_zone_select.h"

/** Longest cell label, including the terminator ("OPT", "SHR", "TP", "L1"... fit easily). */
#define UI_ZONE_LABEL_MAX 8

/** Cursor and picked cells have a 2 px white border. */
#define UI_ZONE_BORDER_PICK 2
/** Mapped cells have a 1 px border; the idle grid lines are 1 px too. */
#define UI_ZONE_BORDER_LINE 1
/** Reach of the inner glow of a picked cell, from the cell edge inwards. */
#define UI_ZONE_GLOW_PX 14

struct vita2d_texture;

typedef struct ui_zone_grid_t {
  UiRect visible;  ///< the grid as drawn: the rect given at init, trimmed to whole cells
  UiRect hit;      ///< the touch rect: equal to @visible (a grid is far above the 48 px minimum)
  int cell_w;
  int cell_h;
  bool read_only;
  int cursor;                 ///< cell under the D-pad cursor / last touched, 0..UI_ZONE_COUNT-1
  UiZoneSelection selection;  ///< picked cells, readable on UI_EVENT_ACTIVATED
  char labels[UI_ZONE_COUNT][UI_ZONE_LABEL_MAX];
  int label_x[UI_ZONE_COUNT];  ///< label offset from the cell's left edge, set with the label
  bool mapped[UI_ZONE_COUNT];
  bool cursor_hidden;  ///< draw no cursor cell (focus is elsewhere); the cursor keeps its position
  bool hold_active;    ///< Confirm is held and moving the cursor adds cells
  bool paint_active;   ///< a finger gesture that began on the grid is in progress
  struct vita2d_texture *tex_lines;
  struct vita2d_texture *tex_mapped;
  struct vita2d_texture *tex_cursor;
  struct vita2d_texture *tex_picked;
  const struct UiTheme
      *mapped_theme;  ///< the theme tex_mapped was baked with; draw re-bakes on change
} UiZoneGrid;

/**
 * ui_zone_grid_init() - Set up an empty grid over @area and bake its textures.
 * @area:      Screen rect of the grid; width and height are trimmed down to whole cells.
 * @read_only: True for the Summary page 2 view: no cursor, a tap reports ACTIVATED.
 *
 * Bakes the four textures here (never per frame). Call ui_zone_grid_destroy() before calling
 * this again on the same struct.
 *
 * @return true on success; false (logged) when @area is smaller than the grid or a texture could
 *         not be allocated, in which case the grid is left destroyed.
 */
bool ui_zone_grid_init(UiZoneGrid *grid, UiRect area, bool read_only);

/** ui_zone_grid_destroy() - Free the baked textures. Safe on a grid that failed to init. */
void ui_zone_grid_destroy(UiZoneGrid *grid);

/**
 * ui_zone_grid_set_cell() - Set one cell's label and mapped flag; lays the label out now.
 * @cell:   0..UI_ZONE_COUNT-1, row * UI_ZONE_COLS + col (A1 is 0, F3 is 17).
 * @label:  T16 text, truncated to UI_ZONE_LABEL_MAX - 1; NULL or "" for a blank (None) cell.
 * @mapped: True when the cell has an output other than None.
 */
void ui_zone_grid_set_cell(UiZoneGrid *grid, int cell, const char *label, bool mapped);

/** ui_zone_grid_clear_selection() - Drop the picked cells (after the assign popup closes). */
void ui_zone_grid_clear_selection(UiZoneGrid *grid);

/**
 * ui_zone_grid_draw() - Draw the grid, cell states and labels. Re-bakes the mapped-cell texture
 * first when the colour theme changed since it was baked (once per change, not per frame).
 */
void ui_zone_grid_draw(UiZoneGrid *grid);

/** ui_zone_grid_input() - Move the cursor, hold-select, finger-paint. See the file comment. */
UiEvent ui_zone_grid_input(UiZoneGrid *grid, const UiInput *in);
