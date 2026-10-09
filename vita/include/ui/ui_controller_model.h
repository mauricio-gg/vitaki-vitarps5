/**
 * @file ui_controller_model.h
 * @brief The Controller page's data: which preset is current and what each input maps to
 *        (SPEC.md section 3.8)
 *
 * Everything the old Controller screen did with the mapping that is not drawing or input handling:
 * choosing a preset, reading the output of an input, assigning an output to a set of inputs and
 * clearing a side of the touch surfaces. The three presets are the three custom slots of the
 * config (context.config.custom_maps); a slot that was never saved starts from the defaults of
 * controller_map_storage_set_defaults(). Every change is saved with ui_settings_persist_config().
 *
 * The model keeps a preview map (VitakiCtrlMapInfo) of the current preset, which is what the
 * diagram and the mapping popup read.
 */

#pragma once

#include "controller.h"
#include "ui/ui_controller_rules.h"

/** How many outputs the mapping popup offers: Options, Share, Touchpad, L1, L2, L3, R1, R2, R3,
 * PS and None. */
#define UI_CTRL_OUTPUT_COUNT 11

/** Zones on one touch surface (6 columns x 3 rows). */
#define UI_CTRL_ZONE_COUNT VITAKI_CTRL_IN_FRONTTOUCH_GRID_COUNT

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

/** ui_controller_model_map() - The preview map of the current preset, for the diagram. */
const VitakiCtrlMapInfo *ui_controller_model_map(void);

/** ui_controller_model_output() - What @input is mapped to in the current preset. */
VitakiCtrlOut ui_controller_model_output(VitakiCtrlIn input);

/**
 * ui_controller_model_common_output() - The output every one of @inputs is mapped to.
 * @return that output (as an int, a VitakiCtrlOut value), or UI_CTRL_MIXED when they differ
 */
int ui_controller_model_common_output(const VitakiCtrlIn *inputs, int count);

/**
 * ui_controller_model_assign() - Map every one of @inputs to @output in the current preset, keep
 * the L2/R2 assignments consistent and save the config. Inputs out of range are skipped.
 */
void ui_controller_model_assign(const VitakiCtrlIn *inputs, int count, VitakiCtrlOut output);

/** ui_controller_model_clear_front() - Map the 18 front zones and the whole front surface to
 * None. */
void ui_controller_model_clear_front(void);

/** ui_controller_model_clear_rear() - Map the 18 rear zones and the whole rear surface to None. */
void ui_controller_model_clear_rear(void);

/** ui_controller_model_output_choice() - The output of row @index of the mapping popup
 * (0 to UI_CTRL_OUTPUT_COUNT - 1); None for an index out of range. */
VitakiCtrlOut ui_controller_model_output_choice(int index);

/** ui_controller_model_output_row() - The row of the mapping popup that holds @output, or -1. */
int ui_controller_model_output_row(VitakiCtrlOut output);
