#pragma once
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <vita2d.h>

// Shared font sizes for VitaRPS5 UI components (docs/ai/UI_FINAL_SPECIFICATION.md)
#define FONT_SIZE_HEADER 24      // Screen titles, primary headers
#define FONT_SIZE_SUBHEADER 18   // Section titles, tab labels
#define FONT_SIZE_BODY 16        // Paragraph/body text
#define FONT_SIZE_SMALL 14       // Secondary text, hints, status badges

typedef struct vita_chiaki_ui_state_t {
  SceTouchData touch_state_front;
  SceTouchData touch_state_back;
  uint32_t button_state;
  uint32_t old_button_state;
  int active_item;
  int next_active_item;
  int mlog_line_offset;
  uint64_t mlog_last_update;
} VitaChiakiUIState;

/// Identifiers of various widgets on the screen
typedef enum ui_main_widget_id_t {
  UI_MAIN_WIDGET_ADD_HOST_BTN,
  UI_MAIN_WIDGET_REGISTER_BTN,
  UI_MAIN_WIDGET_DISCOVERY_BTN,
  UI_MAIN_WIDGET_MESSAGES_BTN,
  UI_MAIN_WIDGET_SETTINGS_BTN,

  // needs to bitwise mask with up to 4 items (current max host count), so >=2 bits (may be increased in the future), and 4 is already occupied by MESSAGES_BTN
  UI_MAIN_WIDGET_HOST_TILE = 1 << 3,

  // FIXME: this is bound to fail REALLY fast if we start adding more inputs in the future
  UI_MAIN_WIDGET_TEXT_INPUT = 1 << 6,
} MainWidgetId;


void draw_ui();
void ui_clear_waking_wait(void);

typedef enum {
  UI_CONNECTION_STAGE_NONE = 0,
  UI_CONNECTION_STAGE_WAKING,
  UI_CONNECTION_STAGE_CONNECTING,
  UI_CONNECTION_STAGE_STARTING_STREAM,
} UIConnectionStage;

void ui_connection_begin(UIConnectionStage stage);
void ui_connection_set_stage(UIConnectionStage stage);
void ui_connection_complete(void);
void ui_connection_cancel(void);
bool ui_connection_overlay_active(void);
UIConnectionStage ui_connection_stage(void);
