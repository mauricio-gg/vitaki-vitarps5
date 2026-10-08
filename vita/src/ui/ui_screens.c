/**
 * @file ui_screens.c
 * @brief Screen rendering implementations for VitaRPS5
 *
 * All screen implementations extracted from ui.c (~2000 lines):
 * - Main menu with console cards
 * - Controller configuration screen
 * - Waking/connecting overlay
 * - Reconnecting overlay
 * - Registration dialog (PIN entry)
 * - Stream overlay
 * - Messages screen
 *
 * This is Phase 7 of the UI refactoring - the largest extraction.
 */

#include <sys/param.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>

#include "context.h"
#include "host.h"
#include "host_feedback.h"
#include "ui.h"
#include "util.h"
#include "video.h"
#include "ui/ui_screens.h"
#include "ui/ui_settings_actions.h"
#include "ui/ui_internal.h"
#include "ui/ui_components.h"
#include "ui/ui_connecting.h"
#include "ui/ui_reconnecting.h"
#include "ui/ui_input.h"
#include "ui/ui_focus.h"
#include "ui/ui_state.h"
#include "ui/ui_graphics.h"
#include "ui/ui_text.h"
#include "ui/ui_home.h"

// ============================================================================
// Constants (use definitions from ui_constants.h via ui_internal.h)
// ============================================================================

#define VIDEO_LOSS_ALERT_DEFAULT_US (5 * 1000 * 1000ULL)
#define WAKE_ERROR_HINT_DURATION_US (7 * 1000 * 1000ULL)

// Legacy colors not yet in ui_constants.h
#define COLOR_WHITE RGBA8(255, 255, 255, 255)
#define COLOR_GRAY50 RGBA8(129, 129, 129, 255)
#define COLOR_BLACK RGBA8(0, 0, 0, 255)

// ============================================================================
// Module-local state
// ============================================================================

// Touch input state pointers (initialized in ui_screens_init)
static bool *touch_block_active = NULL;
static bool *touch_block_pending_clear = NULL;

// ============================================================================
// Forward declarations for helper functions
// ============================================================================

static inline void open_mapping_popup_single(VitakiCtrlIn input, bool is_front);
static bool request_host_wakeup_with_feedback(VitaChiakiHost *host, const char *reason,
                                              bool continue_on_failure);
static void show_cooldown_hint(VitaChiakiHost *host);

static bool request_host_wakeup_with_feedback(VitaChiakiHost *host, const char *reason,
                                              bool continue_on_failure) {
  if (!host) {
    LOGE("Wake requested with null host (%s)", reason ? reason : "unknown");
    return false;
  }

  bool discovered = (host->type & DISCOVERED) && host->discovery_state;
  bool at_rest =
      discovered && host->discovery_state_snapshot == CHIAKI_DISCOVERY_HOST_STATE_STANDBY;
  bool registered = (host->type & REGISTERED) != 0;
  bool manual = (host->type & MANUALLY_ADDED) != 0;
  LOGD("Wake request (%s): host=%s flags(reg=%d disc=%d manual=%d rest=%d)",
       reason ? reason : "unknown", host->hostname[0] ? host->hostname : "<null>",
       registered ? 1 : 0, discovered ? 1 : 0, manual ? 1 : 0, at_rest ? 1 : 0);

  if (host_wakeup(host) != 0) {
    if (continue_on_failure) {
      host_set_hint(host, "Wake signal failed; attempting connection anyway.", false,
                    WAKE_ERROR_HINT_DURATION_US);
    } else {
      host_set_hint(host, "Wake signal failed. Check pairing and network.", true,
                    WAKE_ERROR_HINT_DURATION_US);
    }
    LOGE("Wake request failed (%s): host=%s", reason ? reason : "unknown",
         host->hostname[0] ? host->hostname : "<null>");
    return false;
  }

  return true;
}

/**
 * Show the "console releasing session" hint when a connect attempt is
 * blocked by the post-stop/error cooldown gate. Duration is computed from
 * context.stream.next_stream_allowed_us so the hint self-expires exactly
 * when the gate clears.
 */
static void show_cooldown_hint(VitaChiakiHost *host) {
  uint64_t now_us = sceKernelGetProcessTimeWide();
  uint64_t remaining_us = context.stream.next_stream_allowed_us > now_us
                              ? context.stream.next_stream_allowed_us - now_us
                              : 0;
  char hint_msg[64];
  snprintf(hint_msg, sizeof(hint_msg), "Console releasing session... ready in %llus",
           (unsigned long long)((remaining_us + 999999ULL) / 1000000ULL));
  host_set_hint(host, hint_msg, false, remaining_us);
}

// ============================================================================
// Console connect and re-pair (called by the Home screen, ui_home.c)
// ============================================================================

/**
 * ui_screens_connect_host() - Run the Confirm flow for @host: pair, wake or connect.
 *
 * Applies the cooldown gate, resets the RP_IN_USE auto-retry, then routes to the PIN
 * screen (unregistered), the wake flow (standby) or a connection thread. Consumes
 * context.stream.force_psn_holepunch.
 *
 * @return the screen to show next (MAIN when nothing started)
 */
UIScreenType ui_screens_connect_host(VitaChiakiHost *host) {
  /* Capture and clear force_psn_holepunch so early-return paths never leak the
   * flag into a future LAN attempt. Restored only where start_connection_thread
   * is invoked — including the standby-wake path, where ui_screen_draw_waking()
   * defers the actual thread start. */
  bool saved_force_psn = context.stream.force_psn_holepunch;
  context.stream.force_psn_holepunch = false;

  if (!host)
    return UI_SCREEN_TYPE_MAIN;

  LOGD("ui_screens_connect_host: host_ptr=%p source=%d type=0x%x hostname=%s", (void *)host,
       host->source, host->type, host->hostname[0] ? host->hostname : "<null>");
  context.active_host = host;
  if (takion_cooldown_gate_active()) {
    LOGD("Ignoring connect request — network recovery cooldown active");
    show_cooldown_hint(context.active_host);
    return UI_SCREEN_TYPE_MAIN;
  }
  // A manual connect attempt cancels any pending RP_IN_USE auto-retry
  // (Piece C) rather than racing with it, and earns a fresh one-shot
  // auto-retry budget of its own -- each explicit user press is naturally
  // bounded (it requires the user to act), so there's no risk of an
  // unbounded retry loop from resetting this here.
  context.stream.rp_in_use_retry_pending = false;
  context.stream.rp_in_use_retry_used = false;

  bool discovered =
      (context.active_host->type & DISCOVERED) && (context.active_host->discovery_state);
  bool registered = context.active_host->type & REGISTERED;
  bool added = context.active_host->type & MANUALLY_ADDED;
  bool at_rest = discovered && context.active_host->discovery_state_snapshot ==
                                   CHIAKI_DISCOVERY_HOST_STATE_STANDBY;

  if (!registered)
    return UI_SCREEN_TYPE_REGISTER_HOST;
  if (at_rest) {
    LOGD("Waking dormant console...");
    ui_connection_begin(UI_CONNECTION_STAGE_WAKING);
    if (request_host_wakeup_with_feedback(context.active_host, "cross-standby", false)) {
      /* Restore the flag so ui_screen_draw_waking()'s deferred
       * start_connection_thread() honours the user's Internet choice. */
      context.stream.force_psn_holepunch = saved_force_psn;
      return UI_SCREEN_TYPE_WAKING;
    }
    /* Wake request failed — connection will not proceed; leave flag cleared. */
    ui_connection_cancel();
    return UI_SCREEN_TYPE_MAIN;
  }

  if (added) {
    // Manual hosts may not have fresh discovery state; nudge wake before connect.
    request_host_wakeup_with_feedback(context.active_host, "cross-manual-preconnect", true);
  }

  context.stream.force_psn_holepunch = saved_force_psn;
  ui_connection_begin(UI_CONNECTION_STAGE_CONNECTING);
  if (!start_connection_thread(context.active_host)) {
    /* Thread never started — clear the flag so it cannot bleed into the
     * next connection attempt. */
    context.stream.force_psn_holepunch = false;
    ui_connection_cancel();
    return UI_SCREEN_TYPE_MAIN;
  }
  ui_state_set_waking_wait_for_stream_us(sceKernelGetProcessTimeWide());
  return UI_SCREEN_TYPE_WAKING;
}

/**
 * ui_screens_repair_host() - Unregister @host and send the user to the PIN screen.
 *
 * @return UI_SCREEN_TYPE_REGISTER_HOST, or MAIN when @host is NULL or not paired
 */
UIScreenType ui_screens_repair_host(VitaChiakiHost *host) {
  if (!host || !(host->type & REGISTERED))
    return UI_SCREEN_TYPE_MAIN;

  LOGD("Re-pairing console: %s", host->hostname);
  if (host->registered_state) {
    free(host->registered_state);
    host->registered_state = NULL;
  }

  for (int j = 0; j < context.config.num_registered_hosts; j++) {
    if (context.config.registered_hosts[j] == host) {
      for (int k = j; k < context.config.num_registered_hosts - 1; k++)
        context.config.registered_hosts[k] = context.config.registered_hosts[k + 1];
      context.config.registered_hosts[context.config.num_registered_hosts - 1] = NULL;
      context.config.num_registered_hosts--;
      break;
    }
  }

  host->type &= ~REGISTERED;
  ui_settings_persist_config();
  LOGD("Registration data deleted for console: %s", host->hostname);

  context.active_host = host;
  return UI_SCREEN_TYPE_REGISTER_HOST;
}

UIScreenType ui_screen_draw_main(void) {
  return ui_home_frame();
}

// ============================================================================
// CONTROLLER CONFIGURATION SCREEN (STANDARD WAVE NAV LAYOUT)
// ============================================================================

#include "ui/ui_controller_diagram.h"

// Static state for controller screen
static DiagramState ctrl_diagram = {0};
static int ctrl_legend_scroll = 0;
static int ctrl_preset_index = 0;
static ControllerViewMode ctrl_view_mode = CTRL_VIEW_FRONT;
static bool ctrl_initialized = false;
static VitakiCtrlMapInfo ctrl_preview_map = {0};
static bool ctrl_popup_active = false;
static int ctrl_popup_selection = 0;
static bool ctrl_popup_front = true;
static VitakiCtrlIn ctrl_popup_input = VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC;
static VitakiCtrlIn ctrl_popup_inputs[VITAKI_CTRL_IN_COUNT];
static int ctrl_popup_input_count = 0;
static VitakiCtrlOut ctrl_last_mapping_output = VITAKI_CTRL_OUT_L2;
static int ctrl_popup_scroll = 0;
static bool ctrl_popup_touch_down = false;
static int ctrl_popup_touch_choice = -1;
static bool ctrl_popup_dragging = false;
static float ctrl_popup_touch_initial_y = 0.0f;
static float ctrl_popup_touch_last_y = 0.0f;
static float ctrl_popup_drag_accum = 0.0f;
#define FRONT_GRID_COUNT (VITAKI_FRONT_TOUCH_GRID_COUNT)
#define FRONT_SLOT_COUNT FRONT_GRID_COUNT
#define BACK_GRID_COUNT (VITAKI_REAR_TOUCH_GRID_ROWS * VITAKI_REAR_TOUCH_GRID_COLS)
#define BACK_SLOT_COUNT (sizeof(k_back_touch_slots) / sizeof(k_back_touch_slots[0]))
#define MAPPING_OPTION_COUNT (sizeof(k_mapping_options) / sizeof(k_mapping_options[0]))
#define POPUP_VISIBLE_OPTIONS 4
#define POPUP_ROW_HEIGHT 44
#define TOUCH_DEBOUNCE_FRAMES 10  // Debounce frames for touch input (~166ms at 60fps)

static int ctrl_front_cursor_index = 0;
static int ctrl_front_cursor_row = 0;
static int ctrl_front_cursor_col = 0;
static bool ctrl_front_drag_active = false;
static bool ctrl_front_touch_active = false;
static bool ctrl_front_selection[VITAKI_FRONT_TOUCH_GRID_COUNT] = {0};
static int ctrl_front_selection_count = 0;
static int ctrl_front_drag_path[VITAKI_FRONT_TOUCH_GRID_COUNT] = {0};
static int ctrl_front_drag_path_len = 0;
static int ctrl_back_cursor_index = 0;
static int ctrl_back_cursor_row = 0;
static int ctrl_back_cursor_col = 0;
static bool ctrl_back_drag_active = false;
static bool ctrl_back_touch_active = false;
static bool ctrl_back_selection[BACK_GRID_COUNT] = {0};
static int ctrl_back_selection_count = 0;
static int ctrl_back_drag_path[BACK_GRID_COUNT] = {0};
static int ctrl_back_drag_path_len = 0;
static int ctrl_summary_shoulder_index = 0;

typedef struct mapping_option_t {
  VitakiCtrlOut output;
} MappingOption;

static const VitakiCtrlIn k_front_touch_slots[] = {
    VITAKI_CTRL_IN_FRONTTOUCH_GRID_R0C0, VITAKI_CTRL_IN_FRONTTOUCH_GRID_R0C1,
    VITAKI_CTRL_IN_FRONTTOUCH_GRID_R0C2, VITAKI_CTRL_IN_FRONTTOUCH_GRID_R0C3,
    VITAKI_CTRL_IN_FRONTTOUCH_GRID_R0C4, VITAKI_CTRL_IN_FRONTTOUCH_GRID_R0C5,
    VITAKI_CTRL_IN_FRONTTOUCH_GRID_R1C0, VITAKI_CTRL_IN_FRONTTOUCH_GRID_R1C1,
    VITAKI_CTRL_IN_FRONTTOUCH_GRID_R1C2, VITAKI_CTRL_IN_FRONTTOUCH_GRID_R1C3,
    VITAKI_CTRL_IN_FRONTTOUCH_GRID_R1C4, VITAKI_CTRL_IN_FRONTTOUCH_GRID_R1C5,
    VITAKI_CTRL_IN_FRONTTOUCH_GRID_R2C0, VITAKI_CTRL_IN_FRONTTOUCH_GRID_R2C1,
    VITAKI_CTRL_IN_FRONTTOUCH_GRID_R2C2, VITAKI_CTRL_IN_FRONTTOUCH_GRID_R2C3,
    VITAKI_CTRL_IN_FRONTTOUCH_GRID_R2C4, VITAKI_CTRL_IN_FRONTTOUCH_GRID_R2C5,
};

static const VitakiCtrlIn k_back_touch_slots[] = {
    VITAKI_CTRL_IN_REARTOUCH_GRID_R0C0, VITAKI_CTRL_IN_REARTOUCH_GRID_R0C1,
    VITAKI_CTRL_IN_REARTOUCH_GRID_R0C2, VITAKI_CTRL_IN_REARTOUCH_GRID_R0C3,
    VITAKI_CTRL_IN_REARTOUCH_GRID_R0C4, VITAKI_CTRL_IN_REARTOUCH_GRID_R0C5,
    VITAKI_CTRL_IN_REARTOUCH_GRID_R1C0, VITAKI_CTRL_IN_REARTOUCH_GRID_R1C1,
    VITAKI_CTRL_IN_REARTOUCH_GRID_R1C2, VITAKI_CTRL_IN_REARTOUCH_GRID_R1C3,
    VITAKI_CTRL_IN_REARTOUCH_GRID_R1C4, VITAKI_CTRL_IN_REARTOUCH_GRID_R1C5,
    VITAKI_CTRL_IN_REARTOUCH_GRID_R2C0, VITAKI_CTRL_IN_REARTOUCH_GRID_R2C1,
    VITAKI_CTRL_IN_REARTOUCH_GRID_R2C2, VITAKI_CTRL_IN_REARTOUCH_GRID_R2C3,
    VITAKI_CTRL_IN_REARTOUCH_GRID_R2C4, VITAKI_CTRL_IN_REARTOUCH_GRID_R2C5,
    VITAKI_CTRL_IN_REARTOUCH_ANY,
};

static const char *k_back_touch_labels[] = {
    "Rear A1", "Rear B1", "Rear C1", "Rear D1", "Rear E1",         "Rear F1", "Rear A2",
    "Rear B2", "Rear C2", "Rear D2", "Rear E2", "Rear F2",         "Rear A3", "Rear B3",
    "Rear C3", "Rear D3", "Rear E3", "Rear F3", "Full Rear Touch",
};

static const MappingOption k_mapping_options[] = {
    {VITAKI_CTRL_OUT_OPTIONS}, {VITAKI_CTRL_OUT_SHARE}, {VITAKI_CTRL_OUT_TOUCHPAD},
    {VITAKI_CTRL_OUT_L1},      {VITAKI_CTRL_OUT_L2},    {VITAKI_CTRL_OUT_L3},
    {VITAKI_CTRL_OUT_R1},      {VITAKI_CTRL_OUT_R2},    {VITAKI_CTRL_OUT_R3},
    {VITAKI_CTRL_OUT_PS},      {VITAKI_CTRL_OUT_NONE},
};

static const VitakiCtrlIn k_shoulder_inputs[] = {VITAKI_CTRL_IN_L1, VITAKI_CTRL_IN_R1};

static inline int controller_front_index_from_row_col(int row, int col) {
  return row * VITAKI_FRONT_TOUCH_GRID_COLS + col;
}

static inline VitakiCtrlIn controller_front_input_from_index(int index) {
  return (VitakiCtrlIn)(VITAKI_CTRL_IN_FRONTTOUCH_GRID_START + index);
}

static void controller_front_selection_sync_diagram(void) {
  memcpy(ctrl_diagram.front_selection, ctrl_front_selection, sizeof(ctrl_front_selection));
  ctrl_diagram.front_selection_count = ctrl_front_selection_count;
}

static void controller_front_drag_reset_path(void) {
  ctrl_front_drag_path_len = 0;
}

static void controller_front_selection_clear(void) {
  memset(ctrl_front_selection, 0, sizeof(ctrl_front_selection));
  ctrl_front_selection_count = 0;
  controller_front_drag_reset_path();
  controller_front_selection_sync_diagram();
}

static void controller_front_selection_add_index(int index) {
  if (index < 0 || index >= FRONT_GRID_COUNT)
    return;
  if (!ctrl_front_selection[index]) {
    ctrl_front_selection[index] = true;
    ctrl_front_selection_count++;
    controller_front_selection_sync_diagram();
  }
}

static void controller_front_selection_remove_index(int index) {
  if (index < 0 || index >= FRONT_GRID_COUNT)
    return;
  if (ctrl_front_selection[index]) {
    ctrl_front_selection[index] = false;
    if (ctrl_front_selection_count > 0)
      ctrl_front_selection_count--;
    controller_front_selection_sync_diagram();
  }
}

static void controller_front_drag_visit_cell(int index) {
  if (index < 0 || index >= FRONT_GRID_COUNT)
    return;

  if (ctrl_front_drag_path_len > 0 && ctrl_front_drag_path[ctrl_front_drag_path_len - 1] == index)
    return;

  if (ctrl_front_drag_path_len > 1 && ctrl_front_drag_path[ctrl_front_drag_path_len - 2] == index) {
    int last = ctrl_front_drag_path[ctrl_front_drag_path_len - 1];
    controller_front_selection_remove_index(last);
    ctrl_front_drag_path_len--;
    return;
  }

  if (!ctrl_front_selection[index]) {
    controller_front_selection_add_index(index);
    if (ctrl_front_drag_path_len < FRONT_GRID_COUNT) {
      ctrl_front_drag_path[ctrl_front_drag_path_len++] = index;
    }
  }
}

static void controller_popup_update_scroll_for_selection(void) {
  int max_scroll = MAX(0, (int)MAPPING_OPTION_COUNT - POPUP_VISIBLE_OPTIONS);
  if (ctrl_popup_selection < 0)
    ctrl_popup_selection = 0;
  if (ctrl_popup_selection >= MAPPING_OPTION_COUNT)
    ctrl_popup_selection = MAPPING_OPTION_COUNT - 1;
  if (ctrl_popup_scroll > max_scroll)
    ctrl_popup_scroll = max_scroll;
  if (ctrl_popup_selection < ctrl_popup_scroll) {
    ctrl_popup_scroll = ctrl_popup_selection;
  } else if (ctrl_popup_selection >= ctrl_popup_scroll + POPUP_VISIBLE_OPTIONS) {
    ctrl_popup_scroll = ctrl_popup_selection - POPUP_VISIBLE_OPTIONS + 1;
  }
  if (ctrl_popup_scroll < 0)
    ctrl_popup_scroll = 0;
}

static void controller_popup_reset_scroll(void) {
  ctrl_popup_scroll = 0;
  controller_popup_update_scroll_for_selection();
}

static inline int controller_back_index_from_row_col(int row, int col) {
  return row * VITAKI_REAR_TOUCH_GRID_COLS + col;
}

static inline VitakiCtrlIn controller_back_input_from_index(int index) {
  return (VitakiCtrlIn)(VITAKI_CTRL_IN_REARTOUCH_GRID_START + index);
}

static void controller_front_screen_rect(int diagram_x, int diagram_y, int diagram_w, int diagram_h,
                                         int *out_x, int *out_y, int *out_w, int *out_h) {
  int sx = diagram_x + (int)((float)diagram_w * VITA_SCREEN_X_RATIO);
  int sy = diagram_y + (int)((float)diagram_h * VITA_SCREEN_Y_RATIO);
  int sw = (int)((float)diagram_w * VITA_SCREEN_W_RATIO);
  int sh = (int)((float)diagram_h * VITA_SCREEN_H_RATIO);
  if (sw < 1)
    sw = 1;
  if (sh < 1)
    sh = 1;
  if (out_x)
    *out_x = sx;
  if (out_y)
    *out_y = sy;
  if (out_w)
    *out_w = sw;
  if (out_h)
    *out_h = sh;
}

static int controller_front_cell_from_point(int diagram_x, int diagram_y, int diagram_w,
                                            int diagram_h, float point_x, float point_y) {
  int screen_x, screen_y, screen_w, screen_h;
  controller_front_screen_rect(diagram_x, diagram_y, diagram_w, diagram_h, &screen_x, &screen_y,
                               &screen_w, &screen_h);
  if (point_x < screen_x || point_x >= (float)(screen_x + screen_w) || point_y < screen_y ||
      point_y >= (float)(screen_y + screen_h)) {
    return -1;
  }

  float rel_x = (point_x - (float)screen_x) / (float)screen_w;
  float rel_y = (point_y - (float)screen_y) / (float)screen_h;

  int col = (int)(rel_x * VITAKI_FRONT_TOUCH_GRID_COLS);
  int row = (int)(rel_y * VITAKI_FRONT_TOUCH_GRID_ROWS);
  if (col < 0)
    col = 0;
  if (col >= VITAKI_FRONT_TOUCH_GRID_COLS)
    col = VITAKI_FRONT_TOUCH_GRID_COLS - 1;
  if (row < 0)
    row = 0;
  if (row >= VITAKI_FRONT_TOUCH_GRID_ROWS)
    row = VITAKI_FRONT_TOUCH_GRID_ROWS - 1;
  return controller_front_index_from_row_col(row, col);
}

static void controller_back_pad_rect(int diagram_x, int diagram_y, int diagram_w, int diagram_h,
                                     int *out_x, int *out_y, int *out_w, int *out_h) {
  int px = diagram_x + (int)((float)diagram_w * VITA_RTOUCH_X_RATIO);
  int py = diagram_y + (int)((float)diagram_h * VITA_RTOUCH_Y_RATIO);
  int pw = (int)((float)diagram_w * VITA_RTOUCH_W_RATIO);
  int ph = (int)((float)diagram_h * VITA_RTOUCH_H_RATIO);
  if (pw < 1)
    pw = 1;
  if (ph < 1)
    ph = 1;
  if (out_x)
    *out_x = px;
  if (out_y)
    *out_y = py;
  if (out_w)
    *out_w = pw;
  if (out_h)
    *out_h = ph;
}

static int controller_back_cell_from_point(int diagram_x, int diagram_y, int diagram_w,
                                           int diagram_h, float point_x, float point_y) {
  int pad_x, pad_y, pad_w, pad_h;
  controller_back_pad_rect(diagram_x, diagram_y, diagram_w, diagram_h, &pad_x, &pad_y, &pad_w,
                           &pad_h);
  if (point_x < pad_x || point_x >= (float)(pad_x + pad_w) || point_y < pad_y ||
      point_y >= (float)(pad_y + pad_h)) {
    return -1;
  }

  float rel_x = (point_x - (float)pad_x) / (float)pad_w;
  float rel_y = (point_y - (float)pad_y) / (float)pad_h;

  int col = (int)(rel_x * VITAKI_REAR_TOUCH_GRID_COLS);
  int row = (int)(rel_y * VITAKI_REAR_TOUCH_GRID_ROWS);
  if (col < 0)
    col = 0;
  if (col >= VITAKI_REAR_TOUCH_GRID_COLS)
    col = VITAKI_REAR_TOUCH_GRID_COLS - 1;
  if (row < 0)
    row = 0;
  if (row >= VITAKI_REAR_TOUCH_GRID_ROWS)
    row = VITAKI_REAR_TOUCH_GRID_ROWS - 1;
  return controller_back_index_from_row_col(row, col);
}

static int controller_back_selection_collect(VitakiCtrlIn *out_inputs, int max_count) {
  if (!out_inputs || max_count <= 0)
    return 0;
  int count = 0;
  for (int i = 0; i < BACK_GRID_COUNT && count < max_count; i++) {
    if (ctrl_back_selection[i]) {
      out_inputs[count++] = controller_back_input_from_index(i);
    }
  }
  return count;
}

static int controller_front_selection_collect(VitakiCtrlIn *out_inputs, int max_count) {
  if (!out_inputs || max_count <= 0)
    return 0;
  int count = 0;
  for (int i = 0; i < FRONT_GRID_COUNT && count < max_count; i++) {
    if (ctrl_front_selection[i]) {
      out_inputs[count++] = controller_front_input_from_index(i);
    }
  }
  return count;
}

static void controller_front_set_cursor_index(int index) {
  if (index < 0)
    index = 0;
  if (index >= FRONT_GRID_COUNT)
    index = FRONT_GRID_COUNT - 1;
  ctrl_front_cursor_index = index;
  ctrl_front_cursor_row = index / VITAKI_FRONT_TOUCH_GRID_COLS;
  ctrl_front_cursor_col = index % VITAKI_FRONT_TOUCH_GRID_COLS;
}

static void controller_front_move_cursor(int delta_row, int delta_col) {
  ctrl_front_cursor_row = (ctrl_front_cursor_row + delta_row + VITAKI_FRONT_TOUCH_GRID_ROWS) %
                          VITAKI_FRONT_TOUCH_GRID_ROWS;
  ctrl_front_cursor_col = (ctrl_front_cursor_col + delta_col + VITAKI_FRONT_TOUCH_GRID_COLS) %
                          VITAKI_FRONT_TOUCH_GRID_COLS;
  controller_front_set_cursor_index(
      controller_front_index_from_row_col(ctrl_front_cursor_row, ctrl_front_cursor_col));
}

static void controller_back_selection_sync_diagram(void) {
  memcpy(ctrl_diagram.back_selection, ctrl_back_selection, sizeof(ctrl_back_selection));
  ctrl_diagram.back_selection_count = ctrl_back_selection_count;
}

static void controller_back_drag_reset_path(void) {
  ctrl_back_drag_path_len = 0;
}

static void controller_back_selection_clear(void) {
  memset(ctrl_back_selection, 0, sizeof(ctrl_back_selection));
  ctrl_back_selection_count = 0;
  controller_back_drag_reset_path();
  controller_back_selection_sync_diagram();
}

static void controller_back_selection_add_index(int index) {
  if (index < 0 || index >= BACK_GRID_COUNT)
    return;
  if (!ctrl_back_selection[index]) {
    ctrl_back_selection[index] = true;
    ctrl_back_selection_count++;
    controller_back_selection_sync_diagram();
  }
}

static void controller_back_selection_remove_index(int index) {
  if (index < 0 || index >= BACK_GRID_COUNT)
    return;
  if (ctrl_back_selection[index]) {
    ctrl_back_selection[index] = false;
    if (ctrl_back_selection_count > 0)
      ctrl_back_selection_count--;
    controller_back_selection_sync_diagram();
  }
}

static void controller_back_drag_visit_cell(int index) {
  if (index < 0 || index >= BACK_GRID_COUNT)
    return;

  if (ctrl_back_drag_path_len > 0 && ctrl_back_drag_path[ctrl_back_drag_path_len - 1] == index)
    return;

  if (ctrl_back_drag_path_len > 1 && ctrl_back_drag_path[ctrl_back_drag_path_len - 2] == index) {
    int last = ctrl_back_drag_path[ctrl_back_drag_path_len - 1];
    controller_back_selection_remove_index(last);
    ctrl_back_drag_path_len--;
    return;
  }

  if (!ctrl_back_selection[index]) {
    controller_back_selection_add_index(index);
    if (ctrl_back_drag_path_len < BACK_GRID_COUNT) {
      ctrl_back_drag_path[ctrl_back_drag_path_len++] = index;
    }
  }
}

static void controller_back_set_cursor_index(int index) {
  if (index < 0)
    index = 0;
  if (index >= BACK_GRID_COUNT)
    index = BACK_GRID_COUNT - 1;
  ctrl_back_cursor_index = index;
  ctrl_back_cursor_row = index / VITAKI_REAR_TOUCH_GRID_COLS;
  ctrl_back_cursor_col = index % VITAKI_REAR_TOUCH_GRID_COLS;
}

static void controller_back_move_cursor(int delta_row, int delta_col) {
  ctrl_back_cursor_row = (ctrl_back_cursor_row + delta_row + VITAKI_REAR_TOUCH_GRID_ROWS) %
                         VITAKI_REAR_TOUCH_GRID_ROWS;
  ctrl_back_cursor_col = (ctrl_back_cursor_col + delta_col + VITAKI_REAR_TOUCH_GRID_COLS) %
                         VITAKI_REAR_TOUCH_GRID_COLS;
  controller_back_set_cursor_index(
      controller_back_index_from_row_col(ctrl_back_cursor_row, ctrl_back_cursor_col));
}

static int find_preset_index_for_map(VitakiControllerMapId map_id) {
  for (int i = 0; i < CTRL_PRESET_COUNT; i++) {
    if (g_controller_presets[i].map_id == map_id) {
      return i;
    }
  }
  return 0;
}

// Convert custom map ID to slot index (0, 1, or 2)
static int custom_slot_for_map_id(VitakiControllerMapId map_id) {
  switch (map_id) {
    case VITAKI_CONTROLLER_MAP_CUSTOM_1:
      return 0;
    case VITAKI_CONTROLLER_MAP_CUSTOM_2:
      return 1;
    case VITAKI_CONTROLLER_MAP_CUSTOM_3:
      return 2;
    default:
      return 0;  // Default to slot 0
  }
}

static int controller_layout_center_x(void) {
  float nav_width = ui_nav_get_current_width();
  if (nav_width < 0.0f) {
    nav_width = 0.0f;
  }
  float available_width = (float)VITA_WIDTH - nav_width;
  if (available_width <= 0.0f) {
    nav_width = 0.0f;
    available_width = (float)VITA_WIDTH;
  }
  return (int)(nav_width + available_width * 0.5f);
}

static void controller_compute_diagram_rect(ControllerDetailView detail_view, int *out_x,
                                            int *out_y, int *out_w, int *out_h) {
  float nav_width_f = ui_nav_get_current_width();
  if (nav_width_f < 0.0f) {
    nav_width_f = 0.0f;
  }

  int nav_width = (int)nav_width_f;
  int available_width = VITA_WIDTH - nav_width;
  if (available_width <= 0) {
    nav_width = 0;
    available_width = VITA_WIDTH;
  }

  const int horizontal_padding = 40;
  int usable_width = MAX(available_width - horizontal_padding, 0);

  int diagram_w;
  int diagram_h;
  int diagram_y;

  int target_width = MIN(720, usable_width);
  if (target_width <= 0) {
    target_width = MIN(available_width, 720);
  }

  if (detail_view == CTRL_DETAIL_SUMMARY) {
    diagram_w = target_width;
    diagram_h = 330;
    diagram_y = CONTENT_START_Y + 60;
  } else {
    diagram_w = target_width;
    diagram_h = 330;
    diagram_y = CONTENT_START_Y + 60;
  }

  if (diagram_w > available_width) {
    diagram_w = available_width;
  }

  int diagram_x = nav_width + (available_width - diagram_w) / 2;

  if (out_x)
    *out_x = diagram_x;
  if (out_y)
    *out_y = diagram_y;
  if (out_w)
    *out_w = diagram_w;
  if (out_h)
    *out_h = diagram_h;
}

static ControllerViewMode callout_view_for_page(int page) {
  return (page == 1) ? CTRL_VIEW_BACK : CTRL_VIEW_FRONT;
}

static VitakiCtrlIn controller_summary_selected_shoulder_input(void) {
  int index = ctrl_summary_shoulder_index;
  if (index < 0 || index >= (int)(sizeof(k_shoulder_inputs) / sizeof(k_shoulder_inputs[0]))) {
    index = 0;
  }
  return k_shoulder_inputs[index];
}

static void controller_summary_sync_selection(void) {
  if (ctrl_diagram.detail_view != CTRL_DETAIL_SUMMARY) {
    return;
  }
  if (callout_view_for_page(ctrl_diagram.callout_page) == CTRL_VIEW_FRONT) {
    ctrl_diagram.selected_button = controller_summary_selected_shoulder_input();
  } else {
    ctrl_diagram.selected_button = -1;
  }
  ctrl_diagram.selected_zone = -1;
}

static void controller_summary_select_shoulder(int delta) {
  int count = (int)(sizeof(k_shoulder_inputs) / sizeof(k_shoulder_inputs[0]));
  ctrl_summary_shoulder_index = (ctrl_summary_shoulder_index + delta + count) % count;
  controller_summary_sync_selection();
}

static bool controller_summary_try_open_shoulder_popup(float touch_x, float touch_y, int diagram_x,
                                                       int diagram_y, int diagram_w,
                                                       int diagram_h) {
  if (callout_view_for_page(ctrl_diagram.callout_page) != CTRL_VIEW_FRONT) {
    return false;
  }

  DiagramRenderCtx ctx = {0};
  ui_diagram_init_context(&ctx, diagram_x, diagram_y, diagram_w, diagram_h);

  int left_anchor_x = 0;
  int left_anchor_y = 0;
  int right_anchor_x = 0;
  int right_anchor_y = 0;
  bool has_left_anchor =
      ui_diagram_anchor_for_input(&ctx, VITAKI_CTRL_IN_L1, &left_anchor_x, &left_anchor_y);
  bool has_right_anchor =
      ui_diagram_anchor_for_input(&ctx, VITAKI_CTRL_IN_R1, &right_anchor_x, &right_anchor_y);
  if (!has_left_anchor || !has_right_anchor) {
    return false;
  }

  int box_w = MAX(44, (int)((float)diagram_w * 0.22f));
  int box_h = MAX(32, (int)((float)diagram_h * 0.18f));
  int box_half_w = box_w / 2;
  int box_half_h = box_h / 2;

  bool in_left = touch_x >= left_anchor_x - box_half_w && touch_x <= left_anchor_x + box_half_w &&
                 touch_y >= left_anchor_y - box_half_h && touch_y <= left_anchor_y + box_half_h;
  bool in_right = touch_x >= right_anchor_x - box_half_w &&
                  touch_x <= right_anchor_x + box_half_w &&
                  touch_y >= right_anchor_y - box_half_h && touch_y <= right_anchor_y + box_half_h;

  if (!in_left && !in_right) {
    return false;
  }

  if (in_left && in_right) {
    float left_dist = fabsf(touch_x - (float)left_anchor_x);
    float right_dist = fabsf(touch_x - (float)right_anchor_x);
    ctrl_summary_shoulder_index = (left_dist <= right_dist) ? 0 : 1;
  } else {
    ctrl_summary_shoulder_index = in_left ? 0 : 1;
  }

  controller_summary_sync_selection();
  open_mapping_popup_single(controller_summary_selected_shoulder_input(), true);
  return true;
}

static void controller_apply_preset(int preset_index) {
  if (preset_index < 0)
    preset_index = 0;
  if (preset_index >= CTRL_PRESET_COUNT)
    preset_index = CTRL_PRESET_COUNT - 1;
  ctrl_preset_index = preset_index;

  VitakiControllerMapId map_id = g_controller_presets[ctrl_preset_index].map_id;
  context.config.controller_map_id = map_id;

  // All presets are now custom slots - load from the appropriate slot
  int slot = custom_slot_for_map_id(map_id);
  if (context.config.custom_maps_valid[slot]) {
    // Apply the saved custom mapping
    controller_map_storage_apply(&context.config.custom_maps[slot], &ctrl_preview_map);
  } else {
    // Initialize with defaults for this slot
    controller_map_storage_set_defaults(&context.config.custom_maps[slot]);
    context.config.custom_maps_valid[slot] = true;
    controller_map_storage_apply(&context.config.custom_maps[slot], &ctrl_preview_map);
  }

  ui_diagram_set_preset(&ctrl_diagram, map_id);
  ctrl_diagram.map_id = map_id;
}

static void cycle_controller_preset(int delta) {
  int next = (ctrl_preset_index + delta + CTRL_PRESET_COUNT) % CTRL_PRESET_COUNT;
  controller_apply_preset(next);
}

static void change_callout_page(int delta) {
  if (ctrl_diagram.callout_page_count <= 0)
    return;
  ctrl_diagram.callout_page =
      (ctrl_diagram.callout_page + delta + ctrl_diagram.callout_page_count) %
      ctrl_diagram.callout_page_count;
  ctrl_diagram.mode = callout_view_for_page(ctrl_diagram.callout_page);
  controller_summary_sync_selection();
}

// Save current mapping changes to the active custom slot
static void save_current_mapping_to_slot(void) {
  VitakiControllerMapId map_id = g_controller_presets[ctrl_preset_index].map_id;
  int slot = custom_slot_for_map_id(map_id);

  // Save the current preview map to the appropriate custom slot
  controller_map_storage_from_vcmi(&context.config.custom_maps[slot], &ctrl_preview_map);
  context.config.custom_maps_valid[slot] = true;
}

static int find_mapping_option_index(VitakiCtrlOut output) {
  for (int i = 0; i < MAPPING_OPTION_COUNT; i++) {
    if (k_mapping_options[i].output == output) {
      return i;
    }
  }
  return 0;
}

static const char *controller_slot_label(VitakiCtrlIn input) {
  static char label_buf[32];
  if (vitaki_ctrl_in_is_front_grid(input)) {
    int row = vitaki_ctrl_in_front_grid_row(input);
    int col = vitaki_ctrl_in_front_grid_col(input);
    snprintf(label_buf, sizeof(label_buf), "Front %c%d", 'A' + col, row + 1);
    return label_buf;
  }
  switch (input) {
    case VITAKI_CTRL_IN_L1:
      return "Left Shoulder (L1)";
    case VITAKI_CTRL_IN_R1:
      return "Right Shoulder (R1)";
    case VITAKI_CTRL_IN_SELECT_START:
      return "Select + Start";
    case VITAKI_CTRL_IN_LEFT_SQUARE:
      return "Left + Square";
    case VITAKI_CTRL_IN_RIGHT_CIRCLE:
      return "Right + Circle";
    case VITAKI_CTRL_IN_FRONTTOUCH_ANY:
      return "Full Front Touch";
    case VITAKI_CTRL_IN_FRONTTOUCH_CENTER:
      return "Front Center";
    case VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC:
      return "Front Upper Left";
    case VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC:
      return "Front Upper Right";
    case VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC:
      return "Front Lower Left";
    case VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC:
      return "Front Lower Right";
    default:
      break;
  }
  for (int i = 0; i < BACK_SLOT_COUNT; i++) {
    if (k_back_touch_slots[i] == input) {
      return k_back_touch_labels[i];
    }
  }
  return "Mapping Slot";
}

static bool controller_is_shoulder_input(VitakiCtrlIn input) {
  return input == VITAKI_CTRL_IN_L1 || input == VITAKI_CTRL_IN_R1;
}

static const char *controller_popup_title_for_input(VitakiCtrlIn input, bool is_front) {
  if (controller_is_shoulder_input(input)) {
    return "Shoulder Mapping";
  }
  return is_front ? "Front Touch Mapping" : "Rear Touch Mapping";
}

// Get pointer to current custom slot's map storage
static ControllerMapStorage *get_current_custom_map(void) {
  VitakiControllerMapId map_id = g_controller_presets[ctrl_preset_index].map_id;
  int slot = custom_slot_for_map_id(map_id);
  return &context.config.custom_maps[slot];
}

static void controller_sync_trigger_assignments(void) {
  ControllerMapStorage *custom_map = get_current_custom_map();
  custom_map->in_l2 = VITAKI_CTRL_IN_NONE;
  custom_map->in_r2 = VITAKI_CTRL_IN_NONE;
  for (int i = 0; i < VITAKI_CTRL_IN_COUNT; i++) {
    VitakiCtrlOut output = custom_map->in_out_btn[i];
    if (output == VITAKI_CTRL_OUT_L2 && custom_map->in_l2 == VITAKI_CTRL_IN_NONE) {
      custom_map->in_l2 = i;
    } else if (output == VITAKI_CTRL_OUT_R2 && custom_map->in_r2 == VITAKI_CTRL_IN_NONE) {
      custom_map->in_r2 = i;
    }
  }
}

static void apply_mapping_change_multi(const VitakiCtrlIn *inputs, int count,
                                       VitakiCtrlOut output) {
  if (!inputs || count <= 0)
    return;

  ControllerMapStorage *custom_map = get_current_custom_map();
  VitakiControllerMapId map_id = g_controller_presets[ctrl_preset_index].map_id;
  int slot = custom_slot_for_map_id(map_id);

  for (int i = 0; i < count; i++) {
    VitakiCtrlIn input = inputs[i];
    if (input < 0 || input >= VITAKI_CTRL_IN_COUNT)
      continue;
    custom_map->in_out_btn[input] = output;
  }

  controller_sync_trigger_assignments();
  context.config.custom_maps_valid[slot] = true;
  controller_map_storage_apply(custom_map, &ctrl_preview_map);
  ctrl_diagram.map_id = map_id;
}

static inline void apply_mapping_change_single(VitakiCtrlIn input, VitakiCtrlOut output) {
  apply_mapping_change_multi(&input, 1, output);
}

static void controller_front_clear_all_mappings(void) {
  VitakiCtrlIn inputs[FRONT_GRID_COUNT + 1];
  int count = 0;
  for (int i = 0; i < FRONT_GRID_COUNT; i++) {
    inputs[count++] = controller_front_input_from_index(i);
  }
  inputs[count++] = VITAKI_CTRL_IN_FRONTTOUCH_ANY;
  apply_mapping_change_multi(inputs, count, VITAKI_CTRL_OUT_NONE);
}

static void controller_back_clear_all_mappings(void) {
  VitakiCtrlIn inputs[BACK_GRID_COUNT + 1];
  int count = 0;
  for (int i = 0; i < BACK_GRID_COUNT; i++) {
    inputs[count++] = controller_back_input_from_index(i);
  }
  inputs[count++] = VITAKI_CTRL_IN_REARTOUCH_ANY;
  apply_mapping_change_multi(inputs, count, VITAKI_CTRL_OUT_NONE);
}

static void open_mapping_popup_multi(const VitakiCtrlIn *inputs, int count, bool is_front) {
  if (!inputs || count <= 0)
    return;
  ctrl_popup_active = true;
  ctrl_popup_touch_down = false;
  ctrl_popup_touch_choice = -1;
  ctrl_popup_dragging = false;
  ctrl_popup_touch_initial_y = 0.0f;
  ctrl_popup_touch_last_y = 0.0f;
  ctrl_popup_drag_accum = 0.0f;
  ctrl_popup_front = is_front;
  ctrl_popup_input_count = MIN(count, VITAKI_CTRL_IN_COUNT);
  memcpy(ctrl_popup_inputs, inputs, ctrl_popup_input_count * sizeof(VitakiCtrlIn));
  ctrl_popup_input = ctrl_popup_inputs[0];

  VitakiCtrlOut first_output =
      controller_map_get_output_for_input(&ctrl_preview_map, ctrl_popup_inputs[0]);
  bool same_output = true;
  for (int i = 1; i < ctrl_popup_input_count; i++) {
    VitakiCtrlOut other =
        controller_map_get_output_for_input(&ctrl_preview_map, ctrl_popup_inputs[i]);
    if (other != first_output) {
      same_output = false;
      break;
    }
  }

  VitakiCtrlOut initial = same_output ? first_output : ctrl_last_mapping_output;
  if (initial == VITAKI_CTRL_OUT_NONE)
    initial = ctrl_last_mapping_output;
  ctrl_popup_selection = find_mapping_option_index(initial);
  controller_popup_reset_scroll();
}

static inline void open_mapping_popup_single(VitakiCtrlIn input, bool is_front) {
  open_mapping_popup_multi(&input, 1, is_front);
}

static void handle_mapping_popup_input(void) {
  if (!ctrl_popup_active)
    return;

  const int popup_w = 420;
  const int popup_h = 320;
  const int popup_x = (VITA_WIDTH - popup_w) / 2;
  const int popup_y = (VITA_HEIGHT - popup_h) / 2;
  const int option_y_start = popup_y + 110;
  const int option_row_height = 36;

  if (btn_pressed(SCE_CTRL_UP)) {
    ctrl_popup_selection = (ctrl_popup_selection - 1 + MAPPING_OPTION_COUNT) % MAPPING_OPTION_COUNT;
    controller_popup_update_scroll_for_selection();
  } else if (btn_pressed(SCE_CTRL_DOWN)) {
    ctrl_popup_selection = (ctrl_popup_selection + 1) % MAPPING_OPTION_COUNT;
    controller_popup_update_scroll_for_selection();
  } else if (btn_pressed(SCE_CTRL_CIRCLE)) {
    ctrl_popup_active = false;
    ctrl_popup_input_count = 0;
  } else if (btn_pressed(SCE_CTRL_CROSS)) {
    VitakiCtrlOut output = k_mapping_options[ctrl_popup_selection].output;
    apply_mapping_change_multi(ctrl_popup_inputs, ctrl_popup_input_count, output);
    ctrl_last_mapping_output = output;
    // BUG FIX: Persist mapping changes immediately
    ui_settings_persist_config();
    ctrl_popup_active = false;
    ctrl_popup_input_count = 0;
    ctrl_popup_touch_down = false;
    ctrl_popup_touch_choice = -1;
  }

  SceTouchData touch;
  sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
  if (touch.reportNum > 0) {
    float touch_x = (touch.report[0].x / (float)VITA_TOUCH_PANEL_WIDTH) * (float)VITA_WIDTH;
    float touch_y = (touch.report[0].y / (float)VITA_TOUCH_PANEL_HEIGHT) * (float)VITA_HEIGHT;
    bool inside_popup = touch_x >= popup_x && touch_x <= popup_x + popup_w && touch_y >= popup_y &&
                        touch_y <= popup_y + popup_h;
    if (inside_popup) {
      if (!ctrl_popup_touch_down) {
        ctrl_popup_touch_down = true;
        ctrl_popup_touch_initial_y = touch_y;
        ctrl_popup_touch_last_y = touch_y;
        ctrl_popup_drag_accum = 0.0f;
        ctrl_popup_dragging = false;
      }

      float delta_y = touch_y - ctrl_popup_touch_last_y;
      ctrl_popup_touch_last_y = touch_y;

      if (fabsf(delta_y) > 4.0f) {
        ctrl_popup_dragging = true;
        ctrl_popup_drag_accum += delta_y;
        int scroll_delta = (int)(ctrl_popup_drag_accum / option_row_height);
        if (scroll_delta != 0) {
          ctrl_popup_drag_accum -= scroll_delta * option_row_height;
          ctrl_popup_scroll -= scroll_delta;
          if (ctrl_popup_scroll < 0)
            ctrl_popup_scroll = 0;
          int max_scroll = MAX(0, (int)MAPPING_OPTION_COUNT - POPUP_VISIBLE_OPTIONS);
          if (ctrl_popup_scroll > max_scroll)
            ctrl_popup_scroll = max_scroll;
        }
      }

      if (!ctrl_popup_dragging && touch_y >= option_y_start &&
          touch_y <= option_y_start + POPUP_VISIBLE_OPTIONS * option_row_height) {
        int row = (int)((touch_y - option_y_start) / (float)option_row_height);
        int option_index = ctrl_popup_scroll + row;
        if (option_index >= 0 && option_index < MAPPING_OPTION_COUNT) {
          ctrl_popup_selection = option_index;
          controller_popup_update_scroll_for_selection();
          ctrl_popup_touch_choice = option_index;
        }
      }
    }
  } else if (ctrl_popup_touch_down) {
    ctrl_popup_touch_down = false;
    if (!ctrl_popup_dragging && ctrl_popup_touch_choice >= 0 &&
        ctrl_popup_touch_choice < MAPPING_OPTION_COUNT) {
      ctrl_popup_selection = ctrl_popup_touch_choice;
      controller_popup_update_scroll_for_selection();
      VitakiCtrlOut output = k_mapping_options[ctrl_popup_touch_choice].output;
      apply_mapping_change_multi(ctrl_popup_inputs, ctrl_popup_input_count, output);
      ctrl_last_mapping_output = output;
      // BUG FIX: Persist mapping changes immediately
      ui_settings_persist_config();
      ctrl_popup_active = false;
      ctrl_popup_input_count = 0;
    }
    ctrl_popup_touch_choice = -1;
    ctrl_popup_dragging = false;
    ctrl_popup_drag_accum = 0.0f;
  }
}

static void render_mapping_popup(void) {
  if (!ctrl_popup_active)
    return;

  vita2d_draw_rectangle(0, 0, VITA_WIDTH, VITA_HEIGHT, RGBA8(0, 0, 0, 140));

  int popup_w = 360;
  int popup_h = 340;
  int popup_x = (VITA_WIDTH - popup_w) / 2;
  int popup_y = (VITA_HEIGHT - popup_h) / 2;
  ui_draw_card_with_shadow(popup_x, popup_y, popup_w, popup_h, 12, UI_COLOR_CARD_BG);

  const char *title = controller_popup_title_for_input(ctrl_popup_input, ctrl_popup_front);
  ui_text_draw(font, popup_x + 20, popup_y + 40, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SUBHEADER, title);
  char selection_label[48];
  const char *slot_label = controller_slot_label(ctrl_popup_input);
  if (ctrl_popup_input_count > 1) {
    snprintf(selection_label, sizeof(selection_label), "%d Zones Selected", ctrl_popup_input_count);
    slot_label = selection_label;
  }
  ui_text_draw(font, popup_x + 20, popup_y + 70, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
               slot_label);

  int content_x = popup_x + 24;
  int content_y = popup_y + 115;
  int content_w = popup_w - 64;
  const float option_row_height = (float)POPUP_ROW_HEIGHT;
  int option_y_start = content_y + 20;
  int option_y = option_y_start;
  const float scroll_row_h = option_row_height;
  uint32_t panel_top = RGBA8(18, 20, 28, 230);
  uint32_t panel_bottom = RGBA8(30, 34, 44, 245);
  ui_draw_vertical_gradient_rect(content_x, content_y, content_w,
                                 POPUP_VISIBLE_OPTIONS * POPUP_ROW_HEIGHT + 40, panel_top,
                                 panel_bottom, 18);
  int visible = MIN(POPUP_VISIBLE_OPTIONS, MAPPING_OPTION_COUNT);
  for (int i = 0; i < visible; i++) {
    int option_index = ctrl_popup_scroll + i;
    if (option_index >= MAPPING_OPTION_COUNT)
      break;
    bool selected = (option_index == ctrl_popup_selection);
    uint32_t row_color = selected ? UI_COLOR_PRIMARY_BLUE : RGBA8(55, 55, 60, 200);
    uint32_t text_color = selected ? UI_COLOR_TEXT_PRIMARY : UI_COLOR_TEXT_SECONDARY;
    uint32_t base_color = selected ? RGBA8(90, 120, 220, 255) : RGBA8(60, 60, 70, 200);
    uint32_t glow_color = selected ? RGBA8(150, 190, 255, 220) : RGBA8(40, 40, 48, 220);
    ui_draw_rounded_rect(content_x + 12, option_y - 18, content_w - 24, 36, 16, glow_color);
    ui_draw_rounded_rect(content_x + 12, option_y - 18, content_w - 24, 36, 16, base_color);
    const char *option_name = controller_output_name(k_mapping_options[option_index].output);
    int label_w = ui_text_width(font, FONT_SIZE_SMALL, option_name);
    int label_x = content_x + (content_w - label_w) / 2;
    ui_text_draw(font, label_x, option_y + 2, text_color, FONT_SIZE_SMALL, option_name);
    option_y += POPUP_ROW_HEIGHT;
  }

  if (MAPPING_OPTION_COUNT > POPUP_VISIBLE_OPTIONS) {
    float content_h = (float)(POPUP_VISIBLE_OPTIONS * option_row_height);
    float thumb_h = content_h * ((float)POPUP_VISIBLE_OPTIONS / (float)MAPPING_OPTION_COUNT);
    if (thumb_h < 12.0f)
      thumb_h = 12.0f;
    float scroll_ratio =
        (float)ctrl_popup_scroll / (float)(MAPPING_OPTION_COUNT - POPUP_VISIBLE_OPTIONS);
    float thumb_y = option_y_start + (content_h - thumb_h) * scroll_ratio;
    int bar_x = content_x + content_w - 10;
    ui_draw_rounded_rect(bar_x, option_y_start, 3, (int)content_h, 2, RGBA8(60, 65, 80, 180));
    ui_draw_rounded_rect(bar_x - 1, (int)thumb_y, 5, (int)thumb_h, 2, RGBA8(150, 200, 255, 220));
  }

  int hint_y = popup_y + popup_h - 44;
  const float hint_icon_scale = 0.6f;
  int hint_spacing = 14;
  int hint_total_w = (int)((32 * hint_icon_scale) * 2 + hint_spacing + 100);
  int hint_start_x = popup_x + (popup_w - hint_total_w) / 2;
  if (symbol_ex && symbol_circle) {
    int icon_w = (int)(32 * hint_icon_scale);
    int icon_h = (int)(32 * hint_icon_scale);
    vita2d_draw_texture_scale(symbol_ex, hint_start_x, hint_y, hint_icon_scale, hint_icon_scale);
    ui_text_draw(font, hint_start_x + icon_w + 6, hint_y + icon_h - 4, UI_COLOR_TEXT_SECONDARY,
                 FONT_SIZE_SMALL, "Assign");
    int second_x = hint_start_x + icon_w + 70;
    vita2d_draw_texture_scale(symbol_circle, second_x, hint_y, hint_icon_scale, hint_icon_scale);
    ui_text_draw(font, second_x + icon_w + 6, hint_y + icon_h - 4, UI_COLOR_TEXT_SECONDARY,
                 FONT_SIZE_SMALL, "Cancel");
  } else {
    const char *fallback = "X Assign    O Cancel";
    int fallback_w = ui_text_width(font, FONT_SIZE_SMALL, fallback);
    int fallback_x = popup_x + (popup_w - fallback_w) / 2;
    ui_text_draw(font, fallback_x, hint_y + 8, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL, fallback);
  }
}

/**
 * Helper: Render legend panel showing current button mappings
 */
static void render_controller_legend(int preset_index, int scroll, int x, int y, int w, int h) {
  // Background card
  ui_draw_card_with_shadow(x, y, w, h, 8, UI_COLOR_CARD_BG);

  // Title
  ui_text_draw(font, x + 10, y + 25, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SUBHEADER, "Mappings");

  // Get preset info
  const ControllerPresetDef *preset = &g_controller_presets[preset_index];

  // Draw mapping entries (simplified for now - full implementation would parse map_id)
  int row_y = y + 50;
  int row_h = 28;

  const char *sample_mappings[][2] = {{"D-Pad", "D-Pad"},   {"Face Buttons", "Face Buttons"},
                                      {"L1", "L1"},         {"R1", "R1"},
                                      {"L2", "Rear Touch"}, {"R2", "Rear Touch"},
                                      {"L3", "L+Square"},   {"R3", "R+Circle"}};

  for (int i = 0; i < 8 && i < (h - 50) / row_h; i++) {
    uint32_t row_bg = (i % 2 == 0) ? RGBA8(45, 45, 50, 255) : RGBA8(50, 50, 55, 255);
    vita2d_draw_rectangle(x + 5, row_y - 16, w - 10, row_h, row_bg);

    ui_text_draw(font, x + 10, row_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                 sample_mappings[i][0]);
    ui_text_draw(font, x + w / 2 + 5, row_y, UI_COLOR_PRIMARY_BLUE, FONT_SIZE_SMALL,
                 sample_mappings[i][1]);

    row_y += row_h;
  }
}

/**
 * Main Controller Configuration screen with three-view system:
 * - Summary View (default): Large diagram with callout labels showing current mappings
 * - Front Mapping View: Interactive front view for button remapping
 * - Back Mapping View: Interactive rear touchpad zone mapping
 */
UIScreenType ui_screen_draw_controller(void) {
  if (!ctrl_initialized) {
    ui_diagram_init(&ctrl_diagram);
    ctrl_initialized = true;
    ctrl_diagram.detail_view = CTRL_DETAIL_SUMMARY;
    ctrl_diagram.callout_page = 0;
    ctrl_diagram.mode = callout_view_for_page(ctrl_diagram.callout_page);
    controller_front_set_cursor_index(0);
    controller_front_selection_clear();
    controller_back_set_cursor_index(0);
    controller_back_selection_clear();
    ctrl_preset_index = find_preset_index_for_map(context.config.controller_map_id);
    controller_apply_preset(ctrl_preset_index);
    controller_summary_sync_selection();
  }

  ui_nav_render();

  UIScreenType nav_screen;
  if (!ctrl_popup_active &&
      handle_global_nav_shortcuts(UI_SCREEN_TYPE_CONTROLLER, &nav_screen, true)) {
    return nav_screen;
  }

  if (ctrl_popup_active) {
    handle_mapping_popup_input();
  } else {
    // Only process controller screen input when not focused on nav bar
    // This prevents input leak when hovering over controller icon in nav
    if (ctrl_diagram.detail_view == CTRL_DETAIL_SUMMARY &&
        ui_focus_get_zone() != FOCUS_ZONE_NAV_BAR) {
      if (btn_pressed(SCE_CTRL_LEFT)) {
        cycle_controller_preset(-1);
        // BUG FIX: Persist preset selection immediately
        ui_settings_persist_config();
      } else if (btn_pressed(SCE_CTRL_RIGHT)) {
        cycle_controller_preset(1);
        // BUG FIX: Persist preset selection immediately
        ui_settings_persist_config();
      }
      if (btn_pressed(SCE_CTRL_LTRIGGER)) {
        change_callout_page(-1);
      } else if (btn_pressed(SCE_CTRL_RTRIGGER)) {
        change_callout_page(1);
      }
      if (callout_view_for_page(ctrl_diagram.callout_page) == CTRL_VIEW_FRONT) {
        if (btn_pressed(SCE_CTRL_UP)) {
          controller_summary_select_shoulder(-1);
        } else if (btn_pressed(SCE_CTRL_DOWN)) {
          controller_summary_select_shoulder(1);
        }
        if (btn_pressed(SCE_CTRL_CROSS)) {
          open_mapping_popup_single(controller_summary_selected_shoulder_input(), true);
        }
      }
      if (btn_pressed(SCE_CTRL_SQUARE)) {
        ControllerViewMode view = callout_view_for_page(ctrl_diagram.callout_page);
        if (view == CTRL_VIEW_BACK) {
          controller_back_clear_all_mappings();
        } else {
          controller_front_clear_all_mappings();
        }
        // BUG FIX: Persist mapping changes immediately
        ui_settings_persist_config();
      }
    }

    static uint64_t last_touch_frame = 0;
    static uint64_t current_frame = 0;
    current_frame++;

    int diagram_x, diagram_y, diagram_w, diagram_h;
    controller_compute_diagram_rect(ctrl_diagram.detail_view, &diagram_x, &diagram_y, &diagram_w,
                                    &diagram_h);

    SceTouchData touch_front;
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch_front, 1);
    if (touch_front.reportNum > 0 && ctrl_diagram.detail_view == CTRL_DETAIL_SUMMARY) {
      if (current_frame - last_touch_frame >= TOUCH_DEBOUNCE_FRAMES) {
        float touch_x =
            (touch_front.report[0].x / (float)VITA_TOUCH_PANEL_WIDTH) * (float)VITA_WIDTH;
        float touch_y =
            (touch_front.report[0].y / (float)VITA_TOUCH_PANEL_HEIGHT) * (float)VITA_HEIGHT;
        if (touch_x >= diagram_x && touch_x <= diagram_x + diagram_w && touch_y >= diagram_y &&
            touch_y <= diagram_y + diagram_h) {
          last_touch_frame = current_frame;
          if (controller_summary_try_open_shoulder_popup(touch_x, touch_y, diagram_x, diagram_y,
                                                         diagram_w, diagram_h)) {
            // Shoulder mapping handled by popup in summary view.
          } else if (callout_view_for_page(ctrl_diagram.callout_page) == CTRL_VIEW_BACK) {
            ctrl_diagram.detail_view = CTRL_DETAIL_BACK_MAPPING;
            ctrl_diagram.mode = CTRL_VIEW_BACK;
            controller_back_set_cursor_index(0);
            controller_back_selection_clear();
            ctrl_back_drag_active = false;
            ctrl_back_touch_active = false;
            ctrl_diagram.selected_zone = controller_back_input_from_index(ctrl_back_cursor_index);
          } else {
            ctrl_diagram.detail_view = CTRL_DETAIL_FRONT_MAPPING;
            ctrl_diagram.mode = CTRL_VIEW_FRONT;
            controller_front_set_cursor_index(0);
            controller_front_selection_clear();
            ctrl_diagram.selected_button = controller_front_input_from_index(0);
          }
        }
      }
    }

    if (btn_pressed(SCE_CTRL_CIRCLE) && ctrl_diagram.detail_view != CTRL_DETAIL_SUMMARY) {
      ctrl_diagram.detail_view = CTRL_DETAIL_SUMMARY;
      ctrl_diagram.mode = callout_view_for_page(ctrl_diagram.callout_page);
      controller_summary_sync_selection();
      ctrl_front_drag_active = false;
      controller_front_selection_clear();
      ctrl_back_drag_active = false;
      ctrl_back_touch_active = false;
      controller_back_selection_clear();
    }

    if (ctrl_diagram.detail_view == CTRL_DETAIL_FRONT_MAPPING) {
      if (touch_front.reportNum > 0) {
        float touch_x =
            (touch_front.report[0].x / (float)VITA_TOUCH_PANEL_WIDTH) * (float)VITA_WIDTH;
        float touch_y =
            (touch_front.report[0].y / (float)VITA_TOUCH_PANEL_HEIGHT) * (float)VITA_HEIGHT;
        if (!ctrl_front_touch_active) {
          ctrl_front_touch_active = true;
          controller_front_selection_clear();
          controller_front_drag_reset_path();
        }
        int cell_index = controller_front_cell_from_point(diagram_x, diagram_y, diagram_w,
                                                          diagram_h, touch_x, touch_y);
        if (cell_index >= 0) {
          controller_front_drag_visit_cell(cell_index);
          controller_front_set_cursor_index(cell_index);
        }
      } else if (ctrl_front_touch_active) {
        ctrl_front_touch_active = false;
        VitakiCtrlIn selection_inputs[FRONT_GRID_COUNT];
        int selection_count =
            controller_front_selection_collect(selection_inputs, FRONT_GRID_COUNT);
        controller_front_selection_clear();
        if (selection_count > 0) {
          open_mapping_popup_multi(selection_inputs, selection_count, true);
        }
      }

      if (btn_pressed(SCE_CTRL_RIGHT)) {
        controller_front_move_cursor(0, 1);
      } else if (btn_pressed(SCE_CTRL_LEFT)) {
        controller_front_move_cursor(0, -1);
      } else if (btn_pressed(SCE_CTRL_DOWN)) {
        controller_front_move_cursor(1, 0);
      } else if (btn_pressed(SCE_CTRL_UP)) {
        controller_front_move_cursor(-1, 0);
      }

      if (ctrl_front_drag_active && btn_down(SCE_CTRL_CROSS)) {
        controller_front_selection_add_index(ctrl_front_cursor_index);
      }

      ctrl_diagram.selected_button = controller_front_input_from_index(ctrl_front_cursor_index);

      if (btn_pressed(SCE_CTRL_TRIANGLE)) {
        open_mapping_popup_single(VITAKI_CTRL_IN_FRONTTOUCH_ANY, true);
      }

      if (btn_pressed(SCE_CTRL_SQUARE)) {
        controller_front_clear_all_mappings();
        // BUG FIX: Persist mapping changes immediately
        ui_settings_persist_config();
      }

      if (btn_pressed(SCE_CTRL_CROSS)) {
        ctrl_front_drag_active = true;
        controller_front_selection_clear();
        controller_front_drag_reset_path();
        controller_front_selection_add_index(ctrl_front_cursor_index);
        controller_front_drag_visit_cell(ctrl_front_cursor_index);
      } else if (ctrl_front_drag_active && btn_released(SCE_CTRL_CROSS)) {
        ctrl_front_drag_active = false;
        VitakiCtrlIn selection_inputs[FRONT_GRID_COUNT];
        int selection_count =
            controller_front_selection_collect(selection_inputs, FRONT_GRID_COUNT);
        controller_front_selection_clear();
        if (selection_count > 0) {
          open_mapping_popup_multi(selection_inputs, selection_count, true);
        }
      }
    } else if (ctrl_diagram.detail_view == CTRL_DETAIL_BACK_MAPPING) {
      if (touch_front.reportNum > 0) {
        float touch_x =
            (touch_front.report[0].x / (float)VITA_TOUCH_PANEL_WIDTH) * (float)VITA_WIDTH;
        float touch_y =
            (touch_front.report[0].y / (float)VITA_TOUCH_PANEL_HEIGHT) * (float)VITA_HEIGHT;
        if (!ctrl_back_touch_active) {
          ctrl_back_touch_active = true;
          controller_back_selection_clear();
          controller_back_drag_reset_path();
        }
        int cell_index = controller_back_cell_from_point(diagram_x, diagram_y, diagram_w, diagram_h,
                                                         touch_x, touch_y);
        if (cell_index >= 0) {
          controller_back_drag_visit_cell(cell_index);
          controller_back_set_cursor_index(cell_index);
        }
      } else if (ctrl_back_touch_active) {
        ctrl_back_touch_active = false;
        VitakiCtrlIn selection_inputs[BACK_GRID_COUNT];
        int selection_count = controller_back_selection_collect(selection_inputs, BACK_GRID_COUNT);
        controller_back_selection_clear();
        if (selection_count > 0) {
          open_mapping_popup_multi(selection_inputs, selection_count, false);
        }
      }

      if (btn_pressed(SCE_CTRL_RIGHT)) {
        controller_back_move_cursor(0, 1);
      } else if (btn_pressed(SCE_CTRL_LEFT)) {
        controller_back_move_cursor(0, -1);
      } else if (btn_pressed(SCE_CTRL_DOWN)) {
        controller_back_move_cursor(1, 0);
      } else if (btn_pressed(SCE_CTRL_UP)) {
        controller_back_move_cursor(-1, 0);
      }

      if (ctrl_back_drag_active && btn_down(SCE_CTRL_CROSS)) {
        controller_back_selection_add_index(ctrl_back_cursor_index);
      }

      ctrl_diagram.selected_zone = controller_back_input_from_index(ctrl_back_cursor_index);

      if (btn_pressed(SCE_CTRL_TRIANGLE)) {
        open_mapping_popup_single(VITAKI_CTRL_IN_REARTOUCH_ANY, false);
      }

      if (btn_pressed(SCE_CTRL_SQUARE)) {
        controller_back_clear_all_mappings();
        // BUG FIX: Persist mapping changes immediately
        ui_settings_persist_config();
      }

      if (btn_pressed(SCE_CTRL_CROSS)) {
        ctrl_back_drag_active = true;
        controller_back_selection_clear();
        controller_back_drag_reset_path();
        controller_back_selection_add_index(ctrl_back_cursor_index);
        controller_back_drag_visit_cell(ctrl_back_cursor_index);
      } else if (ctrl_back_drag_active && btn_released(SCE_CTRL_CROSS)) {
        ctrl_back_drag_active = false;
        VitakiCtrlIn selection_inputs[BACK_GRID_COUNT];
        int selection_count = controller_back_selection_collect(selection_inputs, BACK_GRID_COUNT);
        controller_back_selection_clear();
        if (selection_count > 0) {
          open_mapping_popup_multi(selection_inputs, selection_count, false);
        }
      }
    }
  }

  const char *view_name = "Summary";
  if (ctrl_diagram.detail_view == CTRL_DETAIL_FRONT_MAPPING) {
    view_name = "Front Mapping";
  } else if (ctrl_diagram.detail_view == CTRL_DETAIL_BACK_MAPPING) {
    view_name = "Back Mapping";
  }

  char title[64];
  snprintf(title, sizeof(title), "Controller: %s", view_name);
  int title_w = ui_text_width(font, FONT_SIZE_HEADER, title);
  int layout_center_x = controller_layout_center_x();
  int title_x = layout_center_x - title_w / 2;
  ui_text_draw(font, title_x, CONTENT_START_Y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_HEADER, title);

  char preset_text[64];
  snprintf(preset_text, sizeof(preset_text), "Preset: %s",
           g_controller_presets[ctrl_preset_index].name);
  int preset_w = ui_text_width(font, FONT_SIZE_SUBHEADER, preset_text);
  int preset_x = layout_center_x - preset_w / 2;
  ui_text_draw(font, preset_x, CONTENT_START_Y + 26, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SUBHEADER,
               preset_text);

  int diagram_x, diagram_y, diagram_w, diagram_h;
  controller_compute_diagram_rect(ctrl_diagram.detail_view, &diagram_x, &diagram_y, &diagram_w,
                                  &diagram_h);

  ui_diagram_update(&ctrl_diagram);
  ui_diagram_render(&ctrl_diagram, &ctrl_preview_map, diagram_x, diagram_y, diagram_w, diagram_h);

  if (ctrl_diagram.detail_view == CTRL_DETAIL_SUMMARY) {
    int desc_y = diagram_y + diagram_h + 15;
    int desc_w =
        ui_text_width(font, FONT_SIZE_SMALL, g_controller_presets[ctrl_preset_index].description);
    int desc_x = diagram_x + (diagram_w - desc_w) / 2;
    ui_text_draw(font, desc_x, desc_y, UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL,
                 g_controller_presets[ctrl_preset_index].description);

    const char *hint =
        "Left/Right: Change Preset | L/R: Scroll Callouts | Up/Down: Select L1/R1 | X: Edit "
        "Shoulder | Tap Diagram to Edit | Square: Clear View";
    int hint_w = ui_text_width(font, FONT_SIZE_SMALL, hint);
    int hint_x = layout_center_x - hint_w / 2;
    ui_text_draw(font, hint_x, VITA_HEIGHT - 20, UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL, hint);
  } else if (!ctrl_popup_active) {
    const char *hint =
        "Move: D-Pad | Hold X + Move: Select | Triangle: Full | Square: Clear View | Circle: Back";
    int hint_w = ui_text_width(font, FONT_SIZE_SMALL, hint);
    int hint_x = layout_center_x - hint_w / 2;
    ui_text_draw(font, hint_x, VITA_HEIGHT - 20, UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL, hint);
  }

  if (ctrl_popup_active) {
    render_mapping_popup();
  }

  return UI_SCREEN_TYPE_CONTROLLER;
}

/// Render the current frame of an active stream
/// @return whether the stream should keep rendering
bool ui_screen_draw_stream(void) {
  // Match ywnico: immediately return false, let video callback handle everything
  // UI loop will skip rendering when is_streaming is true
  if (context.stream.is_streaming)
    context.stream.is_streaming = false;
  return false;
}

/// Run the connect for the Connecting / Waking screen and draw it
/// Waits indefinitely for console to wake, then auto-transitions to streaming
/// @return the next screen to show
UIScreenType ui_screen_draw_waking(void) {
  if (!ui_connection_overlay_active()) {
    ui_state_set_waking_wait_for_stream_us(0);
    return UI_SCREEN_TYPE_MAIN;
  }

  // If we're in the wake stage, poll discovery state until the console is ready
  if (ui_connection_stage() == UI_CONNECTION_STAGE_WAKING && context.active_host) {
    bool ready =
        (context.active_host->type & REGISTERED) &&
        !(context.active_host->discovery_state &&
          context.active_host->discovery_state_snapshot == CHIAKI_DISCOVERY_HOST_STATE_STANDBY);

    if (ready && !context.stream.session_init) {
      if (takion_cooldown_gate_active()) {
        LOGD("Deferring stream start — network recovery cooldown active");
        // Per-frame call is safe: host_set_hint() (host_feedback.c) only writes
        // fields and reads a timestamp -- no logging, so it can't spam -- and the
        // duration is recomputed from next_stream_allowed_us every call, so the
        // countdown stays accurate while this per-frame poll keeps re-entering.
        // The screen is still drawn below, so Cancel works during the deferral.
        show_cooldown_hint(context.active_host);
      } else {
        LOGD("Console awake, preparing stream startup");
        ui_connection_set_stage(UI_CONNECTION_STAGE_CONNECTING);
        if (!start_connection_thread(context.active_host)) {
          ui_connection_cancel();
          return UI_SCREEN_TYPE_MAIN;
        }
        ui_state_set_waking_wait_for_stream_us(sceKernelGetProcessTimeWide());
      }
    }
  }

  if (ui_connecting_frame()) {
    LOGD("Connection cancelled by user");
    host_cancel_stream_request();
    ui_connection_cancel();
    return UI_SCREEN_TYPE_MAIN;
  }

  return UI_SCREEN_TYPE_WAKING;  // Continue showing waking screen
}

/// Draw the Reconnecting screen ("Optimizing Stream")
/// Shows during packet loss recovery; it has no input and cannot be cancelled
/// @return the next screen type
UIScreenType ui_screen_draw_reconnecting(void) {
  // Check if we should still be showing this screen
  if (!context.stream.reconnect_overlay_active)
    return UI_SCREEN_TYPE_MAIN;

  ui_reconnecting_frame();
  return UI_SCREEN_TYPE_RECONNECTING;
}

/// Draw the debug messages screen
/// @return whether the dialog should keep rendering
bool ui_screen_draw_messages(void) {
  vita2d_set_clear_color(RGBA8(0x00, 0x00, 0x00, 0xFF));
  context.ui_state.next_active_item = -1;

  // initialize mlog_line_offset
  if (!context.ui_state.mlog_last_update)
    context.ui_state.mlog_line_offset = -1;
  if (context.ui_state.mlog_last_update != context.mlog->last_update) {
    context.ui_state.mlog_last_update = context.mlog->last_update;
    context.ui_state.mlog_line_offset = -1;
  }

  int w = VITA_WIDTH;
  int h = VITA_HEIGHT;

  int left_margin = 12;
  int top_margin = 20;
  int bottom_margin = 20;
  int font_size = FONT_SIZE_SUBHEADER;
  int line_height = font_size + 2;

  // compute lines to print
  // TODO enable scrolling etc
  int max_lines = (h - top_margin - bottom_margin) / line_height;
  bool overflow = (context.mlog->lines > max_lines);

  int max_line_offset = 0;
  if (overflow) {
    max_line_offset = context.mlog->lines - max_lines + 1;
  } else {
    max_line_offset = 0;
    context.ui_state.mlog_line_offset = -1;
  }
  int line_offset = max_line_offset;

  // update line offset according to mlog_line_offset
  if (context.ui_state.mlog_line_offset >= 0) {
    if (context.ui_state.mlog_line_offset <= max_line_offset) {
      line_offset = context.ui_state.mlog_line_offset;
    }
  }

  int y = top_margin;
  int i_y = 0;
  if (overflow && (line_offset > 0)) {
    char note[100];
    if (line_offset == 1) {
      snprintf(note, 100, "<%d line above>", line_offset);
    } else {
      snprintf(note, 100, "<%d lines above>", line_offset);
    }
    ui_text_draw(font_mono, left_margin, y, COLOR_GRAY50, font_size, note);
    y += line_height;
    i_y++;
  }

  int j;
  for (j = line_offset; j < context.mlog->lines; j++) {
    if (i_y > max_lines - 1)
      break;
    if (overflow && (i_y == max_lines - 1)) {
      if (j < context.mlog->lines - 1)
        break;
    }
    ui_text_draw(font_mono, left_margin, y, COLOR_WHITE, font_size,
                 get_message_log_line(context.mlog, j));
    y += line_height;
    i_y++;
  }
  if (overflow && (j < context.mlog->lines - 1)) {
    char note[100];
    int lines_below = context.mlog->lines - j - 1;
    if (lines_below == 1) {
      snprintf(note, 100, "<%d line below>", lines_below);
    } else {
      snprintf(note, 100, "<%d lines below>", lines_below);
    }
    ui_text_draw(font_mono, left_margin, y, COLOR_GRAY50, font_size, note);
    y += line_height;
    i_y++;
  }

  if (btn_pressed(SCE_CTRL_UP)) {
    if (overflow) {
      int next_offset = line_offset - 1;

      if (next_offset == 1)
        next_offset = 0;
      if (next_offset == max_line_offset - 1)
        next_offset = max_line_offset - 2;

      if (next_offset < 0)
        next_offset = line_offset;
      context.ui_state.mlog_line_offset = next_offset;
    }
  }
  if (btn_pressed(SCE_CTRL_DOWN)) {
    if (overflow) {
      int next_offset = line_offset + 1;

      if (next_offset == max_line_offset - 1)
        next_offset = max_line_offset;
      if (next_offset == 1)
        next_offset = 2;

      if (next_offset > max_line_offset)
        next_offset = max_line_offset;
      context.ui_state.mlog_line_offset = next_offset;
    }
  }

  if (btn_pressed(SCE_CTRL_CANCEL)) {
    // TODO abort connection if connecting
    vita2d_set_clear_color(RGBA8(0x40, 0x40, 0x40, 0xFF));
    context.ui_state.next_active_item = UI_MAIN_WIDGET_MESSAGES_BTN;
    return false;
  }
  return true;
}

// ============================================================================
// Public API Implementations (wrappers for internal functions)
// ============================================================================

void ui_screens_init(void) {
  // Get touch input state pointers from ui_input module
  touch_block_active = ui_input_get_touch_block_active_ptr();
  touch_block_pending_clear = ui_input_get_touch_block_pending_clear_ptr();
}
