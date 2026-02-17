/**
 * @file ui_screens.c
 * @brief Screen rendering implementations for VitaRPS5
 *
 * All screen implementations extracted from ui.c (~2000 lines):
 * - Main menu with console cards
 * - Settings screen
 * - Profile & registration screen
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
#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <chiaki/base64.h>

#include "context.h"
#include "host.h"
#include "host_feedback.h"
#include "ui.h"
#include "util.h"
#include "video.h"
#include "ui/ui_screens.h"
#include "ui/ui_internal.h"
#include "ui/ui_components.h"
#include "ui/ui_focus.h"
#include "ui/ui_state.h"
#include "ui/ui_graphics.h"

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

// PIN entry state
static PinEntryState pin_entry_state = {0};
bool show_cursor = false;  // Used by PIN entry digit rendering (ui_components.c)
static uint32_t cursor_blink_timer = 0;
static bool pin_entry_initialized = false;

// Touch input state pointers (initialized in ui_screens_init)
static bool *touch_block_active = NULL;
static bool *touch_block_pending_clear = NULL;

// Gesture recognition state for tap vs. swipe disambiguation
#define TAP_SWIPE_THRESHOLD 25.0f

static bool  touch_is_down = false;
static bool  touch_is_swipe = false;
static float touch_start_x = 0.0f;
static float touch_start_y = 0.0f;
static int   touch_start_card_index = -1;
static bool  touch_start_was_add_btn = false;

// ============================================================================
// Forward declarations for helper functions
// ============================================================================


static void reset_pin_entry(void);
static void update_cursor_blink(void);
static bool is_pin_complete(void);
static uint32_t pin_to_number(void);
static UIScreenType handle_vitarps5_touch_input(int num_hosts);
static inline void open_mapping_popup_single(VitakiCtrlIn input, bool is_front);
static void persist_config_or_warn(void);
static bool request_host_wakeup_with_feedback(VitaChiakiHost *host, const char *reason, bool continue_on_failure);

static void persist_config_or_warn(void) {
  if (!config_serialize(&context.config)) {
    LOGE("Failed to persist config changes");
  }
}

static bool request_host_wakeup_with_feedback(VitaChiakiHost *host, const char *reason, bool continue_on_failure) {
  if (!host) {
    LOGE("Wake requested with null host (%s)", reason ? reason : "unknown");
    return false;
  }

  bool discovered = (host->type & DISCOVERED) && host->discovery_state;
  bool at_rest = discovered && host->discovery_state->state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY;
  bool registered = (host->type & REGISTERED) != 0;
  bool manual = (host->type & MANUALLY_ADDED) != 0;
  LOGD("Wake request (%s): host=%s flags(reg=%d disc=%d manual=%d rest=%d)",
       reason ? reason : "unknown",
       host->hostname ? host->hostname : "<null>",
       registered ? 1 : 0,
       discovered ? 1 : 0,
       manual ? 1 : 0,
       at_rest ? 1 : 0);

  if (host_wakeup(host) != 0) {
    if (continue_on_failure) {
      host_set_hint(host,
                    "Wake signal failed; attempting connection anyway.",
                    false,
                    WAKE_ERROR_HINT_DURATION_US);
    } else {
      host_set_hint(host,
                    "Wake signal failed. Check pairing and network.",
                    true,
                    WAKE_ERROR_HINT_DURATION_US);
    }
    LOGE("Wake request failed (%s): host=%s",
         reason ? reason : "unknown",
         host->hostname ? host->hostname : "<null>");
    return false;
  }

  return true;
}


// ============================================================================
// Touch Input Handler
// ============================================================================

static UIScreenType handle_vitarps5_touch_input(int num_hosts) {
  SceTouchData touch;
  sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

  if (context.ui_state.error_popup_active || context.ui_state.debug_menu_active) {
    return UI_SCREEN_TYPE_MAIN;
  }

  if (*touch_block_active) {
    if (touch.reportNum == 0) {
      *touch_block_active = false;
      *touch_block_pending_clear = false;
    } else {
      return UI_SCREEN_TYPE_MAIN;
    }
    // Also reset gesture state when touch block clears
    touch_is_down = false;
    touch_is_swipe = false;
    touch_start_card_index = -1;
    touch_start_was_add_btn = false;
    return UI_SCREEN_TYPE_MAIN;
  }

  // Precompute card layout (needed by all phases)
  int content_center_x = ui_get_dynamic_content_center_x();
  int visible = (num_hosts < CARDS_VISIBLE_MAX) ? num_hosts : CARDS_VISIBLE_MAX;
  int row_width = visible * CONSOLE_CARD_WIDTH + (visible - 1) * CARD_H_GAP;
  int h_start_x = content_center_x - (row_width / 2);
  int card_y = (VITA_HEIGHT / 2) - (CONSOLE_CARD_HEIGHT / 2);
  int stride = CONSOLE_CARD_WIDTH + CARD_H_GAP;
  int offset = ui_cards_get_scroll_offset();

  // ── Phase A: Touch-down (finger just made contact) ──
  if (touch.reportNum > 0 && !touch_is_down) {
    float touch_x = (touch.report[0].x / (float)VITA_TOUCH_PANEL_WIDTH) * (float)VITA_WIDTH;
    float touch_y_sc = (touch.report[0].y / (float)VITA_TOUCH_PANEL_HEIGHT) * (float)VITA_HEIGHT;

    // Nav bar fires immediately (unchanged)
    UIScreenType nav_touch_screen;
    if (nav_touch_hit(touch_x, touch_y_sc, &nav_touch_screen))
      return nav_touch_screen;

    touch_is_down = true;
    touch_is_swipe = false;
    touch_start_x = touch_x;
    touch_start_y = touch_y_sc;
    touch_start_card_index = -1;
    touch_start_was_add_btn = false;

    // Begin drag tracking for swipe-to-scroll
    ui_cards_drag_begin();

    // Check card hitboxes — record index but do NOT fire action
    if (num_hosts > 0) {
      for (int vi = 0; vi < visible && (offset + vi) < num_hosts; vi++) {
        int i = offset + vi;
        int card_x = h_start_x + vi * stride;
        if (is_point_in_rect(touch_x, touch_y_sc, card_x, card_y,
                             CONSOLE_CARD_WIDTH, CONSOLE_CARD_HEIGHT)) {
          touch_start_card_index = i;
          break;
        }
      }

      // Check "Add New" button
      if (button_add_new && touch_start_card_index < 0) {
        int btn_w = vita2d_texture_get_width(button_add_new);
        int btn_x = content_center_x - (btn_w / 2);
        int btn_y = card_y + CONSOLE_CARD_HEIGHT + 60;
        int btn_h = vita2d_texture_get_height(button_add_new);
        if (is_point_in_rect(touch_x, touch_y_sc, btn_x, btn_y, btn_w, btn_h)) {
          touch_start_was_add_btn = true;
        }
      }
    }

    return UI_SCREEN_TYPE_MAIN;
  }

  // ── Phase B: Finger moving (detect swipe) ──
  if (touch.reportNum > 0 && touch_is_down) {
    float touch_x = (touch.report[0].x / (float)VITA_TOUCH_PANEL_WIDTH) * (float)VITA_WIDTH;
    float touch_y_sc = (touch.report[0].y / (float)VITA_TOUCH_PANEL_HEIGHT) * (float)VITA_HEIGHT;

    if (!touch_is_swipe) {
      float dx = touch_x - touch_start_x;
      float dy = touch_y_sc - touch_start_y;
      if ((dx * dx + dy * dy) > (TAP_SWIPE_THRESHOLD * TAP_SWIPE_THRESHOLD)) {
        touch_is_swipe = true;
      }
    }

    // Feed drag offset to card carousel (drag left = positive = scroll right)
    if (touch_is_swipe) {
      float drag_dx = touch_start_x - touch_x;
      ui_cards_drag_update(drag_dx);
    }

    return UI_SCREEN_TYPE_MAIN;
  }

  // ── Phase C: Finger lifted (fire tap if not a swipe) ──
  if (touch.reportNum == 0 && touch_is_down) {
    bool was_swipe = touch_is_swipe;
    int card_idx = touch_start_card_index;
    bool was_add_btn = touch_start_was_add_btn;

    // Finish drag tracking (snap-scrolls on swipe, no-ops on tap)
    ui_cards_drag_end();

    // Reset state
    touch_is_down = false;
    touch_is_swipe = false;
    touch_start_card_index = -1;
    touch_start_was_add_btn = false;

    if (!was_swipe) {
      // Tap on a console card
      if (card_idx >= 0 && num_hosts > 0) {
        ui_cards_set_selected_index(card_idx);

        ConsoleCardInfo* card = ui_cards_get_selected_card();
        if (card && card->host) {
          context.active_host = card->host;

          if (takion_cooldown_gate_active()) {
            LOGD("Touch connect ignored — network recovery cooldown active");
            return UI_SCREEN_TYPE_MAIN;
          }

          bool discovered = (context.active_host->type & DISCOVERED) && (context.active_host->discovery_state);
          bool registered = context.active_host->type & REGISTERED;
          bool at_rest = discovered && context.active_host->discovery_state &&
                         context.active_host->discovery_state->state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY;

          if (!registered) {
            return UI_SCREEN_TYPE_REGISTER_HOST;
          } else if (at_rest) {
            LOGD("Touch wake gesture on dormant console");
            ui_connection_begin(UI_CONNECTION_STAGE_WAKING);
            if (request_host_wakeup_with_feedback(context.active_host, "touch-standby", false))
              return UI_SCREEN_TYPE_WAKING;
            ui_connection_cancel();
            return UI_SCREEN_TYPE_MAIN;
          } else if (registered) {
            ui_connection_begin(UI_CONNECTION_STAGE_CONNECTING);
            if (!start_connection_thread(context.active_host)) {
              ui_connection_cancel();
              return UI_SCREEN_TYPE_MAIN;
            }
            ui_state_set_waking_wait_for_stream_us(sceKernelGetProcessTimeWide());
            return UI_SCREEN_TYPE_WAKING;
          }
        }
      }

      // Tap on "Add New" button
      if (was_add_btn) {
        if (!context.discovery_enabled) {
          start_discovery(NULL, NULL);
        }
      }
    }

    return UI_SCREEN_TYPE_MAIN;
  }

  return UI_SCREEN_TYPE_MAIN;
}

static void main_menu_move_selection(int delta, int num_hosts) {
  if (!ui_focus_is_content() || num_hosts <= 0)
    return;
  int selected = ui_cards_get_selected_index();
  ui_cards_set_selected_index((selected + delta + num_hosts) % num_hosts);
  ui_cards_ensure_selected_visible();
}

static UIScreenType main_menu_activate_selected_card(void) {
  ConsoleCardInfo *card = ui_cards_get_selected_card();
  if (!card || !card->host)
    return UI_SCREEN_TYPE_MAIN;

  context.active_host = card->host;
  if (takion_cooldown_gate_active()) {
    LOGD("Ignoring connect request — network recovery cooldown active");
    return UI_SCREEN_TYPE_MAIN;
  }

  bool discovered = (context.active_host->type & DISCOVERED) && (context.active_host->discovery_state);
  bool registered = context.active_host->type & REGISTERED;
  bool added = context.active_host->type & MANUALLY_ADDED;
  bool at_rest = discovered && context.active_host->discovery_state &&
                 context.active_host->discovery_state->state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY;

  if (!registered)
    return UI_SCREEN_TYPE_REGISTER_HOST;
  if (at_rest) {
    LOGD("Waking dormant console...");
    ui_connection_begin(UI_CONNECTION_STAGE_WAKING);
    if (request_host_wakeup_with_feedback(context.active_host, "cross-standby", false))
      return UI_SCREEN_TYPE_WAKING;
    ui_connection_cancel();
    return UI_SCREEN_TYPE_MAIN;
  }

  if (added) {
    // Manual hosts may not have fresh discovery state; nudge wake before connect.
    request_host_wakeup_with_feedback(context.active_host, "cross-manual-preconnect", true);
  }

  ui_connection_begin(UI_CONNECTION_STAGE_CONNECTING);
  if (!start_connection_thread(context.active_host)) {
    ui_connection_cancel();
    return UI_SCREEN_TYPE_MAIN;
  }
  ui_state_set_waking_wait_for_stream_us(sceKernelGetProcessTimeWide());
  return UI_SCREEN_TYPE_WAKING;
}

static UIScreenType main_menu_repair_selected_card(void) {
  ConsoleCardInfo *card = ui_cards_get_selected_card();
  if (!card || !card->host)
    return UI_SCREEN_TYPE_MAIN;

  VitaChiakiHost *host = card->host;
  if (!(host->type & REGISTERED))
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
  persist_config_or_warn();
  LOGD("Registration data deleted for console: %s", host->hostname);

  context.active_host = host;
  return UI_SCREEN_TYPE_REGISTER_HOST;
}

UIScreenType ui_screen_draw_main(void) {
  // Update and render VitaRPS5 particle background
  ui_particles_update();
  ui_particles_render();

  UIScreenType nav_screen;
  if (handle_global_nav_shortcuts(UI_SCREEN_TYPE_MAIN, &nav_screen, true))
    return nav_screen;

  /* Render VitaRPS5 console cards instead of host tiles */
  ui_cards_render_grid();

  /* Get cached card count (fresh from render_grid call above) */
  int num_hosts = ui_cards_get_count();

  UIScreenType next_screen = UI_SCREEN_TYPE_MAIN;

  // === D-PAD NAVIGATION (moves between console cards in content area) ===
  // Note: Nav bar UP/DOWN is handled by ui_nav_handle_shortcuts() in handle_global_nav_shortcuts()

  if (btn_pressed(SCE_CTRL_LEFT) || btn_pressed(SCE_CTRL_UP))
    main_menu_move_selection(-1, num_hosts);
  else if (btn_pressed(SCE_CTRL_RIGHT) || btn_pressed(SCE_CTRL_DOWN))
    main_menu_move_selection(1, num_hosts);

  /* === X BUTTON (Activate/Select highlighted element) === */

  if (btn_pressed(SCE_CTRL_CROSS) && ui_focus_is_content() && num_hosts > 0)
    next_screen = main_menu_activate_selected_card();

  /* === OTHER BUTTONS === */

  /* Square: Re-pair selected console (unregister + register again) */
  if (btn_pressed(SCE_CTRL_SQUARE) && ui_focus_is_content() && num_hosts > 0)
    next_screen = main_menu_repair_selected_card();

  /* Handle touch screen input for VitaRPS5 UI */
  UIScreenType touch_screen = handle_vitarps5_touch_input(num_hosts);
  if (touch_screen != UI_SCREEN_TYPE_MAIN) {
    return touch_screen;
  }

  /* Start: Open/clear console filter */
  if (btn_pressed(SCE_CTRL_START) && ui_focus_is_content()) {
    ui_cards_open_filter();
  }

  /* Select button shows hints popup */
  if (btn_pressed(SCE_CTRL_SELECT)) {
    trigger_hints_popup("L/R: Browse | Cross: Connect | Start: Filter");
  }

  return next_screen;
}

// ============================================================================
// PHASE 2: SETTINGS SCREEN
// ============================================================================

typedef struct {
  int selected_item;
  int scroll_offset;
  bool dropdown_expanded;
  int dropdown_selected_option;
} SettingsState;

static SettingsState settings_state = {0};

// Settings scroll constants (item dimensions match original draw code)
#define SETTINGS_VISIBLE_ITEMS      7   // Max items fitting in content area (~420px / 60px per item)
#define SETTINGS_ITEM_HEIGHT        50  // Consistent with other UI item heights
#define SETTINGS_ITEM_SPACING       10  // Standard UI spacing
#define SETTINGS_STREAMING_ITEMS    UI_SETTINGS_STREAMING_ITEM_COUNT  // Streaming settings: 3 dropdowns + 10 toggles

// Shared toggle geometry for settings rows
#define SETTINGS_TOGGLE_X_OFFSET    70
#define SETTINGS_TOGGLE_WIDTH       60
#define SETTINGS_TOGGLE_HEIGHT      30

// Toggle animation IDs
#define SETTINGS_TOGGLE_ANIM_FORCE_30FPS            3
#define SETTINGS_TOGGLE_ANIM_AUTO_DISCOVERY         4
#define SETTINGS_TOGGLE_ANIM_SHOW_LATENCY           5
#define SETTINGS_TOGGLE_ANIM_FILL_SCREEN            6
#define SETTINGS_TOGGLE_ANIM_CLAMP_SOFT_RESTART     7
#define SETTINGS_TOGGLE_ANIM_SHOW_NETWORK_ALERTS    8
#define SETTINGS_TOGGLE_ANIM_SHOW_STREAM_EXIT_HINT  9
#define SETTINGS_TOGGLE_ANIM_SHOW_NAV_LABELS       10
#define SETTINGS_TOGGLE_ANIM_CIRCLE_BUTTON_CONFIRM 101
#define SETTINGS_TOGGLE_ANIM_SHOW_ONLY_PAIRED  11
#define SETTINGS_TOGGLE_ANIM_PS_BUTTON_DUAL_MODE  12

static void settings_update_scroll_for_selection(void) {
    int total_items = SETTINGS_STREAMING_ITEMS;
    int max_scroll = total_items - SETTINGS_VISIBLE_ITEMS;
    if (max_scroll < 0) max_scroll = 0;

    // Clamp scroll
    if (settings_state.scroll_offset > max_scroll)
        settings_state.scroll_offset = max_scroll;
    if (settings_state.scroll_offset < 0)
        settings_state.scroll_offset = 0;

    // Keep selection visible
    if (settings_state.selected_item < settings_state.scroll_offset) {
        settings_state.scroll_offset = settings_state.selected_item;
    } else if (settings_state.selected_item >= settings_state.scroll_offset + SETTINGS_VISIBLE_ITEMS) {
        settings_state.scroll_offset = settings_state.selected_item - SETTINGS_VISIBLE_ITEMS + 1;
    }
}

/// Get resolution string from ChiakiVideoResolutionPreset
static const char* get_resolution_string(ChiakiVideoResolutionPreset preset) {
  switch (preset) {
    case CHIAKI_VIDEO_RESOLUTION_PRESET_360p: return "360p";
    case CHIAKI_VIDEO_RESOLUTION_PRESET_540p: return "540p";
    case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
    case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p:
      // Legacy/unsupported values are shown as their effective Vita preset.
      return "540p";
    default: return "540p";
  }
}

/// Get FPS string from ChiakiVideoFPSPreset
static const char* get_fps_string(ChiakiVideoFPSPreset preset) {
  switch (preset) {
    case CHIAKI_VIDEO_FPS_PRESET_30: return "30 FPS";
    case CHIAKI_VIDEO_FPS_PRESET_60: return "60 FPS";
    default: return "60 FPS";
  }
}

static const char* get_latency_mode_string(VitaChiakiLatencyMode mode) {
  switch (mode) {
    case VITA_LATENCY_MODE_ULTRA_LOW: return "Ultra Low (≈1.2 Mbps)";
    case VITA_LATENCY_MODE_LOW: return "Low (≈1.8 Mbps)";
    case VITA_LATENCY_MODE_HIGH: return "High (≈3.2 Mbps)";
    case VITA_LATENCY_MODE_MAX: return "Max (≈3.8 Mbps)";
    case VITA_LATENCY_MODE_BALANCED:
    default:
      return "Balanced (≈2.6 Mbps)";
  }
}

static void apply_force_30fps_runtime(void) {
  if (!context.stream.session_init)
    return;
  uint32_t clamp = context.stream.negotiated_fps ? context.stream.negotiated_fps : 60;
  if (context.config.force_30fps && clamp > 30)
    clamp = 30;
  context.stream.target_fps = clamp;
  context.stream.pacing_accumulator = 0;
}

static void settings_toggle_bool(bool *value, int anim_index) {
  *value = !(*value);
  start_toggle_animation(anim_index, *value);
  persist_config_or_warn();
}

static void settings_activate_selected_item(void) {
  switch (settings_state.selected_item) {
    case UI_SETTINGS_ITEM_QUALITY_PRESET:
      switch (context.config.resolution) {
        case CHIAKI_VIDEO_RESOLUTION_PRESET_360p:
          context.config.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
          break;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_540p:
          context.config.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_360p;
          break;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p:
        case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
          context.config.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
          break;
        default:
          context.config.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_360p;
          break;
      }
      persist_config_or_warn();
      break;
    case UI_SETTINGS_ITEM_LATENCY_MODE:
      context.config.latency_mode = (context.config.latency_mode + 1) % VITA_LATENCY_MODE_COUNT;
      persist_config_or_warn();
      break;
    case UI_SETTINGS_ITEM_FPS_TARGET:
      context.config.fps = (context.config.fps == CHIAKI_VIDEO_FPS_PRESET_30) ? CHIAKI_VIDEO_FPS_PRESET_60 : CHIAKI_VIDEO_FPS_PRESET_30;
      persist_config_or_warn();
      break;
    case UI_SETTINGS_ITEM_FORCE_30_FPS:
      settings_toggle_bool(&context.config.force_30fps, SETTINGS_TOGGLE_ANIM_FORCE_30FPS);
      apply_force_30fps_runtime();
      break;
    case UI_SETTINGS_ITEM_AUTO_DISCOVERY:
      settings_toggle_bool(&context.config.auto_discovery, SETTINGS_TOGGLE_ANIM_AUTO_DISCOVERY);
      break;
    case UI_SETTINGS_ITEM_SHOW_LATENCY:
      settings_toggle_bool(&context.config.show_latency, SETTINGS_TOGGLE_ANIM_SHOW_LATENCY);
      break;
    case UI_SETTINGS_ITEM_SHOW_NETWORK_ALERTS:
      settings_toggle_bool(&context.config.show_network_indicator, SETTINGS_TOGGLE_ANIM_SHOW_NETWORK_ALERTS);
      if (!context.config.show_network_indicator)
        vitavideo_hide_poor_net_indicator();
      break;
    case UI_SETTINGS_ITEM_SHOW_STREAM_EXIT_HINT:
      settings_toggle_bool(&context.config.show_stream_exit_hint, SETTINGS_TOGGLE_ANIM_SHOW_STREAM_EXIT_HINT);
      break;
    case UI_SETTINGS_ITEM_CLAMP_SOFT_RESTART_BITRATE:
      settings_toggle_bool(&context.config.clamp_soft_restart_bitrate, SETTINGS_TOGGLE_ANIM_CLAMP_SOFT_RESTART);
      break;
    case UI_SETTINGS_ITEM_FILL_SCREEN:
      settings_toggle_bool(&context.config.stretch_video, SETTINGS_TOGGLE_ANIM_FILL_SCREEN);
      break;
    case UI_SETTINGS_ITEM_SHOW_NAV_LABELS:
      settings_toggle_bool(&context.config.show_nav_labels, SETTINGS_TOGGLE_ANIM_SHOW_NAV_LABELS);
      break;
    case UI_SETTINGS_ITEM_CIRCLE_BUTTON_CONFIRM:
      settings_toggle_bool(&context.config.circle_btn_confirm, SETTINGS_TOGGLE_ANIM_CIRCLE_BUTTON_CONFIRM);
      break;
    case UI_SETTINGS_ITEM_SHOW_ONLY_PAIRED:
      settings_toggle_bool(&context.config.show_only_paired, SETTINGS_TOGGLE_ANIM_SHOW_ONLY_PAIRED);
      ui_cards_update_cache(true);
      break;
    case UI_SETTINGS_ITEM_PS_BUTTON_DUAL_MODE:
      settings_toggle_bool(&context.config.ps_button_dual_mode, SETTINGS_TOGGLE_ANIM_PS_BUTTON_DUAL_MODE);
      break;
    default:
      break;
  }
}

/// Helper to draw a single settings item (toggle with label)
static void draw_settings_toggle_item(int x, int y, int w, int h, const char* label,
                                      int anim_index, bool value, bool selected) {
  draw_toggle_switch(x + w - SETTINGS_TOGGLE_X_OFFSET, y + (h - SETTINGS_TOGGLE_HEIGHT)/2,
                     SETTINGS_TOGGLE_WIDTH, SETTINGS_TOGGLE_HEIGHT,
                     get_toggle_animation_value(anim_index, value), selected);
  vita2d_font_draw_text(font, x + 15, y + h/2 + 6,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_BODY, label);
}

/// Draw Streaming Quality tab content with scrolling
static void draw_settings_streaming_tab(int content_x, int content_y, int content_w) {
  int item_h = SETTINGS_ITEM_HEIGHT;
  int item_spacing = SETTINGS_ITEM_SPACING;
  int item_stride = item_h + item_spacing;

  // Determine visible range (streaming tab always has SETTINGS_STREAMING_ITEMS)
  int total_items = SETTINGS_STREAMING_ITEMS;
  int first_visible = settings_state.scroll_offset;
  int last_visible = first_visible + SETTINGS_VISIBLE_ITEMS;
  if (last_visible > total_items) last_visible = total_items;

  // Draw only visible items
  static int last_invalid_settings_item = -1;
  for (int i = first_visible; i < last_visible; i++) {
    int y = content_y + (i - first_visible) * item_stride;
    bool selected = (settings_state.selected_item == i);

    switch (i) {
      case UI_SETTINGS_ITEM_QUALITY_PRESET:
        draw_dropdown(content_x, y, content_w, item_h, "Quality Preset",
                      get_resolution_string(context.config.resolution),
                      false, selected);
        break;
      case UI_SETTINGS_ITEM_LATENCY_MODE:
        draw_dropdown(content_x, y, content_w, item_h, "Latency Mode",
                      get_latency_mode_string(context.config.latency_mode),
                      false, selected);
        break;
      case UI_SETTINGS_ITEM_FPS_TARGET:
        draw_dropdown(content_x, y, content_w, item_h, "FPS Target",
                      get_fps_string(context.config.fps),
                      false, selected);
        break;
      case UI_SETTINGS_ITEM_FORCE_30_FPS:
        draw_settings_toggle_item(content_x, y, content_w, item_h,
                                  "Force 30 FPS Output", SETTINGS_TOGGLE_ANIM_FORCE_30FPS,
                                  context.config.force_30fps, selected);
        break;
      case UI_SETTINGS_ITEM_AUTO_DISCOVERY:
        draw_settings_toggle_item(content_x, y, content_w, item_h,
                                  "Auto Discovery", SETTINGS_TOGGLE_ANIM_AUTO_DISCOVERY,
                                  context.config.auto_discovery, selected);
        break;
      case UI_SETTINGS_ITEM_SHOW_LATENCY:
        draw_settings_toggle_item(content_x, y, content_w, item_h,
                                  "Show Latency", SETTINGS_TOGGLE_ANIM_SHOW_LATENCY,
                                  context.config.show_latency, selected);
        break;
      case UI_SETTINGS_ITEM_SHOW_NETWORK_ALERTS:
        draw_settings_toggle_item(content_x, y, content_w, item_h,
                                  "Show Network Alerts", SETTINGS_TOGGLE_ANIM_SHOW_NETWORK_ALERTS,
                                  context.config.show_network_indicator, selected);
        break;
      case UI_SETTINGS_ITEM_SHOW_STREAM_EXIT_HINT:
        draw_settings_toggle_item(content_x, y, content_w, item_h,
                                  "Show Exit Shortcut Hint", SETTINGS_TOGGLE_ANIM_SHOW_STREAM_EXIT_HINT,
                                  context.config.show_stream_exit_hint, selected);
        break;
      case UI_SETTINGS_ITEM_CLAMP_SOFT_RESTART_BITRATE:
        draw_settings_toggle_item(content_x, y, content_w, item_h,
                                  "Clamp Soft Restart Bitrate", SETTINGS_TOGGLE_ANIM_CLAMP_SOFT_RESTART,
                                  context.config.clamp_soft_restart_bitrate, selected);
        break;
      case UI_SETTINGS_ITEM_FILL_SCREEN:
        draw_settings_toggle_item(content_x, y, content_w, item_h,
                                  "Fill Screen", SETTINGS_TOGGLE_ANIM_FILL_SCREEN,
                                  context.config.stretch_video, selected);
        break;
      case UI_SETTINGS_ITEM_SHOW_NAV_LABELS:
        draw_settings_toggle_item(content_x, y, content_w, item_h,
                                  "Show Navigation Labels", SETTINGS_TOGGLE_ANIM_SHOW_NAV_LABELS,
                                  context.config.show_nav_labels, selected);
        break;
      case UI_SETTINGS_ITEM_CIRCLE_BUTTON_CONFIRM:
        draw_toggle_switch(content_x + content_w - SETTINGS_TOGGLE_X_OFFSET,
                           y + (item_h - SETTINGS_TOGGLE_HEIGHT)/2,
                           SETTINGS_TOGGLE_WIDTH, SETTINGS_TOGGLE_HEIGHT,
                           get_toggle_animation_value(SETTINGS_TOGGLE_ANIM_CIRCLE_BUTTON_CONFIRM,
                                                      context.config.circle_btn_confirm),
                           selected);
        vita2d_font_draw_text(font, content_x + 15, y + item_h/2 + 6,
                              UI_COLOR_TEXT_PRIMARY, FONT_SIZE_BODY, "Circle Button Confirm");
        break;
      case UI_SETTINGS_ITEM_SHOW_ONLY_PAIRED:
        draw_settings_toggle_item(content_x, y, content_w, item_h,
                                  "Show Only Paired", SETTINGS_TOGGLE_ANIM_SHOW_ONLY_PAIRED,
                                  context.config.show_only_paired, selected);
        break;
      case UI_SETTINGS_ITEM_PS_BUTTON_DUAL_MODE:
        draw_settings_toggle_item(content_x, y, content_w, item_h,
                                  "PS Button Dual Mode", SETTINGS_TOGGLE_ANIM_PS_BUTTON_DUAL_MODE,
                                  context.config.ps_button_dual_mode, selected);
        break;
      default:
        if (last_invalid_settings_item != i) {
          LOGD("Ignoring unsupported settings item index in renderer: %d", i);
          last_invalid_settings_item = i;
        }
        break;
    }
  }

  // Draw scroll indicator if content exceeds visible area
  if (total_items > SETTINGS_VISIBLE_ITEMS) {
    int bar_x = content_x + content_w + 8;
    int content_h = SETTINGS_VISIBLE_ITEMS * item_stride;
    int thumb_h = (content_h * SETTINGS_VISIBLE_ITEMS) / total_items;
    if (thumb_h < 20) thumb_h = 20;

    int max_scroll = total_items - SETTINGS_VISIBLE_ITEMS;
    int track_travel = content_h - thumb_h;
    int thumb_y = content_y;
    if (max_scroll > 0) {
      thumb_y = content_y + (track_travel * settings_state.scroll_offset) / max_scroll;
    }

    // Track background
    ui_draw_rounded_rect(bar_x, content_y, 4, content_h, 2, RGBA8(60, 65, 80, 180));
    // Thumb
    ui_draw_rounded_rect(bar_x, thumb_y, 4, thumb_h, 2, RGBA8(150, 200, 255, 220));
  }

}

/// Main Settings screen rendering function
/// @return next screen to display
UIScreenType ui_screen_draw_settings(void) {
  // Render particle background
  ui_particles_update();
  ui_particles_render();

  UIScreenType nav_screen;
  if (handle_global_nav_shortcuts(UI_SCREEN_TYPE_SETTINGS, &nav_screen, true))
    return nav_screen;

  // Main content area (nav is overlay - content centered on full screen)
  int content_w = 800;  // Fixed width for content
  int content_x = (VITA_WIDTH - content_w) / 2;  // Center on screen
  int content_y = 100;

  // Settings title (centered on full screen width)
  const char* title = "Streaming Settings";
  int title_width = vita2d_font_text_width(font, FONT_SIZE_HEADER, title);
  int title_x = (VITA_WIDTH - title_width) / 2;
  int min_title_x = NAV_PILL_X + NAV_PILL_WIDTH + 20;
  if (title_x < min_title_x) title_x = min_title_x;
  vita2d_font_draw_text(font, title_x, 50, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_HEADER, title);

  // Content area (no tabs needed - only one section)
  int tab_content_y = 90;
  int tab_content_w = content_w - 40;
  int tab_content_x = content_x + 20;

  draw_settings_streaming_tab(tab_content_x, tab_content_y, tab_content_w);

  // Select button shows hints popup
  if (btn_pressed(SCE_CTRL_SELECT)) {
    trigger_hints_popup("Up/Down: Navigate | X: Toggle/Select | Circle: Back");
  }

  // === INPUT HANDLING ===

  int max_items = SETTINGS_STREAMING_ITEMS;

  // Up/Down: Navigate items (only when not in nav bar)
  if (!ui_focus_is_nav_bar()) {
    if (btn_pressed(SCE_CTRL_UP)) {
      settings_state.selected_item = (settings_state.selected_item - 1 + max_items) % max_items;
      settings_update_scroll_for_selection();
    } else if (btn_pressed(SCE_CTRL_DOWN)) {
      settings_state.selected_item = (settings_state.selected_item + 1) % max_items;
      settings_update_scroll_for_selection();
    }
  }

  // X: Activate selected item (toggle or cycle dropdown)
  if (btn_pressed(SCE_CTRL_CROSS) && !ui_focus_is_nav_bar())
    settings_activate_selected_item();

  // Circle: Back to main menu
  if (btn_pressed(SCE_CTRL_CIRCLE)) {
    return UI_SCREEN_TYPE_MAIN;
  }

  return UI_SCREEN_TYPE_SETTINGS;
}

// ============================================================================
// PHASE 2: PROFILE & REGISTRATION SCREEN
// ============================================================================

typedef enum {
  PROFILE_SECTION_INFO = 0,
  PROFILE_SECTION_CONNECTION = 1,
  PROFILE_SECTION_COUNT = 2
} ProfileSection;

typedef struct {
  ProfileSection current_section;
  bool editing_psn_id;
} ProfileState;

static ProfileState profile_state = {0};

static VitaChiakiHost* profile_get_reference_host(void) {
  if (context.active_host) {
    return context.active_host;
  }

  int selected = ui_cards_get_selected_index();
  int host_idx = 0;
  VitaChiakiHost *first_host = NULL;
  for (int i = 0; i < MAX_CONTEXT_HOSTS; i++) {
    VitaChiakiHost *host = context.hosts[i];
    if (!host) {
      continue;
    }
    if (!first_host) {
      first_host = host;
    }
    if (host_idx == selected) {
      return host;
    }
    host_idx++;
  }

  return first_host;
}

/// Draw profile card (left side)
static void draw_profile_card(int x, int y, int width, int height, bool selected) {
  uint32_t card_color = UI_COLOR_CARD_BG;
  ui_draw_card_with_shadow(x, y, width, height, 12, card_color);

  if (selected) {
    ui_draw_rounded_rect(x - 2, y - 2, width + 4, height + 4, 14, UI_COLOR_PRIMARY_BLUE);
    ui_draw_rounded_rect(x, y, width, height, 12, card_color);
  }

  int content_x = x + 20;
  int content_y = y + 30;

  // User icon (blue circular background with profile.png icon as placeholder)
  int icon_size = 50;
  int icon_x = content_x;
  int icon_y = content_y;
  ui_draw_circle(icon_x + icon_size/2, icon_y + icon_size/2, icon_size/2, UI_COLOR_PRIMARY_BLUE);

  // Profile icon (placeholder until PSN login retrieves actual user avatar)
  if (icon_profile) {
    int img_w = vita2d_texture_get_width(icon_profile);
    int img_h = vita2d_texture_get_height(icon_profile);
    float scale = (icon_size * 0.6f) / img_w;  // 60% of circle size
    int scaled_w = (int)(img_w * scale);
    int scaled_h = (int)(img_h * scale);
    int img_x = icon_x + (icon_size - scaled_w) / 2;
    int img_y = icon_y + (icon_size - scaled_h) / 2;
    vita2d_draw_texture_scale(icon_profile, img_x, img_y, scale, scale);
  }

  // PSN Account ID
  const char* psn_id = context.config.psn_account_id ? context.config.psn_account_id : "Not Set";
  vita2d_font_draw_text(font, content_x + icon_size + 20, content_y + 20,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SUBHEADER, psn_id);

  // PlayStation Network label
  vita2d_font_draw_text(font, content_x + icon_size + 20, content_y + 42,
                        UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL, "PlayStation Network");

  // Divider line
  vita2d_draw_rectangle(content_x, content_y + 70, width - 40, 1,
                        RGBA8(0x50, 0x50, 0x50, 255));

  if (selected) {
    vita2d_font_draw_text(font, content_x, y + height - 16,
                          UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL,
                          "Press X to refresh Account ID");
  }
}

/// Draw connection info card (right side) - two-column layout
static void draw_connection_info_card(int x, int y, int width, int height, bool selected) {
  uint32_t card_color = UI_COLOR_CARD_BG;
  ui_draw_card_with_shadow(x, y, width, height, 12, card_color);

  if (selected) {
    ui_draw_rounded_rect(x - 2, y - 2, width + 4, height + 4, 14, UI_COLOR_PRIMARY_BLUE);
    ui_draw_rounded_rect(x, y, width, height, 12, card_color);
  }

  int content_x = x + 15;
  int content_y = y + 25;
  int line_h = 20;
  int col2_x = content_x + 120;  // Value column

  // Title
  vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SUBHEADER,
                        "Connection Information");
  content_y += 30;

  VitaChiakiHost *host = profile_get_reference_host();
  bool has_host = (host != NULL);
  bool has_discovery = has_host && host->discovery_state;
  bool has_registered = has_host && host->registered_state;
  bool is_streaming = context.stream.is_streaming && context.stream.session_init;

  // Network Type
  const char* network_text = "Unavailable";
  if (has_discovery) {
    network_text = "Local Wi-Fi";
  } else if (has_host && (host->type & MANUALLY_ADDED)) {
    network_text = "Manual Host";
  }
  vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                        "Network Type");
  vita2d_font_draw_text(font, col2_x, content_y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SMALL,
                        network_text);
  content_y += line_h;

  // Console Name
  const char* console_name = "Not selected";
  if (has_discovery && host->discovery_state->host_name) {
    console_name = host->discovery_state->host_name;
  } else if (has_registered && host->registered_state->server_nickname) {
    console_name = host->registered_state->server_nickname;
  } else if (has_host && host->hostname) {
    console_name = host->hostname;
  }
  vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                        "Console");
  vita2d_font_draw_text(font, col2_x, content_y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SMALL,
                        console_name);
  content_y += line_h;

  // Console IP
  const char* console_ip = "N/A";
  if (has_discovery && host->discovery_state->host_addr) {
    console_ip = host->discovery_state->host_addr;
  } else if (has_host && host->hostname) {
    console_ip = host->hostname;
  }
  vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                        "Console IP");
  vita2d_font_draw_text(font, col2_x, content_y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SMALL,
                        console_ip);
  content_y += line_h;

  // Latency (if enabled)
  if (context.config.show_latency) {
    char latency_text[32] = "N/A";
    uint32_t latency_color = UI_COLOR_TEXT_PRIMARY;

    if (context.stream.session_init && context.stream.session.rtt_us > 0) {
      uint32_t latency_ms = (uint32_t)(context.stream.session.rtt_us / 1000);
      snprintf(latency_text, sizeof(latency_text), "%u ms", latency_ms);

      // Color code
      if (latency_ms < 30) {
        latency_color = RGBA8(0x4C, 0xAF, 0x50, 255);  // Green
      } else if (latency_ms < 60) {
        latency_color = RGBA8(0xFF, 0xB7, 0x4D, 255);  // Yellow
      } else {
        latency_color = RGBA8(0xF4, 0x43, 0x36, 255);  // Red
      }
    }

    vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                          "Latency");
    vita2d_font_draw_text(font, col2_x, content_y, latency_color, FONT_SIZE_SMALL,
                          latency_text);
    content_y += line_h;

    // Bitrate (measured)
    char bitrate_text[32] = "N/A";
    uint32_t bitrate_color = UI_COLOR_TEXT_PRIMARY;
    uint64_t now_us = sceKernelGetProcessTimeWide();
    bool metrics_recent = context.stream.metrics_last_update_us != 0 &&
                          (now_us - context.stream.metrics_last_update_us) <= 3000000ULL;
    if (metrics_recent && context.stream.measured_bitrate_mbps > 0.01f) {
      snprintf(bitrate_text, sizeof(bitrate_text), "%.2f Mbps",
               context.stream.measured_bitrate_mbps);
      if (context.stream.measured_bitrate_mbps <= 2.5f) {
        bitrate_color = RGBA8(0x4C, 0xAF, 0x50, 255);  // Green for safe range
      } else if (context.stream.measured_bitrate_mbps <= 3.5f) {
        bitrate_color = RGBA8(0xFF, 0xB7, 0x4D, 255);  // Yellow warning
      } else {
        bitrate_color = RGBA8(0xF4, 0x43, 0x36, 255);  // Red: likely too high
      }
    }
    vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                          "Bitrate");
    vita2d_font_draw_text(font, col2_x, content_y, bitrate_color, FONT_SIZE_SMALL,
                          bitrate_text);
    content_y += line_h;

    // Packet Loss
    char loss_text[48] = "Stable";
    uint32_t loss_color = UI_COLOR_TEXT_PRIMARY;
    bool loss_recent = context.stream.loss_alert_until_us &&
                       now_us < context.stream.loss_alert_until_us;
    if (context.stream.frame_loss_events > 0 ||
        context.stream.takion_drop_events > 0) {
      snprintf(loss_text, sizeof(loss_text), "%u events / %u frames",
               context.stream.takion_drop_events,
               context.stream.total_frames_lost);
      if (loss_recent) {
        loss_color = RGBA8(0xF4, 0x43, 0x36, 255);
      }
    }
    vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_SECONDARY,
                          FONT_SIZE_SMALL, "Packet Loss");
    vita2d_font_draw_text(font, col2_x, content_y, loss_color, FONT_SIZE_SMALL,
                          loss_text);
    content_y += line_h;
  }

  // Connection status
  const char* connection_text = is_streaming ? "Streaming" : (has_host ? "Ready" : "None");
  vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                        "Connection");
  vita2d_font_draw_text(font, col2_x, content_y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SMALL,
                        connection_text);
  content_y += line_h;

  // Remote Play status
  const char* remote_play = has_registered ? "Available" : "Unavailable";
  vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                        "Remote Play");
  vita2d_font_draw_text(font, col2_x, content_y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SMALL,
                        remote_play);
  content_y += line_h;

  // Quality Setting
  const char* quality_text = "540p";
  if (context.config.resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_360p) {
    quality_text = "360p";
  }
  vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                        "Quality Setting");
  vita2d_font_draw_text(font, col2_x, content_y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SMALL,
                        quality_text);
}

/// Draw PSN Authentication section (bottom) - modern design with status indicators
static void draw_registration_section(int x, int y, int width, int height, bool selected) {
  uint32_t card_color = UI_COLOR_CARD_BG;
  ui_draw_card_with_shadow(x, y, width, height, 12, card_color);

  if (selected) {
    ui_draw_rounded_rect(x - 2, y - 2, width + 4, height + 4, 14, UI_COLOR_PRIMARY_BLUE);
    ui_draw_rounded_rect(x, y, width, height, 12, card_color);
  }

  int content_x = x + 15;
  int content_y = y + 25;

  // Title
  vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SUBHEADER,
                        "PSN Authentication");
  content_y += 30;

  // Description text
  vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                        "Required for remote play on PS5 over local net.");
  content_y += 25;

  // Authentication status indicators
  int num_registered = context.config.num_registered_hosts;
  bool authenticated = num_registered > 0;

  // Status indicator 1: Not authenticated (red X) or authenticated (green checkmark)
  draw_status_dot(content_x, content_y - 3, 6, authenticated ? UI_STATUS_ACTIVE : UI_STATUS_ERROR);
  vita2d_font_draw_text(font, content_x + 15, content_y,
                        authenticated ? RGBA8(0x4C, 0xAF, 0x50, 255) : RGBA8(0xF4, 0x43, 0x36, 255),
                        FONT_SIZE_SMALL, authenticated ? "Authenticated" : "Not authenticated");
  content_y += 22;

  // "Add New" button
  int btn_w = 80;
  int btn_h = 30;
  int btn_x = content_x;
  int btn_y = content_y;

  uint32_t btn_color = selected ? UI_COLOR_PRIMARY_BLUE : RGBA8(0x50, 0x70, 0xA0, 255);
  ui_draw_rounded_rect(btn_x, btn_y, btn_w, btn_h, 6, btn_color);

  int text_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, "Add New");
  vita2d_font_draw_text(font, btn_x + (btn_w - text_w) / 2, btn_y + btn_h / 2 + 5,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SMALL, "Add New");

  // Hint if selected
  if (selected) {
    vita2d_font_draw_text(font, btn_x + btn_w + 15, btn_y + btn_h / 2 + 5,
                          UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL,
                          "Press X to register");
  }
}

/// Main Profile & Registration screen
/// @return next screen type to display
UIScreenType ui_screen_draw_profile(void) {
  // Render particle background
  ui_particles_update();
  ui_particles_render();

  UIScreenType nav_screen;
  if (handle_global_nav_shortcuts(UI_SCREEN_TYPE_PROFILE, &nav_screen, true))
    return nav_screen;

  // Main content area (nav is overlay - content centered on full screen)
  int content_w = 800;  // Fixed width for content
  int content_x = (VITA_WIDTH - content_w) / 2;  // Center on screen
  int content_y = CONTENT_START_Y;  // Use unified content start Y

  // Title (centered on full screen width)
  const char* title = "Profile & Connection";
  int title_width = vita2d_font_text_width(font, FONT_SIZE_HEADER, title);
  int title_x = (VITA_WIDTH - title_width) / 2;
  int min_title_x = NAV_PILL_X + NAV_PILL_WIDTH + 20;
  if (title_x < min_title_x) title_x = min_title_x;
  vita2d_font_draw_text(font, title_x, 50, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_HEADER, title);

  // Layout: Profile card (left), Connection info (right) - registration removed
  int card_spacing = 15;
  int card_w = (content_w - card_spacing) / 2;
  int card_h = 250;  // Taller cards since no bottom section

  // Profile card (left)
  draw_profile_card(content_x, content_y, card_w, card_h,
                    profile_state.current_section == PROFILE_SECTION_INFO);

  // Connection info card (right)
  draw_connection_info_card(content_x + card_w + card_spacing, content_y, card_w, card_h,
                             profile_state.current_section == PROFILE_SECTION_CONNECTION);

  // Select button shows hints popup
  if (btn_pressed(SCE_CTRL_SELECT)) {
    trigger_hints_popup("Left/Right: Switch Card | X: Refresh Account ID | Circle: Back");
  }

  UIScreenType next_screen = UI_SCREEN_TYPE_PROFILE;

  // === INPUT HANDLING ===

  // Left/Right: Navigate between Profile and Connection cards
  if (btn_pressed(SCE_CTRL_LEFT)) {
    profile_state.current_section = PROFILE_SECTION_INFO;
  } else if (btn_pressed(SCE_CTRL_RIGHT)) {
    profile_state.current_section = PROFILE_SECTION_CONNECTION;
  }

  if (btn_pressed(SCE_CTRL_CROSS) &&
      profile_state.current_section == PROFILE_SECTION_INFO) {
    if (ui_reload_psn_account_id()) {
      persist_config_or_warn();
      trigger_hints_popup("Account ID refreshed from system profile");
    } else {
      trigger_hints_popup("Could not refresh Account ID");
    }
  }

  // Circle: Back to main menu
  if (btn_pressed(SCE_CTRL_CIRCLE)) {
    next_screen = UI_SCREEN_TYPE_MAIN;
  }

  return next_screen;
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
};

static const VitakiCtrlIn k_back_touch_slots[] = {
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
    VITAKI_CTRL_IN_REARTOUCH_ANY,
};

static const char* k_back_touch_labels[] = {
    "Rear A1",
    "Rear B1",
    "Rear C1",
    "Rear D1",
    "Rear E1",
    "Rear F1",
    "Rear A2",
    "Rear B2",
    "Rear C2",
    "Rear D2",
    "Rear E2",
    "Rear F2",
    "Rear A3",
    "Rear B3",
    "Rear C3",
    "Rear D3",
    "Rear E3",
    "Rear F3",
    "Full Rear Touch",
};

static const MappingOption k_mapping_options[] = {
    { VITAKI_CTRL_OUT_OPTIONS },
    { VITAKI_CTRL_OUT_SHARE },
    { VITAKI_CTRL_OUT_TOUCHPAD },
    { VITAKI_CTRL_OUT_L1 },
    { VITAKI_CTRL_OUT_L2 },
    { VITAKI_CTRL_OUT_L3 },
    { VITAKI_CTRL_OUT_R1 },
    { VITAKI_CTRL_OUT_R2 },
    { VITAKI_CTRL_OUT_R3 },
    { VITAKI_CTRL_OUT_PS },
    { VITAKI_CTRL_OUT_NONE },
};

static const VitakiCtrlIn k_shoulder_inputs[] = {
    VITAKI_CTRL_IN_L1,
    VITAKI_CTRL_IN_R1
};

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
                                         int* out_x, int* out_y, int* out_w, int* out_h) {
    int sx = diagram_x + (int)((float)diagram_w * VITA_SCREEN_X_RATIO);
    int sy = diagram_y + (int)((float)diagram_h * VITA_SCREEN_Y_RATIO);
    int sw = (int)((float)diagram_w * VITA_SCREEN_W_RATIO);
    int sh = (int)((float)diagram_h * VITA_SCREEN_H_RATIO);
    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;
    if (out_x) *out_x = sx;
    if (out_y) *out_y = sy;
    if (out_w) *out_w = sw;
    if (out_h) *out_h = sh;
}

static int controller_front_cell_from_point(int diagram_x, int diagram_y, int diagram_w, int diagram_h,
                                            float point_x, float point_y) {
    int screen_x, screen_y, screen_w, screen_h;
    controller_front_screen_rect(diagram_x, diagram_y, diagram_w, diagram_h,
                                 &screen_x, &screen_y, &screen_w, &screen_h);
    if (point_x < screen_x || point_x >= (float)(screen_x + screen_w) ||
        point_y < screen_y || point_y >= (float)(screen_y + screen_h)) {
        return -1;
    }

    float rel_x = (point_x - (float)screen_x) / (float)screen_w;
    float rel_y = (point_y - (float)screen_y) / (float)screen_h;

    int col = (int)(rel_x * VITAKI_FRONT_TOUCH_GRID_COLS);
    int row = (int)(rel_y * VITAKI_FRONT_TOUCH_GRID_ROWS);
    if (col < 0) col = 0;
    if (col >= VITAKI_FRONT_TOUCH_GRID_COLS) col = VITAKI_FRONT_TOUCH_GRID_COLS - 1;
    if (row < 0) row = 0;
    if (row >= VITAKI_FRONT_TOUCH_GRID_ROWS) row = VITAKI_FRONT_TOUCH_GRID_ROWS - 1;
    return controller_front_index_from_row_col(row, col);
}

static void controller_back_pad_rect(int diagram_x, int diagram_y, int diagram_w, int diagram_h,
                                     int* out_x, int* out_y, int* out_w, int* out_h) {
    int px = diagram_x + (int)((float)diagram_w * VITA_RTOUCH_X_RATIO);
    int py = diagram_y + (int)((float)diagram_h * VITA_RTOUCH_Y_RATIO);
    int pw = (int)((float)diagram_w * VITA_RTOUCH_W_RATIO);
    int ph = (int)((float)diagram_h * VITA_RTOUCH_H_RATIO);
    if (pw < 1) pw = 1;
    if (ph < 1) ph = 1;
    if (out_x) *out_x = px;
    if (out_y) *out_y = py;
    if (out_w) *out_w = pw;
    if (out_h) *out_h = ph;
}

static int controller_back_cell_from_point(int diagram_x, int diagram_y, int diagram_w, int diagram_h,
                                           float point_x, float point_y) {
    int pad_x, pad_y, pad_w, pad_h;
    controller_back_pad_rect(diagram_x, diagram_y, diagram_w, diagram_h, &pad_x, &pad_y, &pad_w, &pad_h);
    if (point_x < pad_x || point_x >= (float)(pad_x + pad_w) ||
        point_y < pad_y || point_y >= (float)(pad_y + pad_h)) {
        return -1;
    }

    float rel_x = (point_x - (float)pad_x) / (float)pad_w;
    float rel_y = (point_y - (float)pad_y) / (float)pad_h;

    int col = (int)(rel_x * VITAKI_REAR_TOUCH_GRID_COLS);
    int row = (int)(rel_y * VITAKI_REAR_TOUCH_GRID_ROWS);
    if (col < 0) col = 0;
    if (col >= VITAKI_REAR_TOUCH_GRID_COLS) col = VITAKI_REAR_TOUCH_GRID_COLS - 1;
    if (row < 0) row = 0;
    if (row >= VITAKI_REAR_TOUCH_GRID_ROWS) row = VITAKI_REAR_TOUCH_GRID_ROWS - 1;
    return controller_back_index_from_row_col(row, col);
}

static int controller_back_selection_collect(VitakiCtrlIn* out_inputs, int max_count) {
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

static int controller_front_selection_collect(VitakiCtrlIn* out_inputs, int max_count) {
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
    ctrl_front_cursor_row = (ctrl_front_cursor_row + delta_row + VITAKI_FRONT_TOUCH_GRID_ROWS) % VITAKI_FRONT_TOUCH_GRID_ROWS;
    ctrl_front_cursor_col = (ctrl_front_cursor_col + delta_col + VITAKI_FRONT_TOUCH_GRID_COLS) % VITAKI_FRONT_TOUCH_GRID_COLS;
    controller_front_set_cursor_index(controller_front_index_from_row_col(ctrl_front_cursor_row, ctrl_front_cursor_col));
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
    ctrl_back_cursor_row = (ctrl_back_cursor_row + delta_row + VITAKI_REAR_TOUCH_GRID_ROWS) % VITAKI_REAR_TOUCH_GRID_ROWS;
    ctrl_back_cursor_col = (ctrl_back_cursor_col + delta_col + VITAKI_REAR_TOUCH_GRID_COLS) % VITAKI_REAR_TOUCH_GRID_COLS;
    controller_back_set_cursor_index(controller_back_index_from_row_col(ctrl_back_cursor_row, ctrl_back_cursor_col));
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
        case VITAKI_CONTROLLER_MAP_CUSTOM_1: return 0;
        case VITAKI_CONTROLLER_MAP_CUSTOM_2: return 1;
        case VITAKI_CONTROLLER_MAP_CUSTOM_3: return 2;
        default: return 0;  // Default to slot 0
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

static void controller_compute_diagram_rect(ControllerDetailView detail_view,
                                            int* out_x, int* out_y,
                                            int* out_w, int* out_h) {
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

    if (out_x) *out_x = diagram_x;
    if (out_y) *out_y = diagram_y;
    if (out_w) *out_w = diagram_w;
    if (out_h) *out_h = diagram_h;
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

static bool controller_summary_try_open_shoulder_popup(float touch_x, float touch_y,
                                                       int diagram_x, int diagram_y,
                                                       int diagram_w, int diagram_h) {
    if (callout_view_for_page(ctrl_diagram.callout_page) != CTRL_VIEW_FRONT) {
        return false;
    }

    DiagramRenderCtx ctx = {0};
    ui_diagram_init_context(&ctx, diagram_x, diagram_y, diagram_w, diagram_h);

    int left_anchor_x = 0;
    int left_anchor_y = 0;
    int right_anchor_x = 0;
    int right_anchor_y = 0;
    bool has_left_anchor = ui_diagram_anchor_for_input(&ctx, VITAKI_CTRL_IN_L1, &left_anchor_x, &left_anchor_y);
    bool has_right_anchor = ui_diagram_anchor_for_input(&ctx, VITAKI_CTRL_IN_R1, &right_anchor_x, &right_anchor_y);
    if (!has_left_anchor || !has_right_anchor) {
        return false;
    }

    int box_w = MAX(44, (int)((float)diagram_w * 0.22f));
    int box_h = MAX(32, (int)((float)diagram_h * 0.18f));
    int box_half_w = box_w / 2;
    int box_half_h = box_h / 2;

    bool in_left = touch_x >= left_anchor_x - box_half_w && touch_x <= left_anchor_x + box_half_w &&
                   touch_y >= left_anchor_y - box_half_h && touch_y <= left_anchor_y + box_half_h;
    bool in_right = touch_x >= right_anchor_x - box_half_w && touch_x <= right_anchor_x + box_half_w &&
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
    ctrl_diagram.callout_page = (ctrl_diagram.callout_page + delta + ctrl_diagram.callout_page_count) % ctrl_diagram.callout_page_count;
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

static const char* controller_slot_label(VitakiCtrlIn input) {
    static char label_buf[32];
    if (vitaki_ctrl_in_is_front_grid(input)) {
        int row = vitaki_ctrl_in_front_grid_row(input);
        int col = vitaki_ctrl_in_front_grid_col(input);
        snprintf(label_buf, sizeof(label_buf), "Front %c%d", 'A' + col, row + 1);
        return label_buf;
    }
    switch (input) {
        case VITAKI_CTRL_IN_L1: return "Left Shoulder (L1)";
        case VITAKI_CTRL_IN_R1: return "Right Shoulder (R1)";
        case VITAKI_CTRL_IN_SELECT_START: return "Select + Start";
        case VITAKI_CTRL_IN_LEFT_SQUARE: return "Left + Square";
        case VITAKI_CTRL_IN_RIGHT_CIRCLE: return "Right + Circle";
        case VITAKI_CTRL_IN_FRONTTOUCH_ANY: return "Full Front Touch";
        case VITAKI_CTRL_IN_FRONTTOUCH_CENTER: return "Front Center";
        case VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC: return "Front Upper Left";
        case VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC: return "Front Upper Right";
        case VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC: return "Front Lower Left";
        case VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC: return "Front Lower Right";
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

static const char* controller_popup_title_for_input(VitakiCtrlIn input, bool is_front) {
    if (controller_is_shoulder_input(input)) {
        return "Shoulder Mapping";
    }
    return is_front ? "Front Touch Mapping" : "Rear Touch Mapping";
}

// Get pointer to current custom slot's map storage
static ControllerMapStorage* get_current_custom_map(void) {
    VitakiControllerMapId map_id = g_controller_presets[ctrl_preset_index].map_id;
    int slot = custom_slot_for_map_id(map_id);
    return &context.config.custom_maps[slot];
}

static void controller_sync_trigger_assignments(void) {
    ControllerMapStorage* custom_map = get_current_custom_map();
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

static void apply_mapping_change_multi(const VitakiCtrlIn* inputs, int count, VitakiCtrlOut output) {
    if (!inputs || count <= 0)
        return;

    ControllerMapStorage* custom_map = get_current_custom_map();
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

static void open_mapping_popup_multi(const VitakiCtrlIn* inputs, int count, bool is_front) {
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

    VitakiCtrlOut first_output = controller_map_get_output_for_input(&ctrl_preview_map, ctrl_popup_inputs[0]);
    bool same_output = true;
    for (int i = 1; i < ctrl_popup_input_count; i++) {
        VitakiCtrlOut other = controller_map_get_output_for_input(&ctrl_preview_map, ctrl_popup_inputs[i]);
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
        persist_config_or_warn();
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
        bool inside_popup = touch_x >= popup_x && touch_x <= popup_x + popup_w &&
                            touch_y >= popup_y && touch_y <= popup_y + popup_h;
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

            if (!ctrl_popup_dragging &&
                touch_y >= option_y_start && touch_y <= option_y_start + POPUP_VISIBLE_OPTIONS * option_row_height) {
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
        if (!ctrl_popup_dragging && ctrl_popup_touch_choice >= 0 && ctrl_popup_touch_choice < MAPPING_OPTION_COUNT) {
            ctrl_popup_selection = ctrl_popup_touch_choice;
            controller_popup_update_scroll_for_selection();
            VitakiCtrlOut output = k_mapping_options[ctrl_popup_touch_choice].output;
            apply_mapping_change_multi(ctrl_popup_inputs, ctrl_popup_input_count, output);
            ctrl_last_mapping_output = output;
            // BUG FIX: Persist mapping changes immediately
            persist_config_or_warn();
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

    const char* title = controller_popup_title_for_input(ctrl_popup_input, ctrl_popup_front);
    vita2d_font_draw_text(font, popup_x + 20, popup_y + 40, UI_COLOR_TEXT_PRIMARY,
                          FONT_SIZE_SUBHEADER, title);
    char selection_label[48];
    const char* slot_label = controller_slot_label(ctrl_popup_input);
    if (ctrl_popup_input_count > 1) {
        snprintf(selection_label, sizeof(selection_label), "%d Zones Selected", ctrl_popup_input_count);
        slot_label = selection_label;
    }
    vita2d_font_draw_text(font, popup_x + 20, popup_y + 70, UI_COLOR_TEXT_SECONDARY,
                          FONT_SIZE_SMALL, slot_label);

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
                                   POPUP_VISIBLE_OPTIONS * POPUP_ROW_HEIGHT + 40,
                                   panel_top, panel_bottom, 18);
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
        const char* option_name = controller_output_name(k_mapping_options[option_index].output);
        int label_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, option_name);
        int label_x = content_x + (content_w - label_w) / 2;
        vita2d_font_draw_text(font, label_x, option_y + 2, text_color, FONT_SIZE_SMALL, option_name);
        option_y += POPUP_ROW_HEIGHT;
    }

    if (MAPPING_OPTION_COUNT > POPUP_VISIBLE_OPTIONS) {
        float content_h = (float)(POPUP_VISIBLE_OPTIONS * option_row_height);
        float thumb_h = content_h * ((float)POPUP_VISIBLE_OPTIONS / (float)MAPPING_OPTION_COUNT);
        if (thumb_h < 12.0f)
            thumb_h = 12.0f;
        float scroll_ratio = (float)ctrl_popup_scroll / (float)(MAPPING_OPTION_COUNT - POPUP_VISIBLE_OPTIONS);
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
        vita2d_font_draw_text(font, hint_start_x + icon_w + 6, hint_y + icon_h - 4,
                              UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL, "Assign");
        int second_x = hint_start_x + icon_w + 70;
        vita2d_draw_texture_scale(symbol_circle, second_x, hint_y, hint_icon_scale, hint_icon_scale);
        vita2d_font_draw_text(font, second_x + icon_w + 6, hint_y + icon_h - 4,
                              UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL, "Cancel");
    } else {
        const char* fallback = "X Assign    O Cancel";
        int fallback_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, fallback);
        int fallback_x = popup_x + (popup_w - fallback_w) / 2;
        vita2d_font_draw_text(font, fallback_x, hint_y + 8, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL, fallback);
    }
}

/**
 * Helper: Render legend panel showing current button mappings
 */
static void render_controller_legend(int preset_index, int scroll, int x, int y, int w, int h) {
    // Background card
    ui_draw_card_with_shadow(x, y, w, h, 8, UI_COLOR_CARD_BG);

    // Title
    vita2d_font_draw_text(font, x + 10, y + 25, UI_COLOR_TEXT_PRIMARY,
                          FONT_SIZE_SUBHEADER, "Mappings");

    // Get preset info
    const ControllerPresetDef* preset = &g_controller_presets[preset_index];

    // Draw mapping entries (simplified for now - full implementation would parse map_id)
    int row_y = y + 50;
    int row_h = 28;

    const char* sample_mappings[][2] = {
        {"D-Pad", "D-Pad"},
        {"Face Buttons", "Face Buttons"},
        {"L1", "L1"},
        {"R1", "R1"},
        {"L2", "Rear Touch"},
        {"R2", "Rear Touch"},
        {"L3", "L+Square"},
        {"R3", "R+Circle"}
    };

    for (int i = 0; i < 8 && i < (h - 50) / row_h; i++) {
        uint32_t row_bg = (i % 2 == 0) ? RGBA8(45, 45, 50, 255) : RGBA8(50, 50, 55, 255);
        vita2d_draw_rectangle(x + 5, row_y - 16, w - 10, row_h, row_bg);

        vita2d_font_draw_text(font, x + 10, row_y, UI_COLOR_TEXT_SECONDARY,
                              FONT_SIZE_SMALL, sample_mappings[i][0]);
        vita2d_font_draw_text(font, x + w/2 + 5, row_y, UI_COLOR_PRIMARY_BLUE,
                              FONT_SIZE_SMALL, sample_mappings[i][1]);

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

    ui_particles_update();
    ui_particles_render();
    ui_nav_render();

    UIScreenType nav_screen;
    if (!ctrl_popup_active && handle_global_nav_shortcuts(UI_SCREEN_TYPE_CONTROLLER, &nav_screen, true)) {
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
                persist_config_or_warn();
            } else if (btn_pressed(SCE_CTRL_RIGHT)) {
                cycle_controller_preset(1);
                // BUG FIX: Persist preset selection immediately
                persist_config_or_warn();
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
                persist_config_or_warn();
            }
        }

        static uint64_t last_touch_frame = 0;
        static uint64_t current_frame = 0;
        current_frame++;

        int diagram_x, diagram_y, diagram_w, diagram_h;
        controller_compute_diagram_rect(ctrl_diagram.detail_view,
                                        &diagram_x, &diagram_y, &diagram_w, &diagram_h);

        SceTouchData touch_front;
        sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch_front, 1);
        if (touch_front.reportNum > 0 && ctrl_diagram.detail_view == CTRL_DETAIL_SUMMARY) {
            if (current_frame - last_touch_frame >= TOUCH_DEBOUNCE_FRAMES) {
                float touch_x = (touch_front.report[0].x / (float)VITA_TOUCH_PANEL_WIDTH) * (float)VITA_WIDTH;
                float touch_y = (touch_front.report[0].y / (float)VITA_TOUCH_PANEL_HEIGHT) * (float)VITA_HEIGHT;
                if (touch_x >= diagram_x && touch_x <= diagram_x + diagram_w &&
                    touch_y >= diagram_y && touch_y <= diagram_y + diagram_h) {
                    last_touch_frame = current_frame;
                    if (controller_summary_try_open_shoulder_popup(touch_x, touch_y,
                                                                   diagram_x, diagram_y,
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
                float touch_x = (touch_front.report[0].x / (float)VITA_TOUCH_PANEL_WIDTH) * (float)VITA_WIDTH;
                float touch_y = (touch_front.report[0].y / (float)VITA_TOUCH_PANEL_HEIGHT) * (float)VITA_HEIGHT;
                if (!ctrl_front_touch_active) {
                    ctrl_front_touch_active = true;
                    controller_front_selection_clear();
                    controller_front_drag_reset_path();
                }
                int cell_index = controller_front_cell_from_point(diagram_x, diagram_y, diagram_w, diagram_h,
                                                                  touch_x, touch_y);
                if (cell_index >= 0) {
                    controller_front_drag_visit_cell(cell_index);
                    controller_front_set_cursor_index(cell_index);
                }
            } else if (ctrl_front_touch_active) {
                ctrl_front_touch_active = false;
                VitakiCtrlIn selection_inputs[FRONT_GRID_COUNT];
                int selection_count = controller_front_selection_collect(selection_inputs, FRONT_GRID_COUNT);
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
                persist_config_or_warn();
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
                int selection_count = controller_front_selection_collect(selection_inputs, FRONT_GRID_COUNT);
                controller_front_selection_clear();
                if (selection_count > 0) {
                    open_mapping_popup_multi(selection_inputs, selection_count, true);
                }
            }
        } else if (ctrl_diagram.detail_view == CTRL_DETAIL_BACK_MAPPING) {
            if (touch_front.reportNum > 0) {
                float touch_x = (touch_front.report[0].x / (float)VITA_TOUCH_PANEL_WIDTH) * (float)VITA_WIDTH;
                float touch_y = (touch_front.report[0].y / (float)VITA_TOUCH_PANEL_HEIGHT) * (float)VITA_HEIGHT;
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
                persist_config_or_warn();
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

    const char* view_name = "Summary";
    if (ctrl_diagram.detail_view == CTRL_DETAIL_FRONT_MAPPING) {
        view_name = "Front Mapping";
    } else if (ctrl_diagram.detail_view == CTRL_DETAIL_BACK_MAPPING) {
        view_name = "Back Mapping";
    }

    char title[64];
    snprintf(title, sizeof(title), "Controller: %s", view_name);
    int title_w = vita2d_font_text_width(font, FONT_SIZE_HEADER, title);
    int layout_center_x = controller_layout_center_x();
    int title_x = layout_center_x - title_w / 2;
    vita2d_font_draw_text(font, title_x, CONTENT_START_Y,
                          UI_COLOR_TEXT_PRIMARY, FONT_SIZE_HEADER, title);

    char preset_text[64];
    snprintf(preset_text, sizeof(preset_text), "Preset: %s",
             g_controller_presets[ctrl_preset_index].name);
    int preset_w = vita2d_font_text_width(font, FONT_SIZE_SUBHEADER, preset_text);
    int preset_x = layout_center_x - preset_w / 2;
    vita2d_font_draw_text(font, preset_x, CONTENT_START_Y + 26,
                          UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SUBHEADER, preset_text);

    int diagram_x, diagram_y, diagram_w, diagram_h;
    controller_compute_diagram_rect(ctrl_diagram.detail_view,
                                    &diagram_x, &diagram_y, &diagram_w, &diagram_h);

    ui_diagram_update(&ctrl_diagram);
    ui_diagram_render(&ctrl_diagram, &ctrl_preview_map, diagram_x, diagram_y, diagram_w, diagram_h);

    if (ctrl_diagram.detail_view == CTRL_DETAIL_SUMMARY) {
        int desc_y = diagram_y + diagram_h + 15;
        int desc_w = vita2d_font_text_width(font, FONT_SIZE_SMALL,
                                            g_controller_presets[ctrl_preset_index].description);
        int desc_x = diagram_x + (diagram_w - desc_w) / 2;
        vita2d_font_draw_text(font, desc_x, desc_y,
            UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL,
            g_controller_presets[ctrl_preset_index].description);

        const char* hint = "Left/Right: Change Preset | L/R: Scroll Callouts | Up/Down: Select L1/R1 | X: Edit Shoulder | Tap Diagram to Edit | Square: Clear View";
        int hint_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, hint);
        int hint_x = layout_center_x - hint_w / 2;
        vita2d_font_draw_text(font, hint_x, VITA_HEIGHT - 20,
                              UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL, hint);
    } else if (!ctrl_popup_active) {
        const char* hint = "Move: D-Pad | Hold X + Move: Select | Triangle: Full | Square: Clear View | Circle: Back";
        int hint_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, hint);
        int hint_x = layout_center_x - hint_w / 2;
        vita2d_font_draw_text(font, hint_x, VITA_HEIGHT - 20,
                              UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL, hint);
    }

    if (ctrl_popup_active) {
        render_mapping_popup();
    }

    return UI_SCREEN_TYPE_CONTROLLER;
}


// VitaRPS5-style PIN entry constants
#define PIN_DIGIT_COUNT 8
#define PIN_DIGIT_WIDTH 60
#define PIN_DIGIT_HEIGHT 70
#define PIN_DIGIT_SPACING 10
#define PIN_CARD_WIDTH 700
#define PIN_CARD_HEIGHT 450

/// Helper: Reset PIN entry state
void reset_pin_entry() {
  for (int i = 0; i < PIN_DIGIT_COUNT; i++) {
    pin_entry_state.pin_digits[i] = 10;  // 10 = empty
  }
  pin_entry_state.current_digit = 0;
  pin_entry_state.pin_complete = false;
  pin_entry_state.complete_pin = 0;
  
  // Get touch input state pointers from ui_input module
  touch_block_active = ui_input_get_touch_block_active_ptr();
  touch_block_pending_clear = ui_input_get_touch_block_pending_clear_ptr();
  show_cursor = true;
  cursor_blink_timer = 0;
  pin_entry_initialized = true;
}

/// Helper: Update cursor blink animation
void update_cursor_blink() {
  cursor_blink_timer++;
  if (cursor_blink_timer >= 30) {  // ~0.5 second at 60fps
    show_cursor = !show_cursor;
    cursor_blink_timer = 0;
  }
}

/// Helper: Check if PIN is complete
bool is_pin_complete() {
  for (int i = 0; i < PIN_DIGIT_COUNT; i++) {
    if (pin_entry_state.pin_digits[i] > 9) {
      return false;
    }
  }
  return true;
}

/// Helper: Convert PIN digits to number
uint32_t pin_to_number() {
  uint32_t pin = 0;
  for (int i = 0; i < PIN_DIGIT_COUNT; i++) {
    pin = pin * 10 + pin_entry_state.pin_digits[i];
  }
  return pin;
}

// render_pin_digit() moved to ui_components.c

/// Draw VitaRPS5-style PIN entry registration screen
/// @return whether the dialog should keep rendering
bool ui_screen_draw_registration(void) {
  // Initialize PIN entry on first render
  if (!pin_entry_initialized) {
    reset_pin_entry();
  }

  // Update cursor blink
  update_cursor_blink();

  // Card centered on screen
  int card_x = (VITA_WIDTH - PIN_CARD_WIDTH) / 2;
  int card_y = (VITA_HEIGHT - PIN_CARD_HEIGHT) / 2;

  ui_draw_card_with_shadow(card_x, card_y, PIN_CARD_WIDTH, PIN_CARD_HEIGHT, 12, UI_COLOR_CARD_BG);

  // Title
  vita2d_font_draw_text(font, card_x + 20, card_y + 50, UI_COLOR_TEXT_PRIMARY, 28,
                        "PS5 Console Registration");

  // Console info (name and IP)
  if (context.active_host) {
    char console_info[128];
    const char* console_name = "Unknown Console";
    const char* host_ip = NULL;

    // Get console name from discovery or registered state
    if (context.active_host->discovery_state && context.active_host->discovery_state->host_name) {
      console_name = context.active_host->discovery_state->host_name;
    } else if (context.active_host->registered_state && context.active_host->registered_state->server_nickname) {
      console_name = context.active_host->registered_state->server_nickname;
    } else if (context.active_host->hostname) {
      console_name = context.active_host->hostname;
    }

    // Get IP from discovery or registered state
    if (context.active_host->discovery_state && context.active_host->discovery_state->host_addr) {
      host_ip = context.active_host->discovery_state->host_addr;
    } else if (context.active_host->registered_state && context.active_host->registered_state->ap_ssid) {
      host_ip = context.active_host->registered_state->ap_ssid;
    }

    if (host_ip) {
      snprintf(console_info, sizeof(console_info), "%s (%s)", console_name, host_ip);
    } else {
      snprintf(console_info, sizeof(console_info), "%s", console_name);
    }
    vita2d_font_draw_text(font, card_x + 20, card_y + 100, UI_COLOR_TEXT_SECONDARY, 20, console_info);
  }

  // Instructions
  vita2d_font_draw_text(font, card_x + 20, card_y + 150, UI_COLOR_TEXT_PRIMARY, 20,
                        "Enter the 8-digit session PIN displayed on your PS5:");

  // PIN digit boxes (centered in card)
  int pin_total_width = (PIN_DIGIT_WIDTH * PIN_DIGIT_COUNT) + (PIN_DIGIT_SPACING * (PIN_DIGIT_COUNT - 1));
  int pin_start_x = card_x + (PIN_CARD_WIDTH - pin_total_width) / 2;
  int pin_y = card_y + 220;

  for (int i = 0; i < PIN_DIGIT_COUNT; i++) {
    int x = pin_start_x + i * (PIN_DIGIT_WIDTH + PIN_DIGIT_SPACING);
    bool is_current = (pin_entry_state.current_digit == i);
    bool has_value = (pin_entry_state.pin_digits[i] <= 9);
    render_pin_digit(x, pin_y, pin_entry_state.pin_digits[i], is_current, has_value);
  }

  // Navigation hints
  vita2d_font_draw_text(font, card_x + 20, card_y + PIN_CARD_HEIGHT - 50, UI_COLOR_TEXT_SECONDARY, 18,
                        "Left/Right: Move   Up/Down: Change digit   Cross: Confirm   Circle: Cancel");

  // Input handling
  if (btn_pressed(SCE_CTRL_LEFT)) {
    if (pin_entry_state.current_digit > 0) {
      pin_entry_state.current_digit--;
    }
  } else if (btn_pressed(SCE_CTRL_RIGHT)) {
    if (pin_entry_state.current_digit < PIN_DIGIT_COUNT - 1) {
      pin_entry_state.current_digit++;
    }
  } else if (btn_pressed(SCE_CTRL_UP)) {
    uint32_t* digit = &pin_entry_state.pin_digits[pin_entry_state.current_digit];
    if (*digit > 9) *digit = 0;
    else *digit = (*digit + 1) % 10;
  } else if (btn_pressed(SCE_CTRL_DOWN)) {
    uint32_t* digit = &pin_entry_state.pin_digits[pin_entry_state.current_digit];
    if (*digit > 9) *digit = 9;
    else *digit = (*digit + 9) % 10;
  } else if (btn_pressed(SCE_CTRL_SQUARE)) {
    // Clear current digit
    pin_entry_state.pin_digits[pin_entry_state.current_digit] = 10;
  } else if (btn_pressed(SCE_CTRL_CROSS)) {
    // Confirm PIN if complete
    if (is_pin_complete()) {
      uint32_t pin = pin_to_number();
      LOGD("User entered PIN: %08u", pin);
      host_register(context.active_host, pin);
      pin_entry_initialized = false;  // Reset for next time
      return false;
    }
  } else if (btn_pressed(SCE_CTRL_CIRCLE)) {
    // Cancel
    pin_entry_initialized = false;  // Reset for next time
    return false;
  }

  return true;
}

/// Render the current frame of an active stream
/// @return whether the stream should keep rendering
bool ui_screen_draw_stream(void) {
  // Match ywnico: immediately return false, let video callback handle everything
  // UI loop will skip rendering when is_streaming is true
  if (context.stream.is_streaming) context.stream.is_streaming = false;
  return false;
}


/// Draw the "Waking up console..." screen with spinner animation
/// Waits indefinitely for console to wake, then auto-transitions to streaming
/// @return the next screen to show
UIScreenType ui_screen_draw_waking(void) {
  if (!ui_connection_overlay_active()) {
    ui_state_set_waking_start_time_us(0);
    ui_state_set_waking_wait_for_stream_us(0);
    return UI_SCREEN_TYPE_MAIN;
  }

  // Initialize timer on first call
  if (ui_state_get_waking_start_time_us() == 0) {
    ui_state_set_waking_start_time_us(sceKernelGetProcessTimeLow() / 1000);  // Convert to milliseconds
  }

  // Get current time for animations
  uint32_t current_time = sceKernelGetProcessTimeLow() / 1000;

  // If we're in the wake stage, poll discovery state until the console is ready
  if (ui_connection_stage() == UI_CONNECTION_STAGE_WAKING && context.active_host) {
    bool ready = (context.active_host->type & REGISTERED) &&
                 !(context.active_host->discovery_state &&
                   context.active_host->discovery_state->state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY);

    if (ready && !context.stream.session_init) {
      if (takion_cooldown_gate_active()) {
        LOGD("Deferring stream start — network recovery cooldown active");
        return UI_SCREEN_TYPE_WAKING;
      }
      LOGD("Console awake, preparing stream startup");
      ui_connection_set_stage(UI_CONNECTION_STAGE_CONNECTING);
      if (!start_connection_thread(context.active_host)) {
        ui_connection_cancel();
        return UI_SCREEN_TYPE_MAIN;
      }
      ui_state_set_waking_wait_for_stream_us(sceKernelGetProcessTimeWide());
    }
  }

  static const char *stage_titles[] = {
    "Waking console",
    "Preparing Remote Play",
    "Starting stream"
  };
  static const char *stage_details[] = {
    "Sending wake signal",
    "Negotiating session",
    "Launching video pipeline"
  };
  const int stage_count = sizeof(stage_titles) / sizeof(stage_titles[0]);
  int stage_index = 0;
  if (ui_connection_stage() >= UI_CONNECTION_STAGE_WAKING)
    stage_index = ui_connection_stage() - UI_CONNECTION_STAGE_WAKING;
  if (stage_index < 0)
    stage_index = 0;
  if (stage_index >= stage_count)
    stage_index = stage_count - 1;

  // Draw modern waking/connecting screen with polished UI
  vita2d_set_clear_color(UI_COLOR_BACKGROUND);

  // Card dimensions (slightly taller for spinner)
  int card_w = 640;
  int card_h = 360;
  int card_x = (VITA_WIDTH - card_w) / 2;
  int card_y = (VITA_HEIGHT - card_h) / 2;

  // Draw card with enhanced shadow (consistent with Phase 1 & 2 polish)
  ui_draw_card_with_shadow(card_x, card_y, card_w, card_h, 12, UI_COLOR_CARD_BG);

  // Draw PlayStation Blue accent borders (top and bottom)
  vita2d_draw_rectangle(card_x, card_y, card_w, 2, UI_COLOR_PRIMARY_BLUE);
  vita2d_draw_rectangle(card_x, card_y + card_h - 2, card_w, 2, UI_COLOR_PRIMARY_BLUE);

  // Title (using FONT_SIZE_HEADER would be 24, but we want slightly larger for importance)
  const char* title = (ui_connection_stage() == UI_CONNECTION_STAGE_WAKING) ?
      "Waking Console" : "Starting Remote Play";
  int title_size = 28;
  int title_w = get_text_width_cached(title, title_size);
  int title_x = card_x + (card_w - title_w) / 2;  // Center title
  vita2d_font_draw_text(font, title_x, card_y + 60, UI_COLOR_TEXT_PRIMARY, title_size, title);

  // Console name/IP info
  if (context.active_host && context.active_host->hostname) {
    char console_info[128];
    const char* console_name = context.active_host->hostname;

    // Try to get more specific name if available
    if (context.active_host->discovery_state && context.active_host->discovery_state->host_name) {
      console_name = context.active_host->discovery_state->host_name;
    }

    snprintf(console_info, sizeof(console_info), "%s", console_name);
    int info_w = vita2d_font_text_width(font, FONT_SIZE_BODY, console_info);
    int info_x = card_x + (card_w - info_w) / 2;  // Center info
    vita2d_font_draw_text(font, info_x, card_y + 95, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_BODY, console_info);
  }

  // Spinner animation (smooth rotation at 2 rotations per second)
  int spinner_cx = card_x + card_w / 2;
  int spinner_cy = card_y + card_h / 2 - 10;
  int spinner_radius = 40;
  int spinner_thickness = 6;
  float rotation = (float)((current_time * 720) % 360000) / 1000.0f;  // 2 rotations/sec
  ui_draw_spinner(spinner_cx, spinner_cy, spinner_radius, spinner_thickness, rotation, UI_COLOR_PRIMARY_BLUE);

  // Stage headline
  const char *stage_headline = stage_titles[stage_index];
  int stage_headline_size = 22;
  int stage_headline_w = vita2d_font_text_width(font, stage_headline_size, stage_headline);
  int stage_headline_x = card_x + (card_w - stage_headline_w) / 2;
  vita2d_font_draw_text(font, stage_headline_x, spinner_cy + spinner_radius + 50,
                        UI_COLOR_TEXT_PRIMARY, stage_headline_size, stage_headline);

  // Detail line
  const char *detail_text = stage_details[stage_index];
  int detail_w = vita2d_font_text_width(font, FONT_SIZE_BODY, detail_text);
  int detail_x = card_x + (card_w - detail_w) / 2;
  vita2d_font_draw_text(font, detail_x, spinner_cy + spinner_radius + 80,
                        UI_COLOR_TEXT_SECONDARY, FONT_SIZE_BODY, detail_text);

  // Cancel hint at bottom (using FONT_SIZE_SMALL from Phase 1)
  int cancel_center_y = card_y + card_h - 45;
  int cancel_center_x = card_x + card_w / 2 - 40;
  ui_draw_circle_outline(cancel_center_x, cancel_center_y, 12, UI_COLOR_TEXT_TERTIARY);
  vita2d_font_draw_text(font, cancel_center_x + 20, cancel_center_y + 6,
                        UI_COLOR_TEXT_TERTIARY, FONT_SIZE_BODY, "Cancel");

  // Handle Circle button to cancel
  if (btn_pressed(SCE_CTRL_CIRCLE)) {
    LOGD("Connection cancelled by user");
    host_cancel_stream_request();
    ui_connection_cancel();
    return UI_SCREEN_TYPE_MAIN;
  }

  return UI_SCREEN_TYPE_WAKING;  // Continue showing waking screen
}

/// Draw reconnecting screen with modern polished UI
/// Shows during packet loss recovery with spinner animation
/// @return the next screen type
UIScreenType ui_screen_draw_reconnecting(void) {
  // Check if we should still be showing this screen
  if (!context.stream.reconnect_overlay_active) {
    ui_state_set_reconnect_start_time(0);
    return UI_SCREEN_TYPE_MAIN;
  }

  // Initialize timer on first call
  if (ui_state_get_reconnect_start_time() == 0) {
    ui_state_set_reconnect_start_time(sceKernelGetProcessTimeLow() / 1000);
  }

  // Get current time for animations
  uint32_t current_time = sceKernelGetProcessTimeLow() / 1000;

  // Draw modern reconnecting screen (consistent with Waking screen)
  vita2d_set_clear_color(UI_COLOR_BACKGROUND);

  // Card dimensions (taller to accommodate all info + spinner)
  int card_w = 640;
  int card_h = 380;
  int card_x = (VITA_WIDTH - card_w) / 2;
  int card_y = (VITA_HEIGHT - card_h) / 2;

  // Draw card with enhanced shadow (Phase 1 & 2 style)
  ui_draw_card_with_shadow(card_x, card_y, card_w, card_h, 12, UI_COLOR_CARD_BG);

  // PlayStation Blue accent borders
  vita2d_draw_rectangle(card_x, card_y, card_w, 2, UI_COLOR_PRIMARY_BLUE);
  vita2d_draw_rectangle(card_x, card_y + card_h - 2, card_w, 2, UI_COLOR_PRIMARY_BLUE);

  // Title (centered)
  const char* title = "Optimizing Stream";
  int title_size = 28;
  int title_w = vita2d_font_text_width(font, title_size, title);
  int title_x = card_x + (card_w - title_w) / 2;
  vita2d_font_draw_text(font, title_x, card_y + 50, UI_COLOR_TEXT_PRIMARY, title_size, title);

  // Subtitle explaining what's happening (centered)
  const char* subtitle = "Recovering from packet loss";
  int subtitle_w = get_text_width_cached(subtitle, FONT_SIZE_BODY);
  int subtitle_x = card_x + (card_w - subtitle_w) / 2;
  vita2d_font_draw_text(font, subtitle_x, card_y + 85, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_BODY, subtitle);

  // Retry bitrate info (centered)
  float retry_mbps = context.stream.loss_retry_bitrate_kbps > 0
                         ? (float)context.stream.loss_retry_bitrate_kbps / 1000.0f
                         : 0.8f;
  char detail[64];
  snprintf(detail, sizeof(detail), "Retrying at %.2f Mbps", retry_mbps);
  int detail_w = vita2d_font_text_width(font, FONT_SIZE_BODY, detail);
  int detail_x = card_x + (card_w - detail_w) / 2;
  vita2d_font_draw_text(font, detail_x, card_y + 115, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_BODY, detail);

  // Spinner animation (matching Waking screen style)
  int spinner_cx = card_x + card_w / 2;
  int spinner_cy = card_y + card_h / 2 + 20;
  int spinner_radius = 32;
  int spinner_thickness = 5;
  float rotation = (float)((current_time * 720) % 360000) / 1000.0f;  // 2 rotations/sec
  ui_draw_spinner(spinner_cx, spinner_cy, spinner_radius, spinner_thickness, rotation, UI_COLOR_PRIMARY_BLUE);

  // Attempt count below spinner (centered)
  char attempt_text[64];
  snprintf(attempt_text, sizeof(attempt_text), "Attempt %u", context.stream.loss_retry_attempts);
  int attempt_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, attempt_text);
  int attempt_x = card_x + (card_w - attempt_w) / 2;
  vita2d_font_draw_text(font, attempt_x, card_y + card_h - 60, UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL, attempt_text);

  // Status message at bottom (centered)
  const char* status_msg = "Please wait...";
  int status_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, status_msg);
  int status_x = card_x + (card_w - status_w) / 2;
  vita2d_font_draw_text(font, status_x, card_y + card_h - 30, UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL, status_msg);

  return UI_SCREEN_TYPE_RECONNECTING;
}

/// Draw the debug messages screen
/// @return whether the dialog should keep rendering
bool ui_screen_draw_messages(void) {
  vita2d_set_clear_color(RGBA8(0x00, 0x00, 0x00, 0xFF));
  context.ui_state.next_active_item = -1;

  // initialize mlog_line_offset
  if (!context.ui_state.mlog_last_update) context.ui_state.mlog_line_offset = -1;
  if (context.ui_state.mlog_last_update != context.mlog->last_update) {
    context.ui_state.mlog_last_update = context.mlog->last_update;
    context.ui_state.mlog_line_offset = -1;
  }


  int w = VITA_WIDTH;
  int h = VITA_HEIGHT;

  int left_margin = 12;
  int top_margin = 20;
  int bottom_margin = 20;
  int font_size = 18;
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
    vita2d_font_draw_text(font_mono, left_margin, y,
                          COLOR_GRAY50, font_size,
                          note
                          );
    y += line_height;
    i_y ++;
  }

  int j;
  for (j = line_offset; j < context.mlog->lines; j++) {
    if (i_y > max_lines - 1) break;
    if (overflow && (i_y == max_lines - 1)) {
      if (j < context.mlog->lines - 1) break;
    }
    vita2d_font_draw_text(font_mono, left_margin, y,
                          COLOR_WHITE, font_size,
                          get_message_log_line(context.mlog, j)
                          );
    y += line_height;
    i_y ++;
  }
  if (overflow && (j < context.mlog->lines - 1)) {
    char note[100];
    int lines_below = context.mlog->lines - j - 1;
    if (lines_below == 1) {
      snprintf(note, 100, "<%d line below>", lines_below);
    } else {
      snprintf(note, 100, "<%d lines below>", lines_below);
    }
    vita2d_font_draw_text(font_mono, left_margin, y,
                          COLOR_GRAY50, font_size,
                          note
                          );
    y += line_height;
    i_y ++;
  }

  if (btn_pressed(SCE_CTRL_UP)) {
    if (overflow) {
      int next_offset = line_offset - 1;

      if (next_offset == 1) next_offset = 0;
      if (next_offset == max_line_offset-1) next_offset = max_line_offset-2;

      if (next_offset < 0) next_offset = line_offset;
      context.ui_state.mlog_line_offset = next_offset;
    }
  }
  if (btn_pressed(SCE_CTRL_DOWN)) {
    if (overflow) {
      int next_offset = line_offset + 1;

      if (next_offset == max_line_offset - 1) next_offset = max_line_offset;
      if (next_offset == 1) next_offset = 2;

      if (next_offset > max_line_offset) next_offset = max_line_offset;
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
  // Initialize screen-specific state
  pin_entry_initialized = false;
  cursor_blink_timer = 0;
  
  // PIN entry state will be initialized on first use
  for (int i = 0; i < 8; i++) {
    pin_entry_state.pin_digits[i] = 10;  // 10 = empty
  }
  pin_entry_state.current_digit = 0;
  pin_entry_state.pin_complete = false;
  pin_entry_state.complete_pin = 0;
  
  // Get touch input state pointers from ui_input module
  touch_block_active = ui_input_get_touch_block_active_ptr();
  touch_block_pending_clear = ui_input_get_touch_block_pending_clear_ptr();
}
