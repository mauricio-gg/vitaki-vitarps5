/**
 * @file ui_controller_zones.c
 * @brief The Controller page's zone grids (see ui_controller_zones.h)
 */

#include "ui/ui_controller_zones.h"

#include "context.h"
#include "controller.h"
#include "ui/ui_controller_model.h"
#include "ui/ui_theme.h"
#include "ui/ui_zone_grid.h"

/** The label of an output that cannot be named by one output (see ui_controller_zone_output()). */
static const char LABEL_MIXED[] = "+";

/** A cached output that no real output equals, so every cell is written on the first sync. */
#define NOT_SHOWN (-2)

static const char *const ZONE_NAMES[UI_ZONE_COUNT] = {
    "A1", "B1", "C1", "D1", "E1", "F1", "A2", "B2", "C2",
    "D2", "E2", "F2", "A3", "B3", "C3", "D3", "E3", "F3",
};

/** One view: its grid and the output each cell shows. */
typedef struct view_t {
  UiZoneGrid grid;
  bool ready;   ///< the grid exists
  bool failed;  ///< creating it failed; the view draws and reacts to nothing
  int shown[UI_ZONE_COUNT];
} View;

static View s_views[UI_CTRL_VIEW_COUNT];

/** True for the two views that lie over the front touch screen. */
static bool is_front_view(UiCtrlZoneView view) {
  return view == UI_CTRL_VIEW_FRONT || view == UI_CTRL_VIEW_SUMMARY_FRONT;
}

/** True for the two Summary views: read-only, no input. */
static bool is_summary_view(UiCtrlZoneView view) {
  return view == UI_CTRL_VIEW_SUMMARY_REAR || view == UI_CTRL_VIEW_SUMMARY_FRONT;
}

UiCtrlSide ui_controller_zones_side(UiCtrlZoneView view) {
  return is_front_view(view) ? UI_CTRL_SIDE_FRONT : UI_CTRL_SIDE_REAR;
}

UiRect ui_controller_zones_diagram_rect(UiCtrlZoneView view) {
  switch (view) {
    case UI_CTRL_VIEW_SUMMARY_FRONT:
      return (UiRect){UI_CTRL_FRONT_X, UI_CTRL_FRONT_Y, UI_CTRL_FRONT_W, UI_CTRL_FRONT_H};
    case UI_CTRL_VIEW_FRONT:
      return (UiRect){UI_CTRL_ZONE_FRONT_X, UI_CTRL_ZONE_FRONT_Y, UI_CTRL_ZONE_FRONT_W,
                      UI_CTRL_ZONE_FRONT_H};
    case UI_CTRL_VIEW_REAR:
      return (UiRect){UI_CTRL_ZONE_REAR_X, UI_CTRL_ZONE_REAR_Y, UI_CTRL_ZONE_REAR_W,
                      UI_CTRL_ZONE_REAR_H};
    case UI_CTRL_VIEW_SUMMARY_REAR:
    default:
      return (UiRect){UI_CTRL_REAR_X, UI_CTRL_REAR_Y, UI_CTRL_REAR_W, UI_CTRL_REAR_H};
  }
}

const char *ui_controller_zones_name(int zone) {
  return zone >= 0 && zone < UI_ZONE_COUNT ? ZONE_NAMES[zone] : "";
}

/** The short cell label of @output: blank for None, the abbreviation the grid has room for. */
static const char *cell_label(int output) {
  switch (output) {
    case UI_CTRL_MIXED:
      return LABEL_MIXED;
    case VITAKI_CTRL_OUT_OPTIONS:
      return "OPT";
    case VITAKI_CTRL_OUT_SHARE:
      return "SHR";
    case VITAKI_CTRL_OUT_TOUCHPAD:
      return "TP";
    case VITAKI_CTRL_OUT_L1:
      return "L1";
    case VITAKI_CTRL_OUT_L2:
      return "L2";
    case VITAKI_CTRL_OUT_L3:
      return "L3";
    case VITAKI_CTRL_OUT_R1:
      return "R1";
    case VITAKI_CTRL_OUT_R2:
      return "R2";
    case VITAKI_CTRL_OUT_R3:
      return "R3";
    case VITAKI_CTRL_OUT_PS:
      return "PS";
    case VITAKI_CTRL_OUT_TRIANGLE:
      return "Tri";
    case VITAKI_CTRL_OUT_CIRCLE:
      return "Cir";
    case VITAKI_CTRL_OUT_CROSS:
      return "Crs";
    case VITAKI_CTRL_OUT_SQUARE:
      return "Sqr";
    case VITAKI_CTRL_OUT_UP:
      return "Up";
    case VITAKI_CTRL_OUT_DOWN:
      return "Dn";
    case VITAKI_CTRL_OUT_LEFT:
      return "Lt";
    case VITAKI_CTRL_OUT_RIGHT:
      return "Rt";
    default:
      return "";
  }
}

/** Create the grid of @view over its diagram the first time it is needed. */
static void ensure_grid(UiCtrlZoneView view, View *v) {
  if (v->ready || v->failed)
    return;
  const UiRect diagram = ui_controller_zones_diagram_rect(view);
  const UiZoneSide side = is_front_view(view) ? UI_ZONE_SIDE_FRONT : UI_ZONE_SIDE_REAR;
  const UiRect area = ui_zone_grid_rect_from_diagram(side, diagram.x, diagram.y, diagram.w);
  if (!ui_zone_grid_init(&v->grid, area, is_summary_view(view))) {
    LOGE("Controller zones: could not create the grid of view %d", (int)view);
    v->failed = true;
    return;
  }
  for (int i = 0; i < UI_ZONE_COUNT; i++)
    v->shown[i] = NOT_SHOWN;
  v->ready = true;
}

void ui_controller_zones_sync(UiCtrlZoneView view) {
  if (view < 0 || view >= UI_CTRL_VIEW_COUNT)
    return;
  View *v = &s_views[view];
  ensure_grid(view, v);
  if (!v->ready)
    return;
  const UiCtrlSide side = ui_controller_zones_side(view);
  /* The Summary front grid shows blocks only: 19 draws instead of 37 keeps page 1 under budget. */
  const bool labelled = view != UI_CTRL_VIEW_SUMMARY_FRONT;
  for (int i = 0; i < UI_ZONE_COUNT; i++) {
    const int output = ui_controller_model_zone_output(side, i);
    if (output == v->shown[i])
      continue;
    ui_zone_grid_set_cell(&v->grid, i, labelled ? cell_label(output) : "",
                          output != VITAKI_CTRL_OUT_NONE);
    v->shown[i] = output;
  }
}

void ui_controller_zones_draw(UiCtrlZoneView view) {
  if (view >= 0 && view < UI_CTRL_VIEW_COUNT && s_views[view].ready)
    ui_zone_grid_draw(&s_views[view].grid);
}

void ui_controller_zones_set_cursor_visible(UiCtrlZoneView view, bool visible) {
  if (view >= 0 && view < UI_CTRL_VIEW_COUNT)
    s_views[view].grid.cursor_hidden = !visible;
}

UiEvent ui_controller_zones_input(UiCtrlZoneView view, const UiInput *in) {
  if (view < 0 || view >= UI_CTRL_VIEW_COUNT || !s_views[view].ready)
    return UI_EVENT_NONE;
  return ui_zone_grid_input(&s_views[view].grid, in);
}

int ui_controller_zones_cursor(UiCtrlZoneView view) {
  if (view < 0 || view >= UI_CTRL_VIEW_COUNT)
    return 0;
  return s_views[view].grid.cursor;
}

const UiZoneSelection *ui_controller_zones_selection(UiCtrlZoneView view) {
  static const UiZoneSelection empty = {0};
  if (view < 0 || view >= UI_CTRL_VIEW_COUNT)
    return &empty;
  return &s_views[view].grid.selection;
}

void ui_controller_zones_clear_selection(UiCtrlZoneView view) {
  if (view >= 0 && view < UI_CTRL_VIEW_COUNT && s_views[view].ready)
    ui_zone_grid_clear_selection(&s_views[view].grid);
}

void ui_controller_zones_reset(UiCtrlZoneView view) {
  if (view < 0 || view >= UI_CTRL_VIEW_COUNT || !s_views[view].ready)
    return;
  ui_zone_grid_clear_selection(&s_views[view].grid);
  s_views[view].grid.cursor = 0;
}
