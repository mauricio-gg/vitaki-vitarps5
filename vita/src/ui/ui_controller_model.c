/**
 * @file ui_controller_model.c
 * @brief The Controller page's data (see ui_controller_model.h)
 */

#include "ui/ui_controller_model.h"

#include "context.h"
#include "logging.h"
#include "ui/ui_constants.h"
#include "ui/ui_settings_actions.h"

/** The outputs of the mapping popup, in row order (SPEC 3.8). */
static const VitakiCtrlOut OUTPUT_CHOICES[UI_CTRL_OUTPUT_COUNT] = {
    VITAKI_CTRL_OUT_OPTIONS, VITAKI_CTRL_OUT_SHARE, VITAKI_CTRL_OUT_TOUCHPAD, VITAKI_CTRL_OUT_L1,
    VITAKI_CTRL_OUT_L2,      VITAKI_CTRL_OUT_L3,    VITAKI_CTRL_OUT_R1,       VITAKI_CTRL_OUT_R2,
    VITAKI_CTRL_OUT_R3,      VITAKI_CTRL_OUT_PS,    VITAKI_CTRL_OUT_NONE,
};

static int s_preset = 0;
static VitakiCtrlMapInfo s_preview;

/** The config slot (0 to 2) that backs the preset with @map_id. */
static int slot_for_map_id(VitakiControllerMapId map_id) {
  switch (map_id) {
    case VITAKI_CONTROLLER_MAP_CUSTOM_2:
      return 1;
    case VITAKI_CONTROLLER_MAP_CUSTOM_3:
      return 2;
    case VITAKI_CONTROLLER_MAP_CUSTOM_1:
    default:
      return 0;
  }
}

/** The config slot of the current preset. */
static int current_slot(void) {
  return slot_for_map_id(g_controller_presets[s_preset].map_id);
}

/** The index of the preset whose map id is @map_id; the first when none is. */
static int preset_for_map_id(VitakiControllerMapId map_id) {
  for (int i = 0; i < CTRL_PRESET_COUNT; i++) {
    if (g_controller_presets[i].map_id == map_id)
      return i;
  }
  return 0;
}

/** True once the model has read the preset the config names. */
static bool s_ready = false;

/**
 * Make preset @index the current one in memory: name it in the config and load its slot into the
 * preview map, seeding a slot that was never saved from the defaults. Saves nothing.
 * @index is raised or lowered into the valid range.
 */
static void load_preset(int index) {
  if (index < 0)
    index = 0;
  if (index >= CTRL_PRESET_COUNT)
    index = CTRL_PRESET_COUNT - 1;
  s_preset = index;

  const VitakiControllerMapId map_id = g_controller_presets[index].map_id;
  context.config.controller_map_id = map_id;

  const int slot = slot_for_map_id(map_id);
  if (!context.config.custom_maps_valid[slot]) {
    controller_map_storage_set_defaults(&context.config.custom_maps[slot]);
    context.config.custom_maps_valid[slot] = true;
  }
  controller_map_storage_apply(&context.config.custom_maps[slot], &s_preview);
}

/** Read the preset the config names the first time the model is asked for anything. */
static void ensure_ready(void) {
  if (s_ready)
    return;
  s_ready = true;
  load_preset(preset_for_map_id(context.config.controller_map_id));
}

int ui_controller_model_preset(void) {
  ensure_ready();
  return s_preset;
}

void ui_controller_model_select_preset(int index) {
  ensure_ready();
  const VitakiControllerMapId before = context.config.controller_map_id;
  load_preset(index);
  if (context.config.controller_map_id != before)
    ui_settings_persist_config();
}

void ui_controller_model_step_preset(int delta) {
  ensure_ready();
  ui_controller_model_select_preset((s_preset + delta + CTRL_PRESET_COUNT) % CTRL_PRESET_COUNT);
}

VitakiCtrlOut ui_controller_model_output(VitakiCtrlIn input) {
  ensure_ready();
  if (input < 0 || input >= VITAKI_CTRL_IN_COUNT)
    return VITAKI_CTRL_OUT_NONE;
  return controller_map_get_output_for_input(&s_preview, input);
}

int ui_controller_model_common_output(const VitakiCtrlIn *inputs, int count) {
  if (!inputs || count <= 0 || count > VITAKI_CTRL_IN_COUNT)
    return UI_CTRL_MIXED;
  int outputs[VITAKI_CTRL_IN_COUNT];
  for (int i = 0; i < count; i++)
    outputs[i] = (int)ui_controller_model_output(inputs[i]);
  return ui_controller_common_output(outputs, count);
}

/** The stored map of the current preset's slot. */
static ControllerMapStorage *current_map(void) {
  return &context.config.custom_maps[current_slot()];
}

/** Rebuild the preview from the stored map, mark the slot saved and persist the config. */
static void commit_current_map(void) {
  const int slot = current_slot();
  context.config.custom_maps_valid[slot] = true;
  controller_map_storage_apply(&context.config.custom_maps[slot], &s_preview);
  ui_settings_persist_config();
}

void ui_controller_model_assign(const VitakiCtrlIn *inputs, int count, VitakiCtrlOut output) {
  ensure_ready();
  if (!inputs || count <= 0)
    return;
  ui_controller_assign_inputs(current_map(), inputs, count, output);
  commit_current_map();
}

int ui_controller_model_zone_output(UiCtrlSide side, int zone) {
  ensure_ready();
  return ui_controller_zone_output(current_map(), side, zone);
}

int ui_controller_model_zones_output(UiCtrlSide side, const int *zones, int count) {
  ensure_ready();
  return ui_controller_zones_output(current_map(), side, zones, count);
}

int ui_controller_model_side_output(UiCtrlSide side) {
  ensure_ready();
  return ui_controller_side_output(current_map(), side);
}

int ui_controller_model_mapped_zones(UiCtrlSide side) {
  ensure_ready();
  return ui_controller_mapped_zones(current_map(), side);
}

void ui_controller_model_assign_zones(UiCtrlSide side, const int *zones, int count,
                                      VitakiCtrlOut output) {
  ensure_ready();
  if (!zones || count <= 0)
    return;
  ui_controller_assign_zones(current_map(), side, zones, count, output);
  commit_current_map();
}

void ui_controller_model_assign_side(UiCtrlSide side, VitakiCtrlOut output) {
  ensure_ready();
  ui_controller_assign_side(current_map(), side, output);
  commit_current_map();
}

VitakiCtrlOut ui_controller_model_output_choice(int index) {
  if (index < 0 || index >= UI_CTRL_OUTPUT_COUNT)
    return VITAKI_CTRL_OUT_NONE;
  return OUTPUT_CHOICES[index];
}

int ui_controller_model_output_row(VitakiCtrlOut output) {
  for (int i = 0; i < UI_CTRL_OUTPUT_COUNT; i++) {
    if (OUTPUT_CHOICES[i] == output)
      return i;
  }
  return -1;
}
