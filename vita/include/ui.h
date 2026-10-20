#pragma once
#include <stdbool.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <vita2d.h>

// Font sizes and UI constants are centralized in ui_constants.h
#include "ui/ui_constants.h"

typedef struct vita_chiaki_ui_state_t {
  SceTouchData touch_state_front;
  SceTouchData touch_state_back;
  uint32_t button_state;
  uint32_t old_button_state;
  bool debug_menu_active;
  int debug_menu_selection;
} VitaChiakiUIState;

void draw_ui();
void ui_clear_waking_wait(void);
bool ui_reload_psn_account_id(void);

/** How the last read of the Vita's PSN account ID from the system registry went. */
typedef enum psn_registry_read_t {
  PSN_REGISTRY_READ_OK = 0,         ///< a valid ID was read
  PSN_REGISTRY_READ_NOT_SIGNED_IN,  ///< read fine, all zero: no PSN account on this Vita
  PSN_REGISTRY_READ_UNREADABLE,     ///< the read failed
} PsnRegistryRead;

/** ui_psn_registry_read() - The outcome of the last registry read (at launch or on refresh). */
PsnRegistryRead ui_psn_registry_read(void);

#include "ui/ui_connection_stage.h"

void ui_connection_begin(UIConnectionStage stage);
void ui_connection_set_stage(UIConnectionStage stage);
void ui_connection_complete(void);
void ui_connection_cancel(void);
bool ui_connection_overlay_active(void);
UIConnectionStage ui_connection_stage(void);
