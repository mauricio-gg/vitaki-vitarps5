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

const VitakiCtrlMapInfo *ui_controller_model_map(void) {
  ensure_ready();
  return &s_preview;
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

/**
 * Point in_l2 and in_r2 of @map at the first input mapped to L2 and to R2 (None when no input is),
 * as the preview map and the stream read them.
 */
static void sync_trigger_assignments(ControllerMapStorage *map) {
  map->in_l2 = VITAKI_CTRL_IN_NONE;
  map->in_r2 = VITAKI_CTRL_IN_NONE;
  for (int i = 0; i < VITAKI_CTRL_IN_COUNT; i++) {
    const VitakiCtrlOut output = (VitakiCtrlOut)map->in_out_btn[i];
    if (output == VITAKI_CTRL_OUT_L2 && map->in_l2 == VITAKI_CTRL_IN_NONE) {
      map->in_l2 = i;
    } else if (output == VITAKI_CTRL_OUT_R2 && map->in_r2 == VITAKI_CTRL_IN_NONE) {
      map->in_r2 = i;
    }
  }
}

void ui_controller_model_assign(const VitakiCtrlIn *inputs, int count, VitakiCtrlOut output) {
  ensure_ready();
  if (!inputs || count <= 0)
    return;

  const int slot = current_slot();
  ControllerMapStorage *map = &context.config.custom_maps[slot];
  for (int i = 0; i < count; i++) {
    if (inputs[i] < 0 || inputs[i] >= VITAKI_CTRL_IN_COUNT) {
      LOGE("Controller model: input %d out of range, skipped", (int)inputs[i]);
      continue;
    }
    map->in_out_btn[inputs[i]] = output;
  }

  sync_trigger_assignments(map);
  context.config.custom_maps_valid[slot] = true;
  controller_map_storage_apply(map, &s_preview);
  ui_settings_persist_config();
}

/** Map the @count zones from @first, and the whole-surface input @any, to None. */
static void clear_surface(VitakiCtrlIn first, int count, VitakiCtrlIn any) {
  VitakiCtrlIn inputs[UI_CTRL_ZONE_COUNT + 1];
  for (int i = 0; i < count; i++)
    inputs[i] = (VitakiCtrlIn)(first + i);
  inputs[count] = any;
  ui_controller_model_assign(inputs, count + 1, VITAKI_CTRL_OUT_NONE);
}

void ui_controller_model_clear_front(void) {
  clear_surface(VITAKI_CTRL_IN_FRONTTOUCH_GRID_START, VITAKI_CTRL_IN_FRONTTOUCH_GRID_COUNT,
                VITAKI_CTRL_IN_FRONTTOUCH_ANY);
}

void ui_controller_model_clear_rear(void) {
  clear_surface(VITAKI_CTRL_IN_REARTOUCH_GRID_START, VITAKI_CTRL_IN_REARTOUCH_GRID_COUNT,
                VITAKI_CTRL_IN_REARTOUCH_ANY);
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
