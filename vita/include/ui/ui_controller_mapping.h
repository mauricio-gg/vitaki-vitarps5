/**
 * @file ui_controller_mapping.h
 * @brief The Controller page's mapping popup (SPEC.md section 3.8)
 *
 * A C12 list of the 11 outputs on a C11 size L popup. One popup serves the shoulder buttons, a set
 * of touch zones and a whole touch surface; choosing a row assigns it to what the popup was opened
 * for and saves the config. The ticked row is the output every target holds now (none when they
 * differ). The model (ui_controller_model.c) does the assigning.
 */

#pragma once

#include <stdbool.h>

#include "controller.h"
#include "ui/ui_component.h"
#include "ui/ui_controller_rules.h"
#include "ui/ui_hint_row.h"

/** ui_controller_mapping_close() - Close the popup without assigning anything. Safe when closed. */
void ui_controller_mapping_close(void);

/** ui_controller_mapping_is_open() - True while the popup is open. */
bool ui_controller_mapping_is_open(void);

/** ui_controller_mapping_open_shoulder() - Open "Shoulder Mapping" for @input (L1 or R1);
 * @subtitle names it ("Left Shoulder (L1)"). */
void ui_controller_mapping_open_shoulder(VitakiCtrlIn input, const char *subtitle);

/**
 * ui_controller_mapping_open_zones() - Open "Front Touch Mapping" / "Rear Touch Mapping" for the
 * zones @zones[0..count-1] of @side. The subtitle is the zone ("Front C2"), or "N Zones Selected"
 * and the value they share ("Mixed" when they differ) for several.
 */
void ui_controller_mapping_open_zones(UiCtrlSide side, const int *zones, int count);

/** ui_controller_mapping_open_side() - Open the popup for the whole surface @side; the subtitle is
 * "Full Front Touch" / "Full Rear Touch" and its value ("Mixed" when the zones differ). */
void ui_controller_mapping_open_side(UiCtrlSide side);

/**
 * ui_controller_mapping_update() - Drive the open popup with this frame's input: a chosen row is
 * assigned and saved, Cancel or a tap outside closes it unchanged.
 * @return true when the popup closed this frame
 */
bool ui_controller_mapping_update(const UiInput *in);

/** ui_controller_mapping_draw() - Draw the popup on top of the page. Nothing when closed. */
void ui_controller_mapping_draw(void);

/** ui_controller_mapping_hints() - Fill @out with the popup's hints; returns how many. */
int ui_controller_mapping_hints(UiHintItem out[UI_HINT_MAX_ITEMS]);
