/**
 * @file ui_controller_model.h
 * @brief The Controller page's data: which preset is current and what each input maps to
 *        (SPEC.md section 3.8)
 *
 * Everything the old Controller screen did with the mapping that is not drawing or input handling:
 * choosing a preset, reading the output of an input, assigning an output to a set of inputs and
 * reading and assigning touch zones or a whole touch surface (clearing is assigning None). The
 * three presets are the three custom slots of the config (context.config.custom_maps); a slot that
 * was never saved starts from the defaults of controller_map_storage_set_defaults(). Every change
 * is saved with ui_settings_persist_config().
 *
 * The model keeps a preview map (VitakiCtrlMapInfo) of the current preset, which is what the
 * callouts read; touch zones are read from the stored map through the rules.
 */

#pragma once

#include "controller.h"
#include "ui/ui_controller_rules.h"

/** How many outputs the mapping popup offers: Options, Share, Touchpad, L1, L2, L3, R1, R2, R3,
 * PS and None. */
#define UI_CTRL_OUTPUT_COUNT 11

/** ui_controller_model_preset() - Index of the current preset, 0 to CTRL_PRESET_COUNT - 1. */
int ui_controller_model_preset(void);

/**
 * ui_controller_model_select_preset() - Make preset @index the current one.
 * @index: Raised or lowered into the valid range.
 *
 * Loads the preset's slot into the preview map, seeding a slot that was never saved from the
 * defaults. When this changes the preset the config names, the config is saved.
 */
void ui_controller_model_select_preset(int index);

/**
 * ui_controller_model_step_preset() - Select the preset @delta places from the current one,
 * wrapping over the presets (-1 is the previous, +1 the next).
 */
void ui_controller_model_step_preset(int delta);

/** ui_controller_model_output() - What @input is mapped to in the current preset. */
VitakiCtrlOut ui_controller_model_output(VitakiCtrlIn input);

/**
 * ui_controller_model_common_output() - The output every one of @inputs is mapped to.
 * @return that output (as an int, a VitakiCtrlOut value), or UI_CTRL_MIXED when they differ
 */
int ui_controller_model_common_output(const VitakiCtrlIn *inputs, int count);

/**
 * ui_controller_model_assign() - Map every one of @inputs to @output in the current preset, keep
 * the L2/R2 assignments consistent and save the config. Inputs out of range are skipped. For
 * touch zones use the zone functions below, which keep the stream equal to what the page shows.
 */
void ui_controller_model_assign(const VitakiCtrlIn *inputs, int count, VitakiCtrlOut output);

/** ui_controller_model_zone_output() - What touching zone @zone of @side does in the current
 * preset (an output, or UI_CTRL_MIXED). See ui_controller_rules.h. */
int ui_controller_model_zone_output(UiCtrlSide side, int zone);

/** ui_controller_model_zones_output() - The common output of @zones of @side, or UI_CTRL_MIXED. */
int ui_controller_model_zones_output(UiCtrlSide side, const int *zones, int count);

/** ui_controller_model_side_output() - The output touching anywhere on @side does, or
 * UI_CTRL_MIXED when the zones differ. */
int ui_controller_model_side_output(UiCtrlSide side);

/** ui_controller_model_mapped_zones() - How many zones of @side do something when touched. */
int ui_controller_model_mapped_zones(UiCtrlSide side);

/** ui_controller_model_assign_zones() - Map the zones @zones[0..count-1] of @side to @output and
 * save the config. */
void ui_controller_model_assign_zones(UiCtrlSide side, const int *zones, int count,
                                      VitakiCtrlOut output);

/** ui_controller_model_assign_side() - Map the whole touch surface @side to @output (None clears
 * it) and save the config. */
void ui_controller_model_assign_side(UiCtrlSide side, VitakiCtrlOut output);

/** ui_controller_model_output_choice() - The output of row @index of the mapping popup
 * (0 to UI_CTRL_OUTPUT_COUNT - 1); None for an index out of range. */
VitakiCtrlOut ui_controller_model_output_choice(int index);

/** ui_controller_model_output_row() - The row of the mapping popup that holds @output, or -1. */
int ui_controller_model_output_row(VitakiCtrlOut output);
