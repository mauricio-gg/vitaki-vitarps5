/**
 * @file ui_zone_select.c
 * @brief C21 ZoneGrid selection logic (see ui_zone_select.h)
 */

#include "ui/ui_zone_select.h"

#include <string.h>

/* Source art geometry of the two grids (SPEC.md C21), in pixels of the unscaled diagram image. */
#define FRONT_SRC_W 874
#define FRONT_GRID_X 178
#define FRONT_GRID_Y 30
#define FRONT_GRID_W 526
#define FRONT_GRID_H 298
#define REAR_SRC_W 720
#define REAR_GRID_X 137
#define REAR_GRID_Y 45
#define REAR_GRID_W 444
#define REAR_GRID_H 188

/** scale_round() - @value * @num / @den rounded to the nearest integer (non-negative inputs). */
static int scale_round(int value, int num, int den) {
  return (value * num + den / 2) / den;
}

UiRect ui_zone_grid_rect_from_diagram(UiZoneSide side, int diagram_x, int diagram_y,
                                      int diagram_w) {
  const bool front = side == UI_ZONE_SIDE_FRONT;
  const int src_w = front ? FRONT_SRC_W : REAR_SRC_W;
  const int gx = front ? FRONT_GRID_X : REAR_GRID_X;
  const int gy = front ? FRONT_GRID_Y : REAR_GRID_Y;
  const int gw = front ? FRONT_GRID_W : REAR_GRID_W;
  const int gh = front ? FRONT_GRID_H : REAR_GRID_H;
  return (UiRect){diagram_x + scale_round(gx, diagram_w, src_w),
                  diagram_y + scale_round(gy, diagram_w, src_w), scale_round(gw, diagram_w, src_w),
                  scale_round(gh, diagram_w, src_w)};
}

int ui_zone_cell_from_point(UiRect grid, float x, float y) {
  if (grid.w <= 0 || grid.h <= 0 || !ui_rect_contains(grid, x, y))
    return -1;
  int col = (int)((x - (float)grid.x) * (float)UI_ZONE_COLS / (float)grid.w);
  int row = (int)((y - (float)grid.y) * (float)UI_ZONE_ROWS / (float)grid.h);
  if (col >= UI_ZONE_COLS)
    col = UI_ZONE_COLS - 1;
  if (row >= UI_ZONE_ROWS)
    row = UI_ZONE_ROWS - 1;
  return row * UI_ZONE_COLS + col;
}

int ui_zone_cursor_move(int cursor, uint32_t repeat) {
  if (cursor < 0)
    cursor = 0;
  if (cursor >= UI_ZONE_COUNT)
    cursor = UI_ZONE_COUNT - 1;
  int row = cursor / UI_ZONE_COLS;
  int col = cursor % UI_ZONE_COLS;
  if ((repeat & UI_BTN_UP) && row > 0)
    row--;
  if ((repeat & UI_BTN_DOWN) && row < UI_ZONE_ROWS - 1)
    row++;
  if ((repeat & UI_BTN_LEFT) && col > 0)
    col--;
  if ((repeat & UI_BTN_RIGHT) && col < UI_ZONE_COLS - 1)
    col++;
  return row * UI_ZONE_COLS + col;
}

void ui_zone_sel_clear(UiZoneSelection *sel) {
  memset(sel, 0, sizeof(*sel));
}

bool ui_zone_sel_add(UiZoneSelection *sel, int cell) {
  if (cell < 0 || cell >= UI_ZONE_COUNT || sel->picked[cell])
    return false;
  sel->cells[sel->count++] = cell;
  sel->picked[cell] = true;
  return true;
}

bool ui_zone_sel_paint_visit(UiZoneSelection *sel, int cell) {
  if (cell < 0 || cell >= UI_ZONE_COUNT)
    return false;
  if (sel->count > 0 && sel->cells[sel->count - 1] == cell)
    return false;
  if (sel->count > 1 && sel->cells[sel->count - 2] == cell) {
    sel->picked[sel->cells[sel->count - 1]] = false;
    sel->count--;
    return true;
  }
  return ui_zone_sel_add(sel, cell);
}
