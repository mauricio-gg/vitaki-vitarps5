#pragma once
#include <chiaki/common.h>
#include <chiaki/controller.h>

// TODO controls should be made fully customizable in the future.
// For now, use a set of defined maps
typedef enum vitaki_controller_map_id_t {
  VITAKI_CONTROLLER_MAP_0      =  0, // L2, R2 rear touchscreen upper corners; L3, R3 rear touchscreen lower corners; touchpad entire front
  VITAKI_CONTROLLER_MAP_1      =  1, // L2, R2 front touchscreen upper corners; L3, R3 front touchscreen lower corners; touchpad front middle
  VITAKI_CONTROLLER_MAP_2      =  2, // L2, R2 front touchscreen lower corners; L3 R3 on back; touchpad front middle
  VITAKI_CONTROLLER_MAP_3      =  3, // L2, R2 front touchscreen upper corners; L3 R3 on back; touchpad front middle
  VITAKI_CONTROLLER_MAP_4      =  4, // no L2,R2,L3,R3 mapping; touchpad entire front
  VITAKI_CONTROLLER_MAP_5      =  5, // no L2,R2,L3,R3,touchpad mapping
  VITAKI_CONTROLLER_MAP_6      =  6, // L2, R2 front touchscreen lower corners; no L3 R3; touchpad front middle
  VITAKI_CONTROLLER_MAP_7      =  7, // L2, R2 front touchscreen upper corners; no L3 R3; touchpad front middle
  VITAKI_CONTROLLER_MAP_25     = 25, // L2, R2 front touchscreen upper corners; L3, R3 front touchscreen lower corners; no touchpad
  VITAKI_CONTROLLER_MAP_99     = 99, // map ywnico randomly came up with
  VITAKI_CONTROLLER_MAP_100    =  100, // L3, R3 rear touchscreen upper corners; L2, R2 rear touchscreen lower corners; touchpad entire front
  VITAKI_CONTROLLER_MAP_101    =  101, // L3, R3 front touchscreen upper corners; L2, R2 front touchscreen lower corners; touchpad front middle
  VITAKI_CONTROLLER_MAP_102    =  102, // L3, R3 front touchscreen lower corners; L2 R2 on back; touchpad front middle
  VITAKI_CONTROLLER_MAP_103    =  103, // L3, R3 front touchscreen upper corners; L2 R2 on back; touchpad front middle
  VITAKI_CONTROLLER_MAP_104    =  104, // no L3,R3,L2,R2 mapping; touchpad entire front
  VITAKI_CONTROLLER_MAP_105    =  105, // no L3,R3,L2,R2,touchpad mapping
  VITAKI_CONTROLLER_MAP_106    =  106, // L3, R3 front touchscreen lower corners; no L2 R2; touchpad front middle
  VITAKI_CONTROLLER_MAP_107    =  107, // L3, R3 front touchscreen upper corners; no L2 R2; touchpad front middle
  VITAKI_CONTROLLER_MAP_125    = 125, // L3, R3 front touchscreen upper corners; L2, R2 front touchscreen lower corners; no touchpad
  VITAKI_CONTROLLER_MAP_199    = 199, // map ywnico randomly came up with (swap L2R2 <> L3R3)
  // Custom preset slots - user-configurable mappings
  VITAKI_CONTROLLER_MAP_CUSTOM_1 = 201,
  VITAKI_CONTROLLER_MAP_CUSTOM_2 = 202,
  VITAKI_CONTROLLER_MAP_CUSTOM_3 = 203,
} VitakiControllerMapId;

// Control buttons (used for array indices => start at 0)
typedef enum vitaki_ctrl_in_t {
  VITAKI_CTRL_IN_NONE = 0,

  VITAKI_CTRL_IN_L1,
  VITAKI_CTRL_IN_R1,

  VITAKI_CTRL_IN_SELECT_START,
  VITAKI_CTRL_IN_LEFT_SQUARE,
  VITAKI_CTRL_IN_RIGHT_CIRCLE,

  VITAKI_CTRL_IN_REARTOUCH_UL,
  VITAKI_CTRL_IN_REARTOUCH_UR,
  VITAKI_CTRL_IN_REARTOUCH_LL,
  VITAKI_CTRL_IN_REARTOUCH_LR,
  VITAKI_CTRL_IN_REARTOUCH_LEFT,
  VITAKI_CTRL_IN_REARTOUCH_RIGHT,
  VITAKI_CTRL_IN_REARTOUCH_ANY,
  VITAKI_CTRL_IN_REARTOUCH_LEFT_L1,
  VITAKI_CTRL_IN_REARTOUCH_RIGHT_R1,

  VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC,
  VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC,
  VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC,
  VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC,
  VITAKI_CTRL_IN_FRONTTOUCH_LEFT,
  VITAKI_CTRL_IN_FRONTTOUCH_RIGHT,
  VITAKI_CTRL_IN_FRONTTOUCH_CENTER,
  VITAKI_CTRL_IN_FRONTTOUCH_ANY,

  VITAKI_CTRL_IN_FRONTTOUCH_GRID_R0C0,
  VITAKI_CTRL_IN_FRONTTOUCH_GRID_R0C1,
  VITAKI_CTRL_IN_FRONTTOUCH_GRID_R0C2,
  VITAKI_CTRL_IN_FRONTTOUCH_GRID_R0C3,
  VITAKI_CTRL_IN_FRONTTOUCH_GRID_R0C4,
  VITAKI_CTRL_IN_FRONTTOUCH_GRID_R0C5,
  VITAKI_CTRL_IN_FRONTTOUCH_GRID_R1C0,
  VITAKI_CTRL_IN_FRONTTOUCH_GRID_R1C1,
  VITAKI_CTRL_IN_FRONTTOUCH_GRID_R1C2,
  VITAKI_CTRL_IN_FRONTTOUCH_GRID_R1C3,
  VITAKI_CTRL_IN_FRONTTOUCH_GRID_R1C4,
  VITAKI_CTRL_IN_FRONTTOUCH_GRID_R1C5,
  VITAKI_CTRL_IN_FRONTTOUCH_GRID_R2C0,
  VITAKI_CTRL_IN_FRONTTOUCH_GRID_R2C1,
  VITAKI_CTRL_IN_FRONTTOUCH_GRID_R2C2,
  VITAKI_CTRL_IN_FRONTTOUCH_GRID_R2C3,
  VITAKI_CTRL_IN_FRONTTOUCH_GRID_R2C4,
  VITAKI_CTRL_IN_FRONTTOUCH_GRID_R2C5,

  VITAKI_CTRL_IN_REARTOUCH_GRID_R0C0,
  VITAKI_CTRL_IN_REARTOUCH_GRID_R0C1,
  VITAKI_CTRL_IN_REARTOUCH_GRID_R0C2,
  VITAKI_CTRL_IN_REARTOUCH_GRID_R0C3,
  VITAKI_CTRL_IN_REARTOUCH_GRID_R0C4,
  VITAKI_CTRL_IN_REARTOUCH_GRID_R0C5,
  VITAKI_CTRL_IN_REARTOUCH_GRID_R1C0,
  VITAKI_CTRL_IN_REARTOUCH_GRID_R1C1,
  VITAKI_CTRL_IN_REARTOUCH_GRID_R1C2,
  VITAKI_CTRL_IN_REARTOUCH_GRID_R1C3,
  VITAKI_CTRL_IN_REARTOUCH_GRID_R1C4,
  VITAKI_CTRL_IN_REARTOUCH_GRID_R1C5,
  VITAKI_CTRL_IN_REARTOUCH_GRID_R2C0,
  VITAKI_CTRL_IN_REARTOUCH_GRID_R2C1,
  VITAKI_CTRL_IN_REARTOUCH_GRID_R2C2,
  VITAKI_CTRL_IN_REARTOUCH_GRID_R2C3,
  VITAKI_CTRL_IN_REARTOUCH_GRID_R2C4,
  VITAKI_CTRL_IN_REARTOUCH_GRID_R2C5,

  VITAKI_CTRL_IN_COUNT, // final element to count the total number
} VitakiCtrlIn;

#define VITAKI_REAR_TOUCH_GRID_COLS 6
#define VITAKI_REAR_TOUCH_GRID_ROWS 3
#define VITAKI_CTRL_IN_REARTOUCH_GRID_START VITAKI_CTRL_IN_REARTOUCH_GRID_R0C0
#define VITAKI_CTRL_IN_REARTOUCH_GRID_COUNT (VITAKI_REAR_TOUCH_GRID_COLS * VITAKI_REAR_TOUCH_GRID_ROWS)
#define VITAKI_CTRL_IN_REARTOUCH_GRID_END (VITAKI_CTRL_IN_REARTOUCH_GRID_START + VITAKI_CTRL_IN_REARTOUCH_GRID_COUNT)

#define VITAKI_FRONT_TOUCH_GRID_COLS 6
#define VITAKI_FRONT_TOUCH_GRID_ROWS 3
#define VITAKI_CTRL_IN_FRONTTOUCH_GRID_START VITAKI_CTRL_IN_FRONTTOUCH_GRID_R0C0
#define VITAKI_CTRL_IN_FRONTTOUCH_GRID_COUNT (VITAKI_FRONT_TOUCH_GRID_COLS * VITAKI_FRONT_TOUCH_GRID_ROWS)
#define VITAKI_CTRL_IN_FRONTTOUCH_GRID_END (VITAKI_CTRL_IN_FRONTTOUCH_GRID_START + VITAKI_CTRL_IN_FRONTTOUCH_GRID_COUNT)

static inline bool vitaki_ctrl_in_is_rear_grid(VitakiCtrlIn input) {
  return input >= VITAKI_CTRL_IN_REARTOUCH_GRID_START && input < VITAKI_CTRL_IN_REARTOUCH_GRID_END;
}

static inline int vitaki_ctrl_in_rear_grid_index(VitakiCtrlIn input) {
  return (int)(input - VITAKI_CTRL_IN_REARTOUCH_GRID_START);
}

static inline int vitaki_ctrl_in_rear_grid_row(VitakiCtrlIn input) {
  return vitaki_ctrl_in_rear_grid_index(input) / VITAKI_REAR_TOUCH_GRID_COLS;
}

static inline int vitaki_ctrl_in_rear_grid_col(VitakiCtrlIn input) {
  return vitaki_ctrl_in_rear_grid_index(input) % VITAKI_REAR_TOUCH_GRID_COLS;
}

static inline bool vitaki_ctrl_in_is_front_grid(VitakiCtrlIn input) {
  return input >= VITAKI_CTRL_IN_FRONTTOUCH_GRID_START && input < VITAKI_CTRL_IN_FRONTTOUCH_GRID_END;
}

static inline int vitaki_ctrl_in_front_grid_index(VitakiCtrlIn input) {
  return (int)(input - VITAKI_CTRL_IN_FRONTTOUCH_GRID_START);
}

static inline int vitaki_ctrl_in_front_grid_row(VitakiCtrlIn input) {
  return vitaki_ctrl_in_front_grid_index(input) / VITAKI_FRONT_TOUCH_GRID_COLS;
}

static inline int vitaki_ctrl_in_front_grid_col(VitakiCtrlIn input) {
  return vitaki_ctrl_in_front_grid_index(input) % VITAKI_FRONT_TOUCH_GRID_COLS;
}

typedef enum vitaki_ctrl_out_t {
  VITAKI_CTRL_OUT_UP       = CHIAKI_CONTROLLER_BUTTON_DPAD_UP,
  VITAKI_CTRL_OUT_DOWN     = CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN,
  VITAKI_CTRL_OUT_LEFT     = CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT,
  VITAKI_CTRL_OUT_RIGHT    = CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT,

  VITAKI_CTRL_OUT_TRIANGLE = CHIAKI_CONTROLLER_BUTTON_PYRAMID,
  VITAKI_CTRL_OUT_CIRCLE   = CHIAKI_CONTROLLER_BUTTON_MOON,
  VITAKI_CTRL_OUT_CROSS    = CHIAKI_CONTROLLER_BUTTON_CROSS,
  VITAKI_CTRL_OUT_SQUARE   = CHIAKI_CONTROLLER_BUTTON_BOX,

  VITAKI_CTRL_OUT_SHARE    = CHIAKI_CONTROLLER_BUTTON_SHARE,
  VITAKI_CTRL_OUT_OPTIONS  = CHIAKI_CONTROLLER_BUTTON_OPTIONS,
  VITAKI_CTRL_OUT_PS       = CHIAKI_CONTROLLER_BUTTON_PS,
  VITAKI_CTRL_OUT_TOUCHPAD = CHIAKI_CONTROLLER_BUTTON_TOUCHPAD,

  VITAKI_CTRL_OUT_L1       = CHIAKI_CONTROLLER_BUTTON_L1,
  VITAKI_CTRL_OUT_R1       = CHIAKI_CONTROLLER_BUTTON_R1,
  VITAKI_CTRL_OUT_L3       = CHIAKI_CONTROLLER_BUTTON_L3,
  VITAKI_CTRL_OUT_R3       = CHIAKI_CONTROLLER_BUTTON_R3,

  // TODO be careful about avoiding collision
  VITAKI_CTRL_OUT_L2       = 3,
  VITAKI_CTRL_OUT_R2       = 7,

  // VITAKI_CTRL_OUT_NONE must be 0 for logic to work
  VITAKI_CTRL_OUT_NONE     = 0,
} VitakiCtrlOut;

typedef struct vitaki_ctrl_map_info_t {
  bool in_state[VITAKI_CTRL_IN_COUNT];
  int in_out_btn[VITAKI_CTRL_IN_COUNT];
  int in_l2;
  int in_r2;
  bool did_init;
} VitakiCtrlMapInfo;

typedef struct controller_map_storage_t {
  int in_out_btn[VITAKI_CTRL_IN_COUNT];
  int in_l2;
  int in_r2;
} ControllerMapStorage;

/**
 * Controller preset definition for the immersive controller layout
 * Provides user-friendly names and descriptions for controller map IDs
 */
typedef struct controller_preset_def_t {
    const char* name;           // Display name (e.g., "Default", "Shooter")
    const char* description;    // Short description for 60px area
    VitakiControllerMapId map_id;
} ControllerPresetDef;

/**
 * 3 custom preset slots: Custom 1, Custom 2, Custom 3
 * Global array defined in controller.c
 */
extern const ControllerPresetDef g_controller_presets[];

void init_controller_map(VitakiCtrlMapInfo* vcmi, VitakiControllerMapId controller_map_id);
void controller_map_storage_from_vcmi(ControllerMapStorage* storage, const VitakiCtrlMapInfo* vcmi);
void controller_map_storage_apply(const ControllerMapStorage* storage, VitakiCtrlMapInfo* vcmi);
void controller_map_storage_set_defaults(ControllerMapStorage* storage);
VitakiCtrlOut controller_map_get_output_for_input(const VitakiCtrlMapInfo* vcmi, VitakiCtrlIn input);
const char* controller_output_name(VitakiCtrlOut button);
const char* controller_output_symbol(VitakiCtrlOut button);
