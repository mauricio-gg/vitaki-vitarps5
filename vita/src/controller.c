#include "controller.h"
#include "context.h"

void controller_map_storage_from_vcmi(ControllerMapStorage *storage,
                                      const VitakiCtrlMapInfo *vcmi) {
  if (!storage || !vcmi)
    return;
  memcpy(storage->in_out_btn, vcmi->in_out_btn, sizeof(storage->in_out_btn));
  storage->in_l2 = vcmi->in_l2;
  storage->in_r2 = vcmi->in_r2;
}

void controller_map_storage_apply(const ControllerMapStorage *storage, VitakiCtrlMapInfo *vcmi) {
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

static void controller_map_apply_common_bindings(VitakiCtrlMapInfo *vcmi) {
  vcmi->in_out_btn[VITAKI_CTRL_IN_L1] = VITAKI_CTRL_OUT_L1;
  vcmi->in_out_btn[VITAKI_CTRL_IN_R1] = VITAKI_CTRL_OUT_R1;
  vcmi->in_out_btn[VITAKI_CTRL_IN_SELECT_START] = VITAKI_CTRL_OUT_PS;
}

/**
 * controller_map_fill_default() - Build the one mapping a never-saved custom preset starts from.
 * @vcmi: Map to overwrite completely.
 *
 * Both the stream (controller_map_try_apply_custom_preset) and the Controller page / config
 * (controller_map_storage_set_defaults) take a never-saved preset from here, so they cannot
 * disagree. Every input the stream reads for a touch is used: the rear grid holds L2 on its left
 * three columns and R2 on its right three (host_input.c presses the trigger of the zone under the
 * finger), the front touch is Touchpad, and the L+Square / R+Circle combos are L3 / R3.
 * in_l2 / in_r2 name the first L2 / R2 zone, as ui_controller_rules.c would compute.
 */
static void controller_map_fill_default(VitakiCtrlMapInfo *vcmi) {
  memset(vcmi->in_out_btn, 0, sizeof(vcmi->in_out_btn));
  vcmi->in_l2 = VITAKI_CTRL_IN_NONE;
  vcmi->in_r2 = VITAKI_CTRL_IN_NONE;
  controller_map_apply_common_bindings(vcmi);

  vcmi->in_out_btn[VITAKI_CTRL_IN_LEFT_SQUARE] = VITAKI_CTRL_OUT_L3;
  vcmi->in_out_btn[VITAKI_CTRL_IN_RIGHT_CIRCLE] = VITAKI_CTRL_OUT_R3;
  vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_ANY] = VITAKI_CTRL_OUT_TOUCHPAD;

  for (int row = 0; row < VITAKI_REAR_TOUCH_GRID_ROWS; row++) {
    for (int col = 0; col < VITAKI_REAR_TOUCH_GRID_COLS; col++) {
      const VitakiCtrlIn zone = (VitakiCtrlIn)(VITAKI_CTRL_IN_REARTOUCH_GRID_START +
                                               row * VITAKI_REAR_TOUCH_GRID_COLS + col);
      const bool left_half = col < VITAKI_REAR_TOUCH_GRID_COLS / 2;
      vcmi->in_out_btn[zone] = left_half ? VITAKI_CTRL_OUT_L2 : VITAKI_CTRL_OUT_R2;
      if (left_half && vcmi->in_l2 == VITAKI_CTRL_IN_NONE)
        vcmi->in_l2 = zone;
      if (!left_half && vcmi->in_r2 == VITAKI_CTRL_IN_NONE)
        vcmi->in_r2 = zone;
    }
  }
  vcmi->did_init = true;
}

void controller_map_storage_set_defaults(ControllerMapStorage *storage) {
  if (!storage)
    return;
  VitakiCtrlMapInfo defaults = {0};
  controller_map_fill_default(&defaults);
  controller_map_storage_from_vcmi(storage, &defaults);
}

VitakiCtrlOut controller_map_get_output_for_input(const VitakiCtrlMapInfo *vcmi,
                                                  VitakiCtrlIn input) {
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

static bool controller_map_try_apply_custom_preset(VitakiCtrlMapInfo *vcmi,
                                                   VitakiControllerMapId controller_map_id) {
  int slot = -1;
  switch (controller_map_id) {
    case VITAKI_CONTROLLER_MAP_CUSTOM_1:
      slot = 0;
      break;
    case VITAKI_CONTROLLER_MAP_CUSTOM_2:
      slot = 1;
      break;
    case VITAKI_CONTROLLER_MAP_CUSTOM_3:
      slot = 2;
      break;
    default:
      return false;
  }

  if (context.config.custom_maps_valid[slot]) {
    controller_map_storage_apply(&context.config.custom_maps[slot], vcmi);
  } else {
    controller_map_fill_default(vcmi);
  }
  return true;
}

static void controller_map_apply_l2_r2(VitakiCtrlMapInfo *vcmi, VitakiCtrlIn l2_input,
                                       VitakiCtrlIn r2_input) {
  vcmi->in_l2 = l2_input;
  vcmi->in_r2 = r2_input;
}

static void controller_map_apply_profile(VitakiCtrlMapInfo *vcmi, VitakiCtrlIn l3_input,
                                         VitakiCtrlIn r3_input, VitakiCtrlIn touchpad_input,
                                         VitakiCtrlIn l2_input, VitakiCtrlIn r2_input) {
  if (l3_input != VITAKI_CTRL_IN_NONE)
    vcmi->in_out_btn[l3_input] = VITAKI_CTRL_OUT_L3;
  if (r3_input != VITAKI_CTRL_IN_NONE)
    vcmi->in_out_btn[r3_input] = VITAKI_CTRL_OUT_R3;
  if (touchpad_input != VITAKI_CTRL_IN_NONE)
    vcmi->in_out_btn[touchpad_input] = VITAKI_CTRL_OUT_TOUCHPAD;
  controller_map_apply_l2_r2(vcmi, l2_input, r2_input);
}

void init_controller_map(VitakiCtrlMapInfo *vcmi, VitakiControllerMapId controller_map_id) {
  // TODO make fully configurable instead of using controller_map_id
  // For now, 0 = default map, 1 = ywnico map

  // Clear map
  memset(vcmi->in_out_btn, 0, sizeof(vcmi->in_out_btn));
  vcmi->in_l2 = VITAKI_CTRL_IN_NONE;
  vcmi->in_r2 = VITAKI_CTRL_IN_NONE;

  // L1, R1, Select+Start are common in all maps currently
  controller_map_apply_common_bindings(vcmi);

  if (controller_map_try_apply_custom_preset(vcmi, controller_map_id))
    return;

  switch (controller_map_id) {
    case VITAKI_CONTROLLER_MAP_1:
      controller_map_apply_profile(
          vcmi, VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC, VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC,
          VITAKI_CTRL_IN_FRONTTOUCH_CENTER, VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC,
          VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC);
      break;
    case VITAKI_CONTROLLER_MAP_2:
      controller_map_apply_profile(vcmi, VITAKI_CTRL_IN_REARTOUCH_LEFT,
                                   VITAKI_CTRL_IN_REARTOUCH_RIGHT, VITAKI_CTRL_IN_FRONTTOUCH_CENTER,
                                   VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC,
                                   VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC);
      break;
    case VITAKI_CONTROLLER_MAP_3:
      controller_map_apply_profile(vcmi, VITAKI_CTRL_IN_REARTOUCH_LEFT,
                                   VITAKI_CTRL_IN_REARTOUCH_RIGHT, VITAKI_CTRL_IN_FRONTTOUCH_CENTER,
                                   VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC,
                                   VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC);
      break;
    case VITAKI_CONTROLLER_MAP_4:
    case VITAKI_CONTROLLER_MAP_104:
      vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_ANY] = VITAKI_CTRL_OUT_TOUCHPAD;
      break;
    case VITAKI_CONTROLLER_MAP_5:
    case VITAKI_CONTROLLER_MAP_105:
      break;
    case VITAKI_CONTROLLER_MAP_6:
      controller_map_apply_profile(
          vcmi, VITAKI_CTRL_IN_NONE, VITAKI_CTRL_IN_NONE, VITAKI_CTRL_IN_FRONTTOUCH_CENTER,
          VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC, VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC);
      break;
    case VITAKI_CONTROLLER_MAP_7:
      controller_map_apply_profile(
          vcmi, VITAKI_CTRL_IN_NONE, VITAKI_CTRL_IN_NONE, VITAKI_CTRL_IN_FRONTTOUCH_CENTER,
          VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC, VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC);
      break;
    case VITAKI_CONTROLLER_MAP_25:
      controller_map_apply_profile(
          vcmi, VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC, VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC,
          VITAKI_CTRL_IN_NONE, VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC, VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC);
      break;
    case VITAKI_CONTROLLER_MAP_125:
      controller_map_apply_profile(
          vcmi, VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC, VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC,
          VITAKI_CTRL_IN_NONE, VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC, VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC);
      break;
    case VITAKI_CONTROLLER_MAP_199:
      controller_map_apply_profile(vcmi, VITAKI_CTRL_IN_REARTOUCH_LEFT_L1,
                                   VITAKI_CTRL_IN_REARTOUCH_RIGHT_R1, VITAKI_CTRL_IN_FRONTTOUCH_ANY,
                                   VITAKI_CTRL_IN_LEFT_SQUARE, VITAKI_CTRL_IN_RIGHT_CIRCLE);
      break;
    case VITAKI_CONTROLLER_MAP_101:
      controller_map_apply_profile(
          vcmi, VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC, VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC,
          VITAKI_CTRL_IN_FRONTTOUCH_CENTER, VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC,
          VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC);
      break;
    case VITAKI_CONTROLLER_MAP_102:
      controller_map_apply_profile(vcmi, VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC,
                                   VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC,
                                   VITAKI_CTRL_IN_FRONTTOUCH_CENTER, VITAKI_CTRL_IN_REARTOUCH_LEFT,
                                   VITAKI_CTRL_IN_REARTOUCH_RIGHT);
      break;
    case VITAKI_CONTROLLER_MAP_103:
      controller_map_apply_profile(vcmi, VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC,
                                   VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC,
                                   VITAKI_CTRL_IN_FRONTTOUCH_CENTER, VITAKI_CTRL_IN_REARTOUCH_LEFT,
                                   VITAKI_CTRL_IN_REARTOUCH_RIGHT);
      break;
    case VITAKI_CONTROLLER_MAP_106:
      controller_map_apply_profile(
          vcmi, VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC, VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC,
          VITAKI_CTRL_IN_FRONTTOUCH_CENTER, VITAKI_CTRL_IN_NONE, VITAKI_CTRL_IN_NONE);
      break;
    case VITAKI_CONTROLLER_MAP_107:
      controller_map_apply_profile(
          vcmi, VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC, VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC,
          VITAKI_CTRL_IN_FRONTTOUCH_CENTER, VITAKI_CTRL_IN_NONE, VITAKI_CTRL_IN_NONE);
      break;
    case VITAKI_CONTROLLER_MAP_100:
      controller_map_apply_profile(vcmi, VITAKI_CTRL_IN_REARTOUCH_UL, VITAKI_CTRL_IN_REARTOUCH_UR,
                                   VITAKI_CTRL_IN_FRONTTOUCH_ANY, VITAKI_CTRL_IN_REARTOUCH_LL,
                                   VITAKI_CTRL_IN_REARTOUCH_LR);
      break;
    case VITAKI_CONTROLLER_MAP_0:
    case VITAKI_CONTROLLER_MAP_99:
    default:
      controller_map_apply_profile(vcmi, VITAKI_CTRL_IN_REARTOUCH_LL, VITAKI_CTRL_IN_REARTOUCH_LR,
                                   VITAKI_CTRL_IN_FRONTTOUCH_ANY, VITAKI_CTRL_IN_REARTOUCH_UL,
                                   VITAKI_CTRL_IN_REARTOUCH_UR);
      break;
  }

  vcmi->did_init = true;

  if (vcmi->in_l2 != VITAKI_CTRL_IN_NONE)
    vcmi->in_out_btn[vcmi->in_l2] = VITAKI_CTRL_OUT_L2;
  if (vcmi->in_r2 != VITAKI_CTRL_IN_NONE)
    vcmi->in_out_btn[vcmi->in_r2] = VITAKI_CTRL_OUT_R2;
}
