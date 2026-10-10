// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the pure ZoneGrid selection rules: which cell is under a finger
// and how a painted path grows and backtracks. `./tools/build.sh test` only cross-compiles for
// arm-vita-eabi and never executes, so run these natively:
//
//   cc -std=c99 -Wall -Wextra -I vita/include \
//      test/ui_zone_select_tests.c vita/src/ui/ui_zone_select.c -o /tmp/ui_zone_select_tests && \
//      /tmp/ui_zone_select_tests

#include <assert.h>
#include <stdio.h>

#include "ui/ui_zone_select.h"

/* A 600 x 300 grid at (100, 50): 100 x 100 px cells. */
static const UiRect GRID = {100, 50, 600, 300};

/* The selected cells, in order, must equal the expected list. */
static void expect_cells(const UiZoneSelection *sel, const int *want, int n) {
  assert(sel->count == n);
  for (int i = 0; i < n; i++) {
    assert(sel->cells[i] == want[i]);
    assert(sel->picked[want[i]]);
  }
}

/* catches: a finger painted back over the previous cell leaves the cell it just left selected, so
 * the user cannot undo an over-reach without lifting. */
static void test_backtrack_one_cell_removes_last(void) {
  UiZoneSelection sel;
  ui_zone_sel_clear(&sel);
  ui_zone_sel_paint_visit(&sel, 0);
  ui_zone_sel_paint_visit(&sel, 1);
  ui_zone_sel_paint_visit(&sel, 2);
  assert(ui_zone_sel_paint_visit(&sel, 1));
  const int want[] = {0, 1};
  expect_cells(&sel, want, 2);
  assert(!sel.picked[2]);
}

/* catches: moving back two cells being treated as a backtrack and wiping cells the user meant to
 * keep (only the immediately previous cell is a backtrack; two back is an already-picked cell). */
static void test_back_two_cells_is_not_a_backtrack(void) {
  UiZoneSelection sel;
  ui_zone_sel_clear(&sel);
  ui_zone_sel_paint_visit(&sel, 0);
  ui_zone_sel_paint_visit(&sel, 1);
  ui_zone_sel_paint_visit(&sel, 2);
  assert(!ui_zone_sel_paint_visit(&sel, 0));
  const int want[] = {0, 1, 2};
  expect_cells(&sel, want, 3);
}

/* catches: a cell appearing twice in the selection (so a zone is assigned twice and the picked
 * count shown in the popup is wrong) when the finger loops back over earlier cells. */
static void test_revisit_does_not_duplicate(void) {
  UiZoneSelection sel;
  ui_zone_sel_clear(&sel);
  const int path[] = {0, 1, 7, 6, 0, 6, 0};
  for (int i = 0; i < 7; i++)
    ui_zone_sel_paint_visit(&sel, path[i]);
  /* 0,1,7,6 picked; the revisit of 0 is not previous-but-one (that is 7), so ignored; 6 is
   * the last cell; 0 again is three back, ignored. */
  const int want[] = {0, 1, 7, 6};
  expect_cells(&sel, want, 4);
}

/* catches: writing past the 18-entry selection array when the whole grid is painted and then
 * revisited; also that a full sweep really selects all 18. */
static void test_painting_everything_stops_at_18(void) {
  UiZoneSelection sel;
  ui_zone_sel_clear(&sel);
  for (int i = 0; i < UI_ZONE_COUNT; i++)
    assert(ui_zone_sel_paint_visit(&sel, i));
  assert(sel.count == UI_ZONE_COUNT);
  for (int i = 0; i < UI_ZONE_COUNT - 2; i++)
    assert(!ui_zone_sel_paint_visit(&sel, i));
  assert(sel.count == UI_ZONE_COUNT);
  assert(!ui_zone_sel_add(&sel, 3));
}

/* catches: an off-grid point (-1) from a finger leaving the grid adding or removing cells. */
static void test_off_grid_visit_is_ignored(void) {
  UiZoneSelection sel;
  ui_zone_sel_clear(&sel);
  ui_zone_sel_paint_visit(&sel, 4);
  ui_zone_sel_paint_visit(&sel, 5);
  assert(!ui_zone_sel_paint_visit(&sel, -1));
  assert(!ui_zone_sel_paint_visit(&sel, UI_ZONE_COUNT));
  const int want[] = {4, 5};
  expect_cells(&sel, want, 2);
}

/* catches: a touch a pixel outside the grid selecting an edge cell (or a touch on the last pixel
 * being dropped), and the right/bottom edge reading a column or row past the end. */
static void test_cell_from_point_edges(void) {
  assert(ui_zone_cell_from_point(GRID, 100.0f, 50.0f) == 0);                 /* top-left pixel */
  assert(ui_zone_cell_from_point(GRID, 699.0f, 349.0f) == UI_ZONE_COUNT - 1); /* last pixel */
  assert(ui_zone_cell_from_point(GRID, 99.0f, 50.0f) == -1);                 /* just left */
  assert(ui_zone_cell_from_point(GRID, 100.0f, 49.0f) == -1);                /* just above */
  assert(ui_zone_cell_from_point(GRID, 700.0f, 100.0f) == -1);               /* just right */
  assert(ui_zone_cell_from_point(GRID, 150.0f, 350.0f) == -1);               /* just below */
  assert(ui_zone_cell_from_point(GRID, 199.0f, 149.0f) == 0);                /* cell A1 corner */
  assert(ui_zone_cell_from_point(GRID, 200.0f, 150.0f) == UI_ZONE_COLS + 1); /* B2 */
}

/* catches: the D-pad wrapping from the right edge to the next row (the spec says no wrap), which
 * would drop the cursor on a cell the user never aimed at. */
static void test_cursor_does_not_wrap(void) {
  assert(ui_zone_cursor_move(UI_ZONE_COLS - 1, UI_BTN_RIGHT) == UI_ZONE_COLS - 1);
  assert(ui_zone_cursor_move(UI_ZONE_COLS, UI_BTN_LEFT) == UI_ZONE_COLS);
  assert(ui_zone_cursor_move(2, UI_BTN_UP) == 2);
  assert(ui_zone_cursor_move(UI_ZONE_COUNT - 1, UI_BTN_DOWN) == UI_ZONE_COUNT - 1);
  assert(ui_zone_cursor_move(7, UI_BTN_DOWN) == 13);
}

/* catches: the grid rect drifting from the SPEC source-art rectangles when the diagram is scaled
 * (the grid would no longer sit over the Vita screen / rear pad). */
static void test_grid_rect_scales_with_diagram(void) {
  UiRect front = ui_zone_grid_rect_from_diagram(UI_ZONE_SIDE_FRONT, 0, 0, 874);
  assert(front.x == 178 && front.y == 30 && front.w == 526 && front.h == 298);
  UiRect rear = ui_zone_grid_rect_from_diagram(UI_ZONE_SIDE_REAR, 100, 200, 360);
  assert(rear.x == 100 + 69 && rear.y == 200 + 23 && rear.w == 222 && rear.h == 94);
}

int main(void) {
  test_backtrack_one_cell_removes_last();
  test_back_two_cells_is_not_a_backtrack();
  test_revisit_does_not_duplicate();
  test_painting_everything_stops_at_18();
  test_off_grid_visit_is_ignored();
  test_cell_from_point_edges();
  test_cursor_does_not_wrap();
  test_grid_rect_scales_with_diagram();
  puts("ui_zone_select_tests: all passed");
  return 0;
}
