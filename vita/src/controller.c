#include "controller.h"
#include "ui/ui_constants.h"
#include "context.h"
#include <stdio.h>

/**
 * Controller preset definitions for the immersive controller layout
 * These map user-friendly names and descriptions to VitakiControllerMapId values
 */
const ControllerPresetDef g_controller_presets[CTRL_PRESET_COUNT] = {
    { "Default", "Standard layout with rear touch L2/R2", VITAKI_CONTROLLER_MAP_0 },
    { "Shooter", "Optimized for FPS games with quick L2/R2", VITAKI_CONTROLLER_MAP_1 },
    { "Racing", "Front touch zones for triggers", VITAKI_CONTROLLER_MAP_3 },
    { "Fighting", "Precise face button mapping", VITAKI_CONTROLLER_MAP_4 },
    { "Classic", "Remote Play default layout", VITAKI_CONTROLLER_MAP_25 },
    { "Custom", "Your personalized mapping", VITAKI_CONTROLLER_MAP_99 }
};

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

  if (controller_map_id == VITAKI_CONTROLLER_MAP_99) {
    vcmi->in_out_btn[VITAKI_CTRL_IN_LEFT_SQUARE]         = VITAKI_CTRL_OUT_L3;
    vcmi->in_out_btn[VITAKI_CTRL_IN_RIGHT_CIRCLE]        = VITAKI_CTRL_OUT_R3;
    vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_ANY]      = VITAKI_CTRL_OUT_TOUCHPAD;

    vcmi->in_l2 = VITAKI_CTRL_IN_REARTOUCH_LEFT_L1;
    vcmi->in_r2 = VITAKI_CTRL_IN_REARTOUCH_RIGHT_R1;

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
}
