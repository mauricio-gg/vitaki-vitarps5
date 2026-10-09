/**
 * @file ui_controller_page.h
 * @brief The XMB Controller page (SPEC.md C20 and section 3.8)
 *
 * A C07 page frame with the preset switcher in the title row, the front diagram, the L1 and R1
 * callouts, the preset description and page label as footers, and the hint row. Confirm or a tap on
 * a callout opens the Shoulder Mapping popup (a C12 list on a C11 popup). The data (current preset,
 * what each input maps to) is ui_controller_model.c's.
 *
 * Summary page 1 "Buttons" is built. Page 2 and the zone views come with ticket #305's next tasks.
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
 *         back to Home, which then shows the Controller category on the preset the page was on)
 */
UIScreenType ui_controller_page_frame(void);
