#include "controller.h"
#include "ui/ui_constants.h"

const ControllerPresetDef g_controller_presets[CTRL_PRESET_COUNT] = {
    { "Custom 1", "Your first custom mapping", VITAKI_CONTROLLER_MAP_CUSTOM_1 },
    { "Custom 2", "Your second custom mapping", VITAKI_CONTROLLER_MAP_CUSTOM_2 },
    { "Custom 3", "Your third custom mapping", VITAKI_CONTROLLER_MAP_CUSTOM_3 }
};

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
