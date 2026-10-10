/**
 * @file ui_controller_page.h
 * @brief The XMB Controller page (SPEC.md C20 and section 3.8)
 *
 * A C07 page frame with a back chevron and, on the Summary pages, the preset switcher in the title
 * row. Summary page 1 "Buttons" shows the front diagram with the L1 and R1 callouts; page 2 "Back
 * Touch" the rear diagram with a read-only zone grid. The Front Touch and Rear Touch zone views
 * (reached with Triangle, Confirm on page 2, or a tap on the diagram) show an interactive zone
 * grid. The footer carries the preset description or the zone, the page label and the small Clear
 * and Whole surface buttons; the hint row sits below. A mapping popup (ui_controller_mapping.h)
 * assigns outputs. The data (current preset, what each input maps to) is ui_controller_model.c's.
 */

#pragma once

#include "ui/ui_types.h"

/** ui_controller_page_init() - Bake the chevrons and the leader dot. Call once from init_ui(),
 * after ui_shapes_init(). */
void ui_controller_page_init(void);

/**
 * ui_controller_page_open() - Make @preset (0 to 2) the current preset and start the page on
 * Summary page 1 with L1 focused. Home calls this before it switches to UI_SCREEN_TYPE_CONTROLLER.
 */
void ui_controller_page_open(int preset);

/**
 * ui_controller_page_frame() - Run one frame of the page: handle input, draw.
 * @return the screen to show next (UI_SCREEN_TYPE_CONTROLLER to stay, UI_SCREEN_TYPE_MAIN to go
 *         back to Home, which then shows the Controller category on the preset the page was on;
 *         Cancel in a zone view returns to its Summary page, not to Home)
 */
UIScreenType ui_controller_page_frame(void);
