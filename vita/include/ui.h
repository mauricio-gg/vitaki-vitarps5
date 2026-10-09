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
  int active_item;
  int next_active_item;
  bool debug_menu_active;
  bool debug_menu_modal_pushed;
  int debug_menu_selection;
} VitaChiakiUIState;

/// Identifiers of various widgets on the screen
#ifndef UI_MAIN_WIDGET_ID_DEFINED
#define UI_MAIN_WIDGET_ID_DEFINED
typedef enum ui_main_widget_id_t {
  UI_MAIN_WIDGET_ADD_HOST_BTN,
  UI_MAIN_WIDGET_REGISTER_BTN,
  UI_MAIN_WIDGET_DISCOVERY_BTN,
  UI_MAIN_WIDGET_MESSAGES_BTN,
  UI_MAIN_WIDGET_SETTINGS_BTN,

  // needs to bitwise mask with up to 4 items (current max host count), so >=2 bits (may be
  // increased in the future), and 4 is already occupied by MESSAGES_BTN
  UI_MAIN_WIDGET_HOST_TILE = 1 << 3,

  // FIXME: this is bound to fail REALLY fast if we start adding more inputs in the future
  UI_MAIN_WIDGET_TEXT_INPUT = 1 << 6,
} MainWidgetId;
#endif

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
