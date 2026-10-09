/**
 * @file ui_controller_rules.h
 * @brief Pure rules of the Controller page (SPEC.md section 3.8)
 *
 * No Vita, context or UI dependency (only the input and output ids of controller.h), so the rules
 * can be checked natively (test/ui_controller_rules_tests.c).
 *
 * What a touch does in the stream (vita/src/host_input.c), which these rules mirror so that the
 * page shows the truth:
 *  - A front touch fires the output of the whole-surface input (VITAKI_CTRL_IN_FRONTTOUCH_ANY) AND
 *    the output of the grid cell under the finger. A rear touch does the same with
 *    VITAKI_CTRL_IN_REARTOUCH_ANY and the rear cell. The two add up; they do not override.
 *  - A grid cell holding L2 or R2 presses that trigger. The whole-surface input does not: it ORs
 *    the output's raw code into the button bits, so an L2 or R2 there presses the wrong buttons.
 *  - The legacy quadrant, half, arc and centre inputs (REARTOUCH_UL, FRONTTOUCH_LEFT...) are not
 *    read for rear touches at all and are outside what the page can show or edit.
 *  - in_l2 / in_r2 name an input that holds L2 / R2; loading a map writes that output into the
 *    input (controller_map_storage_apply), so an input listed there but stored as None is L2/R2.
 */

#pragma once

#include "controller.h"

/** A common-output result when the outputs of a set of inputs differ. */
#define UI_CTRL_MIXED (-1)

/** Zones on one touch surface (6 columns x 3 rows). */
#define UI_CTRL_ZONES VITAKI_CTRL_IN_FRONTTOUCH_GRID_COUNT

/** The two touch surfaces. */
typedef enum ui_ctrl_side_t {
  UI_CTRL_SIDE_FRONT = 0,
  UI_CTRL_SIDE_REAR,
} UiCtrlSide;

/**
 * ui_controller_common_output() - The one output every input of a set is mapped to.
 * @outputs: The output of each input in the set.
 * @count:   How many; the set must not be empty.
 *
 * The mapping popup ticks this output; when the inputs differ it reads "Mixed" and ticks nothing.
 * None (0) is an output like any other: a set that is all None has the common output None.
 *
 * @return the common output, or UI_CTRL_MIXED when the outputs differ or the set is empty
 */
int ui_controller_common_output(const int *outputs, int count);

/**
 * ui_controller_zone_output() - What touching zone @zone of @side does, as one output.
 * @map:  The preset's stored map.
 * @side: Front or rear surface.
 * @zone: 0 to UI_CTRL_ZONES - 1, row * 6 + column (A1 is 0, F3 is 17).
 *
 * The zone's own output, or the surface's whole-surface output when the zone has none. A touch
 * that fires two different outputs (the cell and the whole-surface input disagree), or fires the
 * whole-surface input with a trigger on it (see the file comment), cannot be named by one output.
 *
 * @return an output (a VitakiCtrlOut value, 0 for None), or UI_CTRL_MIXED when it cannot be named
 *         or @zone is out of range
 */
int ui_controller_zone_output(const ControllerMapStorage *map, UiCtrlSide side, int zone);

/**
 * ui_controller_side_output() - What touching anywhere on @side does, when it is the same
 * everywhere: the common output of the 18 zones, or UI_CTRL_MIXED when they differ.
 */
int ui_controller_side_output(const ControllerMapStorage *map, UiCtrlSide side);

/**
 * ui_controller_zones_output() - The common output of the zones @zones[0..count-1] of @side, or
 * UI_CTRL_MIXED when they differ or the set is empty or out of range.
 */
int ui_controller_zones_output(const ControllerMapStorage *map, UiCtrlSide side, const int *zones,
                               int count);

/**
 * ui_controller_mapped_zones() - How many zones of @side do something when touched. A zone that
 * is Mixed does something and counts.
 */
int ui_controller_mapped_zones(const ControllerMapStorage *map, UiCtrlSide side);

/**
 * ui_controller_assign_inputs() - Map every one of @inputs to @output and keep in_l2 / in_r2 in
 * step: outputs held through them are written into the inputs first, then both point at the first
 * input holding L2 / R2 (None when no input does). Inputs out of range are skipped.
 */
void ui_controller_assign_inputs(ControllerMapStorage *map, const VitakiCtrlIn *inputs, int count,
                                 VitakiCtrlOut output);

/**
 * ui_controller_assign_zones() - Map the zones @zones[0..count-1] of @side to @output.
 *
 * First the whole-surface input is folded into the zones and cleared, so afterwards each zone's
 * own output is all a touch does and the page shows exactly what the stream does: a zone that
 * had no output of its own takes the whole-surface output, a zone that had one keeps it. Zones out
 * of range are skipped.
 */
void ui_controller_assign_zones(ControllerMapStorage *map, UiCtrlSide side, const int *zones,
                                int count, VitakiCtrlOut output);

/** ui_controller_assign_side() - Map all 18 zones of @side to @output and clear its
 * whole-surface input, so touching anywhere does @output and nothing else. */
void ui_controller_assign_side(ControllerMapStorage *map, UiCtrlSide side, VitakiCtrlOut output);
