/**
 * @file ui_controller_rules.c
 * @brief Pure rules of the Controller page (see ui_controller_rules.h)
 */

#include "ui/ui_controller_rules.h"

#include <stdbool.h>
#include <stddef.h>

/** The first grid input and the whole-surface input of @side. */
static VitakiCtrlIn grid_start(UiCtrlSide side) {
  return side == UI_CTRL_SIDE_FRONT ? VITAKI_CTRL_IN_FRONTTOUCH_GRID_START
                                    : VITAKI_CTRL_IN_REARTOUCH_GRID_START;
}

static VitakiCtrlIn any_input(UiCtrlSide side) {
  return side == UI_CTRL_SIDE_FRONT ? VITAKI_CTRL_IN_FRONTTOUCH_ANY : VITAKI_CTRL_IN_REARTOUCH_ANY;
}

/** What @input does: its stored output, or the trigger it is listed as in in_l2 / in_r2. */
static int input_output(const ControllerMapStorage *map, VitakiCtrlIn input) {
  const int stored = map->in_out_btn[input];
  if (stored != VITAKI_CTRL_OUT_NONE)
    return stored;
  if (map->in_l2 == (int)input)
    return VITAKI_CTRL_OUT_L2;
  if (map->in_r2 == (int)input)
    return VITAKI_CTRL_OUT_R2;
  return VITAKI_CTRL_OUT_NONE;
}

static bool is_trigger(int output) {
  return output == VITAKI_CTRL_OUT_L2 || output == VITAKI_CTRL_OUT_R2;
}

int ui_controller_common_output(const int *outputs, int count) {
  if (!outputs || count <= 0)
    return UI_CTRL_MIXED;
  for (int i = 1; i < count; i++) {
    if (outputs[i] != outputs[0])
      return UI_CTRL_MIXED;
  }
  return outputs[0];
}

int ui_controller_zone_output(const ControllerMapStorage *map, UiCtrlSide side, int zone) {
  if (!map || zone < 0 || zone >= UI_CTRL_ZONES)
    return UI_CTRL_MIXED;
  const int whole = input_output(map, any_input(side));
  const int cell = input_output(map, (VitakiCtrlIn)(grid_start(side) + zone));
  if (whole == VITAKI_CTRL_OUT_NONE)
    return cell;
  if (is_trigger(whole))
    return UI_CTRL_MIXED;
  if (cell == VITAKI_CTRL_OUT_NONE || cell == whole)
    return whole;
  return UI_CTRL_MIXED;
}

int ui_controller_zones_output(const ControllerMapStorage *map, UiCtrlSide side, const int *zones,
                               int count) {
  if (!zones || count <= 0 || count > UI_CTRL_ZONES)
    return UI_CTRL_MIXED;
  int outputs[UI_CTRL_ZONES];
  for (int i = 0; i < count; i++)
    outputs[i] = ui_controller_zone_output(map, side, zones[i]);
  return ui_controller_common_output(outputs, count);
}

int ui_controller_side_output(const ControllerMapStorage *map, UiCtrlSide side) {
  int zones[UI_CTRL_ZONES];
  for (int i = 0; i < UI_CTRL_ZONES; i++)
    zones[i] = i;
  return ui_controller_zones_output(map, side, zones, UI_CTRL_ZONES);
}

int ui_controller_mapped_zones(const ControllerMapStorage *map, UiCtrlSide side) {
  int mapped = 0;
  for (int i = 0; i < UI_CTRL_ZONES; i++) {
    if (ui_controller_zone_output(map, side, i) != VITAKI_CTRL_OUT_NONE)
      mapped++;
  }
  return mapped;
}

/** Write the outputs held through in_l2 / in_r2 into their inputs, as loading a map does. */
static void fold_triggers(ControllerMapStorage *map) {
  if (map->in_l2 != VITAKI_CTRL_IN_NONE)
    map->in_out_btn[map->in_l2] = VITAKI_CTRL_OUT_L2;
  if (map->in_r2 != VITAKI_CTRL_IN_NONE)
    map->in_out_btn[map->in_r2] = VITAKI_CTRL_OUT_R2;
}

/** Point in_l2 / in_r2 at the first input holding L2 / R2, None when no input does. */
static void sync_triggers(ControllerMapStorage *map) {
  map->in_l2 = VITAKI_CTRL_IN_NONE;
  map->in_r2 = VITAKI_CTRL_IN_NONE;
  for (int i = 0; i < VITAKI_CTRL_IN_COUNT; i++) {
    const int output = map->in_out_btn[i];
    if (output == VITAKI_CTRL_OUT_L2 && map->in_l2 == VITAKI_CTRL_IN_NONE) {
      map->in_l2 = i;
    } else if (output == VITAKI_CTRL_OUT_R2 && map->in_r2 == VITAKI_CTRL_IN_NONE) {
      map->in_r2 = i;
    }
  }
}

void ui_controller_assign_inputs(ControllerMapStorage *map, const VitakiCtrlIn *inputs, int count,
                                 VitakiCtrlOut output) {
  if (!map || !inputs || count <= 0)
    return;
  fold_triggers(map);
  for (int i = 0; i < count; i++) {
    if (inputs[i] > VITAKI_CTRL_IN_NONE && inputs[i] < VITAKI_CTRL_IN_COUNT)
      map->in_out_btn[inputs[i]] = output;
  }
  sync_triggers(map);
}

/** Fold the whole-surface output of @side into the zones that have none, then clear it. */
static void fold_whole_surface(ControllerMapStorage *map, UiCtrlSide side) {
  const VitakiCtrlIn whole = any_input(side);
  const int output = map->in_out_btn[whole];
  if (output == VITAKI_CTRL_OUT_NONE)
    return;
  for (int i = 0; i < UI_CTRL_ZONES; i++) {
    const VitakiCtrlIn cell = (VitakiCtrlIn)(grid_start(side) + i);
    if (map->in_out_btn[cell] == VITAKI_CTRL_OUT_NONE)
      map->in_out_btn[cell] = output;
  }
  map->in_out_btn[whole] = VITAKI_CTRL_OUT_NONE;
  sync_triggers(map);
}

void ui_controller_assign_zones(ControllerMapStorage *map, UiCtrlSide side, const int *zones,
                                int count, VitakiCtrlOut output) {
  if (!map || !zones || count <= 0)
    return;
  fold_triggers(map);
  fold_whole_surface(map, side);
  VitakiCtrlIn inputs[UI_CTRL_ZONES];
  int kept = 0;
  for (int i = 0; i < count && kept < UI_CTRL_ZONES; i++) {
    if (zones[i] >= 0 && zones[i] < UI_CTRL_ZONES)
      inputs[kept++] = (VitakiCtrlIn)(grid_start(side) + zones[i]);
  }
  ui_controller_assign_inputs(map, inputs, kept, output);
}

void ui_controller_assign_side(ControllerMapStorage *map, UiCtrlSide side, VitakiCtrlOut output) {
  if (!map)
    return;
  VitakiCtrlIn inputs[UI_CTRL_ZONES + 1];
  for (int i = 0; i < UI_CTRL_ZONES; i++)
    inputs[i] = (VitakiCtrlIn)(grid_start(side) + i);
  inputs[UI_CTRL_ZONES] = any_input(side);
  ui_controller_assign_inputs(map, inputs, UI_CTRL_ZONES + 1, VITAKI_CTRL_OUT_NONE);
  ui_controller_assign_inputs(map, inputs, UI_CTRL_ZONES, output);
}
