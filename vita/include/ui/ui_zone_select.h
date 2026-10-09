/**
 * @file ui_zone_select.h
 * @brief C21 ZoneGrid selection logic with no SDK dependency (SPEC.md C21)
 *
 * The geometry of the 6 x 3 grid and the rules that decide which cells are selected: cell from a
 * point, the D-pad cursor, hold-to-select and the finger-paint path with its one-cell backtrack.
 * Kept free of vita2d so it can be checked natively (test/ui_zone_select_tests.c).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui/ui_component.h"

#define UI_ZONE_COLS 6
#define UI_ZONE_ROWS 3
#define UI_ZONE_COUNT (UI_ZONE_COLS * UI_ZONE_ROWS)

/** Which diagram the grid lies over. */
typedef enum ui_zone_side_t {
  UI_ZONE_SIDE_FRONT = 0, /**< the Vita screen, controller_front.png */
  UI_ZONE_SIDE_REAR,      /**< the rear touch pad, controller_back_clean.png */
} UiZoneSide;

/** The cells picked so far, in the order they were added. */
typedef struct ui_zone_selection_t {
  int cells[UI_ZONE_COUNT];    ///< cell indexes (row * UI_ZONE_COLS + col), oldest first
  int count;                   ///< valid entries in @cells
  bool picked[UI_ZONE_COUNT];  ///< picked[i] is true when cell i is in @cells
} UiZoneSelection;

/**
 * ui_zone_grid_rect_from_diagram() - Screen rect of the grid over a drawn diagram.
 * @side:      Front screen or rear pad.
 * @diagram_x: Left edge of the drawn diagram image, screen pixels.
 * @diagram_y: Top edge of the drawn diagram image.
 * @diagram_w: Drawn width of the diagram image (its height follows the art's aspect ratio).
 *
 * The grid rect is a fixed rectangle in the source art (front 526 x 298 at +178/+30 of 874 px,
 * rear 444 x 188 at +137/+45 of 720 px), scaled by diagram_w / source width and rounded.
 */
UiRect ui_zone_grid_rect_from_diagram(UiZoneSide side, int diagram_x, int diagram_y, int diagram_w);

/**
 * ui_zone_cell_from_point() - Which cell lies under a point.
 * @grid: The grid rect (right and bottom edges excluded).
 * @x, @y: Point in screen pixels.
 * @return the cell index, or -1 when the point is outside the grid or the grid is empty.
 */
int ui_zone_cell_from_point(UiRect grid, float x, float y);

/**
 * ui_zone_cursor_move() - Step the cursor by the D-pad directions in @repeat, without wrapping.
 * @cursor: Current cell (clamped into the grid first).
 * @repeat: UiButton bits that fire this frame; only the four D-pad bits are read.
 * @return the new cursor cell.
 */
int ui_zone_cursor_move(int cursor, uint32_t repeat);

/** ui_zone_sel_clear() - Empty the selection. */
void ui_zone_sel_clear(UiZoneSelection *sel);

/**
 * ui_zone_sel_add() - Add a cell unless it is invalid or already picked (hold-to-select).
 * @return true when the selection changed.
 */
bool ui_zone_sel_add(UiZoneSelection *sel, int cell);

/**
 * ui_zone_sel_paint_visit() - The finger moves onto @cell during a paint gesture.
 *
 * - The cell the finger is already on: nothing.
 * - The cell before the last one (backtracking exactly one cell): the last cell is removed.
 * - Any other cell not yet picked: it is added.
 * - A cell picked earlier (not the previous one): nothing, it is never duplicated.
 * An invalid cell (-1, a point off the grid) is ignored.
 *
 * @return true when the selection changed.
 */
bool ui_zone_sel_paint_visit(UiZoneSelection *sel, int cell);
