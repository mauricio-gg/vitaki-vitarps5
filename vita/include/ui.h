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
  bool debug_menu_modal_pushed;
  int debug_menu_selection;
} VitaChiakiUIState;

void draw_ui();
void ui_clear_waking_wait(void);
bool ui_reload_psn_account_id(void);

#include "ui/ui_connection_stage.h"

void ui_connection_begin(UIConnectionStage stage);
void ui_connection_set_stage(UIConnectionStage stage);
void ui_connection_complete(void);
void ui_connection_cancel(void);
bool ui_connection_overlay_active(void);
UIConnectionStage ui_connection_stage(void);
