/**
 * @file ui_controller_zones.h
 * @brief The Controller page's four zone grids (SPEC.md C20 and C21)
 *
 * One C21 ZoneGrid per view: the read-only rear grid of Summary page 2, the read-only front grid
 * of Summary page 1 (mapped blocks only, no cell labels, to stay inside the draw budget), and the
 * interactive front and rear grids of the zone views. Each grid is created the first time its view
 * is shown and then kept (creating or freeing one waits for the GPU). The cell labels and mapped
 * flags come from the model and are rewritten only for the cells whose output changed since the
 * last frame.
 */

#pragma once

#include "ui/ui_component.h"
#include "ui/ui_controller_rules.h"
#include "ui/ui_zone_select.h"

/** Which grid, and with it which diagram box it lies over. */
typedef enum ui_ctrl_zone_view_t {
  UI_CTRL_VIEW_SUMMARY_REAR = 0,  ///< Summary page 2, read-only
  UI_CTRL_VIEW_SUMMARY_FRONT,     ///< Summary page 1, read-only, blocks without labels
  UI_CTRL_VIEW_FRONT,             ///< the Front Touch zone view
  UI_CTRL_VIEW_REAR,              ///< the Rear Touch zone view
  UI_CTRL_VIEW_COUNT
} UiCtrlZoneView;

/** ui_controller_zones_side() - The touch surface @view shows. */
UiCtrlSide ui_controller_zones_side(UiCtrlZoneView view);

/** ui_controller_zones_diagram_rect() - The box the diagram of @view is drawn in. */
UiRect ui_controller_zones_diagram_rect(UiCtrlZoneView view);

/** ui_controller_zones_name() - "A1" to "F3" for zone @zone (0 to 17); "" when out of range. */
const char *ui_controller_zones_name(int zone);

/**
 * ui_controller_zones_sync() - Create the grid of @view on first use and bring its cells up to
 * date with the model. Call once a frame before drawing or reading the grid.
 */
void ui_controller_zones_sync(UiCtrlZoneView view);

/** ui_controller_zones_draw() - Draw the grid of @view. Nothing when it could not be created. */
void ui_controller_zones_draw(UiCtrlZoneView view);

/** ui_controller_zones_set_cursor_visible() - Show or hide the cursor cell of @view while focus is
 * on something else; the cursor keeps its position and picked and mapped cells draw as usual. */
void ui_controller_zones_set_cursor_visible(UiCtrlZoneView view, bool visible);

/**
 * ui_controller_zones_input() - Forward this frame's input to the grid of @view (see
 * ui_zone_grid_input()). UI_EVENT_ACTIVATED means "assign the selection"; the page must not act
 * on the same touch itself and must not call this while a popup is open.
 */
UiEvent ui_controller_zones_input(UiCtrlZoneView view, const UiInput *in);

/** ui_controller_zones_cursor() - The cell under the cursor of @view. */
int ui_controller_zones_cursor(UiCtrlZoneView view);

/** ui_controller_zones_selection() - The cells picked in @view (valid after ACTIVATED). */
const UiZoneSelection *ui_controller_zones_selection(UiCtrlZoneView view);

/** ui_controller_zones_clear_selection() - Drop the picked cells (after the popup closes). */
void ui_controller_zones_clear_selection(UiCtrlZoneView view);

/** ui_controller_zones_reset() - Put the cursor of @view on A1 with nothing picked (on entry). */
void ui_controller_zones_reset(UiCtrlZoneView view);
