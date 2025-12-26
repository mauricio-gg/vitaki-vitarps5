#include "controller.h"
#include "ui/ui_constants.h"
#include "context.h"
#include <stdio.h>

/**
 * Controller preset definitions for the immersive controller layout
 * These map user-friendly names and descriptions to VitakiControllerMapId values
 */
const ControllerPresetDef g_controller_presets[CTRL_PRESET_COUNT] = {
    { "Custom 1", "Your first custom mapping", VITAKI_CONTROLLER_MAP_CUSTOM_1 },
    { "Custom 2", "Your second custom mapping", VITAKI_CONTROLLER_MAP_CUSTOM_2 },
    { "Custom 3", "Your third custom mapping", VITAKI_CONTROLLER_MAP_CUSTOM_3 }
};

void controller_map_storage_from_vcmi(ControllerMapStorage* storage, const VitakiCtrlMapInfo* vcmi) {
  if (!storage || !vcmi)
    return;
  memcpy(storage->in_out_btn, vcmi->in_out_btn, sizeof(storage->in_out_btn));
  storage->in_l2 = vcmi->in_l2;
  storage->in_r2 = vcmi->in_r2;
}

void controller_map_storage_apply(const ControllerMapStorage* storage, VitakiCtrlMapInfo* vcmi) {
  if (!storage || !vcmi)
    return;

  memset(vcmi->in_state, 0, sizeof(vcmi->in_state));
  memcpy(vcmi->in_out_btn, storage->in_out_btn, sizeof(storage->in_out_btn));
  vcmi->in_l2 = storage->in_l2;
  vcmi->in_r2 = storage->in_r2;
  vcmi->did_init = true;

  if (vcmi->in_l2 != VITAKI_CTRL_IN_NONE)
    vcmi->in_out_btn[vcmi->in_l2] = VITAKI_CTRL_OUT_L2;
  if (vcmi->in_r2 != VITAKI_CTRL_IN_NONE)
    vcmi->in_out_btn[vcmi->in_r2] = VITAKI_CTRL_OUT_R2;
}

void controller_map_storage_set_defaults(ControllerMapStorage* storage) {
  if (!storage)
    return;
  VitakiCtrlMapInfo temp = {0};
  init_controller_map(&temp, VITAKI_CONTROLLER_MAP_0);
  controller_map_storage_from_vcmi(storage, &temp);
}

VitakiCtrlOut controller_map_get_output_for_input(const VitakiCtrlMapInfo* vcmi, VitakiCtrlIn input) {
  if (!vcmi)
    return VITAKI_CTRL_OUT_NONE;

  VitakiCtrlOut output = vcmi->in_out_btn[input];
  if (output == VITAKI_CTRL_OUT_NONE) {
    if (vcmi->in_l2 == input)
      return VITAKI_CTRL_OUT_L2;
    if (vcmi->in_r2 == input)
      return VITAKI_CTRL_OUT_R2;
  }
  return output;
}

static void apply_default_custom_map(VitakiCtrlMapInfo* vcmi) {
  vcmi->in_out_btn[VITAKI_CTRL_IN_LEFT_SQUARE]         = VITAKI_CTRL_OUT_L3;
  vcmi->in_out_btn[VITAKI_CTRL_IN_RIGHT_CIRCLE]        = VITAKI_CTRL_OUT_R3;
  vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_ANY]      = VITAKI_CTRL_OUT_TOUCHPAD;
  vcmi->in_l2 = VITAKI_CTRL_IN_REARTOUCH_LEFT_L1;
  vcmi->in_r2 = VITAKI_CTRL_IN_REARTOUCH_RIGHT_R1;
  vcmi->in_out_btn[vcmi->in_l2] = VITAKI_CTRL_OUT_L2;
  vcmi->in_out_btn[vcmi->in_r2] = VITAKI_CTRL_OUT_R2;
  vcmi->did_init = true;
}

const char* controller_output_symbol(VitakiCtrlOut button) {
  switch (button) {
    case VITAKI_CTRL_OUT_TRIANGLE: return "△";
    case VITAKI_CTRL_OUT_CIRCLE: return "○";
    case VITAKI_CTRL_OUT_CROSS: return "✕";
    case VITAKI_CTRL_OUT_SQUARE: return "□";
    default:
      return controller_output_name(button);
  }
}

const char* controller_output_name(VitakiCtrlOut button) {
  switch (button) {
    case VITAKI_CTRL_OUT_TRIANGLE: return "Triangle";
    case VITAKI_CTRL_OUT_CIRCLE: return "Circle";
    case VITAKI_CTRL_OUT_CROSS: return "Cross";
    case VITAKI_CTRL_OUT_SQUARE: return "Square";
    case VITAKI_CTRL_OUT_L1: return "L1";
    case VITAKI_CTRL_OUT_R1: return "R1";
    case VITAKI_CTRL_OUT_L2: return "L2";
    case VITAKI_CTRL_OUT_R2: return "R2";
    case VITAKI_CTRL_OUT_L3: return "L3";
    case VITAKI_CTRL_OUT_R3: return "R3";
    case VITAKI_CTRL_OUT_PS: return "PS";
    case VITAKI_CTRL_OUT_SHARE: return "Share";
    case VITAKI_CTRL_OUT_OPTIONS: return "Options";
    case VITAKI_CTRL_OUT_TOUCHPAD: return "Touchpad";
    case VITAKI_CTRL_OUT_UP: return "D-Pad Up";
    case VITAKI_CTRL_OUT_DOWN: return "D-Pad Down";
    case VITAKI_CTRL_OUT_LEFT: return "D-Pad Left";
    case VITAKI_CTRL_OUT_RIGHT: return "D-Pad Right";
    case VITAKI_CTRL_OUT_NONE:
    default:
      return "None";
  }
}

void init_controller_map(VitakiCtrlMapInfo* vcmi, VitakiControllerMapId controller_map_id) {
  // TODO make fully configurable instead of using controller_map_id
  // For now, 0 = default map, 1 = ywnico map

  // Clear map
  memset(vcmi->in_out_btn, 0, sizeof(vcmi->in_out_btn));
  vcmi->in_l2 = VITAKI_CTRL_IN_NONE;
  vcmi->in_r2 = VITAKI_CTRL_IN_NONE;


  // L1, R1, Select+Start are common in all maps currently
  vcmi->in_out_btn[VITAKI_CTRL_IN_L1]                  = VITAKI_CTRL_OUT_L1;
  vcmi->in_out_btn[VITAKI_CTRL_IN_R1]                  = VITAKI_CTRL_OUT_R1;
  vcmi->in_out_btn[VITAKI_CTRL_IN_SELECT_START]        = VITAKI_CTRL_OUT_PS;

  // Handle custom preset slots
  if (controller_map_id == VITAKI_CONTROLLER_MAP_CUSTOM_1 ||
      controller_map_id == VITAKI_CONTROLLER_MAP_CUSTOM_2 ||
      controller_map_id == VITAKI_CONTROLLER_MAP_CUSTOM_3) {
    int slot = 0;
    if (controller_map_id == VITAKI_CONTROLLER_MAP_CUSTOM_2) slot = 1;
    else if (controller_map_id == VITAKI_CONTROLLER_MAP_CUSTOM_3) slot = 2;

    if (context.config.custom_maps_valid[slot]) {
      controller_map_storage_apply(&context.config.custom_maps[slot], vcmi);
      return;
    }
    apply_default_custom_map(vcmi);
    return;
  } else if (controller_map_id == VITAKI_CONTROLLER_MAP_1) {
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC]   = VITAKI_CTRL_OUT_L3;
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC]   = VITAKI_CTRL_OUT_R3;
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_CENTER]   = VITAKI_CTRL_OUT_TOUCHPAD;

    vcmi->in_l2 = VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC;
    vcmi->in_r2 = VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC;

  } else if (controller_map_id == VITAKI_CONTROLLER_MAP_2) {
    vcmi->in_out_btn[VITAKI_CTRL_IN_REARTOUCH_LEFT]      = VITAKI_CTRL_OUT_L3;
    vcmi->in_out_btn[VITAKI_CTRL_IN_REARTOUCH_RIGHT]     = VITAKI_CTRL_OUT_R3;
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_CENTER]   = VITAKI_CTRL_OUT_TOUCHPAD;

    vcmi->in_l2 = VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC;
    vcmi->in_r2 = VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC;

  } else if (controller_map_id == VITAKI_CONTROLLER_MAP_3) {
    vcmi->in_out_btn[VITAKI_CTRL_IN_REARTOUCH_LEFT]      = VITAKI_CTRL_OUT_L3;
    vcmi->in_out_btn[VITAKI_CTRL_IN_REARTOUCH_RIGHT]     = VITAKI_CTRL_OUT_R3;
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_CENTER]   = VITAKI_CTRL_OUT_TOUCHPAD;

    vcmi->in_l2 = VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC;
    vcmi->in_r2 = VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC;

  } else if (controller_map_id == VITAKI_CONTROLLER_MAP_4) {
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_ANY]   = VITAKI_CTRL_OUT_TOUCHPAD;
    // no L2, R2, L3, R3

  } else if (controller_map_id == VITAKI_CONTROLLER_MAP_5) {
    // no L2, R2, L3, R3, touchpad

  } else if (controller_map_id == VITAKI_CONTROLLER_MAP_6) {
    // no L3, R3
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_CENTER]   = VITAKI_CTRL_OUT_TOUCHPAD;

    vcmi->in_l2 = VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC;
    vcmi->in_r2 = VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC;

  } else if (controller_map_id == VITAKI_CONTROLLER_MAP_7) {
    // no L3, R3
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_CENTER]   = VITAKI_CTRL_OUT_TOUCHPAD;

    vcmi->in_l2 = VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC;
    vcmi->in_r2 = VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC;

  } else if (controller_map_id == VITAKI_CONTROLLER_MAP_25) {
    // no touchpad
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC]   = VITAKI_CTRL_OUT_L3;
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC]   = VITAKI_CTRL_OUT_R3;

    vcmi->in_l2 = VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC;
    vcmi->in_r2 = VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC;

  } else if (controller_map_id == VITAKI_CONTROLLER_MAP_199) {
    vcmi->in_l2 = VITAKI_CTRL_IN_LEFT_SQUARE;
    vcmi->in_r2 = VITAKI_CTRL_IN_RIGHT_CIRCLE;
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_ANY]      = VITAKI_CTRL_OUT_TOUCHPAD;

    vcmi->in_out_btn[VITAKI_CTRL_IN_REARTOUCH_LEFT_L1] = VITAKI_CTRL_OUT_L3;
    vcmi->in_out_btn[VITAKI_CTRL_IN_REARTOUCH_RIGHT_R1] = VITAKI_CTRL_OUT_R3;

  } else if (controller_map_id == VITAKI_CONTROLLER_MAP_101) {
    vcmi->in_l2 = VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC;
    vcmi->in_r2 = VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC;
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_CENTER]   = VITAKI_CTRL_OUT_TOUCHPAD;

    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC] = VITAKI_CTRL_OUT_L3;
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC] = VITAKI_CTRL_OUT_R3;

  } else if (controller_map_id == VITAKI_CONTROLLER_MAP_102) {
    vcmi->in_l2 = VITAKI_CTRL_IN_REARTOUCH_LEFT;
    vcmi->in_r2 = VITAKI_CTRL_IN_REARTOUCH_RIGHT;
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_CENTER]   = VITAKI_CTRL_OUT_TOUCHPAD;

    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC] = VITAKI_CTRL_OUT_L3;
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC] = VITAKI_CTRL_OUT_R3;

  } else if (controller_map_id == VITAKI_CONTROLLER_MAP_103) {
    vcmi->in_l2 = VITAKI_CTRL_IN_REARTOUCH_LEFT;
    vcmi->in_r2 = VITAKI_CTRL_IN_REARTOUCH_RIGHT;
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_CENTER]   = VITAKI_CTRL_OUT_TOUCHPAD;

    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC] = VITAKI_CTRL_OUT_L3;
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC] = VITAKI_CTRL_OUT_R3;

  } else if (controller_map_id == VITAKI_CONTROLLER_MAP_104) {
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_ANY]   = VITAKI_CTRL_OUT_TOUCHPAD;
    // no L2, R2, L3, R3

  } else if (controller_map_id == VITAKI_CONTROLLER_MAP_105) {
    // no L2, R2, L3, R3, touchpad

  } else if (controller_map_id == VITAKI_CONTROLLER_MAP_106) {
    // no L2, R2
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_CENTER]   = VITAKI_CTRL_OUT_TOUCHPAD;

    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC] = VITAKI_CTRL_OUT_L3;
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC] = VITAKI_CTRL_OUT_R3;

  } else if (controller_map_id == VITAKI_CONTROLLER_MAP_107) {
    // no L2, R2
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_CENTER]   = VITAKI_CTRL_OUT_TOUCHPAD;

    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC] = VITAKI_CTRL_OUT_L3;
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC] = VITAKI_CTRL_OUT_R3;

  } else if (controller_map_id == VITAKI_CONTROLLER_MAP_125) {
    // no touchpad
    vcmi->in_l2 = VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC;
    vcmi->in_r2 = VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC;

    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC] = VITAKI_CTRL_OUT_L3;
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC] = VITAKI_CTRL_OUT_R3;

  } else if (controller_map_id == VITAKI_CONTROLLER_MAP_100) {
    vcmi->in_out_btn[VITAKI_CTRL_IN_L1]                  = VITAKI_CTRL_OUT_L1;
    vcmi->in_out_btn[VITAKI_CTRL_IN_R1]                  = VITAKI_CTRL_OUT_R1;
    vcmi->in_out_btn[VITAKI_CTRL_IN_SELECT_START]        = VITAKI_CTRL_OUT_PS;
    vcmi->in_l2 = VITAKI_CTRL_IN_REARTOUCH_LL;
    vcmi->in_r2 = VITAKI_CTRL_IN_REARTOUCH_LR;
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_ANY]      = VITAKI_CTRL_OUT_TOUCHPAD;

    vcmi->in_out_btn[VITAKI_CTRL_IN_REARTOUCH_UL] = VITAKI_CTRL_OUT_L3;
    vcmi->in_out_btn[VITAKI_CTRL_IN_REARTOUCH_UR] = VITAKI_CTRL_OUT_R3;
  } else { // default, VITAKI_CONTROLLER_MAP_0
    vcmi->in_out_btn[VITAKI_CTRL_IN_L1]                  = VITAKI_CTRL_OUT_L1;
    vcmi->in_out_btn[VITAKI_CTRL_IN_R1]                  = VITAKI_CTRL_OUT_R1;
    vcmi->in_out_btn[VITAKI_CTRL_IN_SELECT_START]        = VITAKI_CTRL_OUT_PS;
    vcmi->in_out_btn[VITAKI_CTRL_IN_REARTOUCH_LL]        = VITAKI_CTRL_OUT_L3;
    vcmi->in_out_btn[VITAKI_CTRL_IN_REARTOUCH_LR]        = VITAKI_CTRL_OUT_R3;
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_ANY]      = VITAKI_CTRL_OUT_TOUCHPAD;

    vcmi->in_l2 = VITAKI_CTRL_IN_REARTOUCH_UL;
    vcmi->in_r2 = VITAKI_CTRL_IN_REARTOUCH_UR;
  }

  vcmi->did_init = true;

  if (vcmi->in_l2 != VITAKI_CTRL_IN_NONE)
    vcmi->in_out_btn[vcmi->in_l2] = VITAKI_CTRL_OUT_L2;
  if (vcmi->in_r2 != VITAKI_CTRL_IN_NONE)
    vcmi->in_out_btn[vcmi->in_r2] = VITAKI_CTRL_OUT_R2;
}
