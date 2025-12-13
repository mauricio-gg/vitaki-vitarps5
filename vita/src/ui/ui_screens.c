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
#include "ui.h"
#include "util.h"
#include "video.h"
#include "ui/ui_screens.h"
#include "ui/ui_internal.h"
#include "ui/ui_components.h"

// ============================================================================
// Constants (use definitions from ui_constants.h via ui_internal.h)
// ============================================================================

#define VIDEO_LOSS_ALERT_DEFAULT_US (5 * 1000 * 1000ULL)

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
// ============================================================================
// Forward declarations for helper functions
// ============================================================================


// Legacy helper function for touch detection (should use is_point_in_rect from ui_input.c)
static bool is_touched(int x, int y, int width, int height) {
  SceTouchData* tdf = &(context.ui_state.touch_state_front);
  if (!tdf) {
    return false;
  }
  return tdf->report->x > x && tdf->report->x <= x + width &&
         tdf->report->y > y && tdf->report->y <= y + height;
}
static void reset_pin_entry(void);
static void update_cursor_blink(void);
static bool is_pin_complete(void);
static uint32_t pin_to_number(void);
static UIScreenType handle_vitarps5_touch_input(int num_hosts);


// ============================================================================
// Touch Input Handler
// ============================================================================

UIScreenType handle_vitarps5_touch_input(int num_hosts) {
  SceTouchData touch;
  sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

  if (context.ui_state.error_popup_active || context.ui_state.debug_menu_active) {
    return UI_SCREEN_TYPE_MAIN;
  }

  if (*touch_block_active) {
    if (touch.reportNum == 0) {
      // Finger lifted - clear the block
      *touch_block_active = false;
      *touch_block_pending_clear = false;
    } else {
      return UI_SCREEN_TYPE_MAIN;  // Still blocking while finger is down
    }
  }

  if (touch.reportNum > 0) {
    // Convert touch coordinates to screen coordinates
    // Vita touch resolution: 1920x1088, screen: 960x544
    float touch_x = (touch.report[0].x / 1920.0f) * 960.0f;
    float touch_y = (touch.report[0].y / 1088.0f) * 544.0f;

    UIScreenType nav_touch_screen;
    if (nav_touch_hit(touch_x, touch_y, &nav_touch_screen))
      return nav_touch_screen;

    // Check console cards (rectangular hitboxes)
    if (num_hosts > 0) {
      int content_area_x = WAVE_NAV_WIDTH + ((VITA_WIDTH - WAVE_NAV_WIDTH) / 2);
      for (int i = 0; i < num_hosts; i++) {
        int card_x = content_area_x - (CONSOLE_CARD_WIDTH / 2);
        int card_y = CONSOLE_CARD_START_Y + (i * CONSOLE_CARD_SPACING);

        if (is_point_in_rect(touch_x, touch_y, card_x, card_y,
            CONSOLE_CARD_WIDTH, CONSOLE_CARD_HEIGHT)) {
          // Select card and trigger connect action
          ui_cards_set_selected_index(i);

          // Find and connect to selected host
          int host_idx = 0;
          for (int j = 0; j < MAX_NUM_HOSTS; j++) {
              if (context.hosts[j]) {
                if (host_idx == ui_cards_get_selected_index()) {
                  context.active_host = context.hosts[j];

                  if (takion_cooldown_gate_active()) {
                    LOGD("Touch connect ignored — network recovery cooldown active");
                    return UI_SCREEN_TYPE_MAIN;
                  }

                  bool discovered = (context.active_host->type & DISCOVERED) && (context.active_host->discovery_state);
                  bool registered = context.active_host->type & REGISTERED;
                bool at_rest = discovered && context.active_host->discovery_state &&
                               context.active_host->discovery_state->state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY;

                if (discovered && !at_rest && registered) {
                  ui_connection_begin(UI_CONNECTION_STAGE_CONNECTING);
                  if (!start_connection_thread(context.active_host)) {
                    ui_connection_cancel();
                    return UI_SCREEN_TYPE_MAIN;
                  }
                  ui_state_set_waking_wait_for_stream_us(sceKernelGetProcessTimeWide());
                  return UI_SCREEN_TYPE_WAKING;
                } else if (at_rest) {
                  LOGD("Touch wake gesture on dormant console");
                  ui_connection_begin(UI_CONNECTION_STAGE_WAKING);
                  host_wakeup(context.active_host);
                  return UI_SCREEN_TYPE_WAKING;
                } else if (!registered) {
                  return UI_SCREEN_TYPE_REGISTER_HOST;
                }
                break;
              }
              host_idx++;
            }
          }
          break;
        }
      }

      // Check "Add New" button
      if (button_add_new) {
        int btn_w = vita2d_texture_get_width(button_add_new);
        int btn_x = content_area_x - (btn_w / 2);
        int btn_y = CONSOLE_CARD_START_Y + (num_hosts * CONSOLE_CARD_SPACING) + 20;
        int btn_h = vita2d_texture_get_height(button_add_new);

        if (is_point_in_rect(touch_x, touch_y, btn_x, btn_y, btn_w, btn_h)) {
          if (!context.discovery_enabled) {
            start_discovery(NULL, NULL);
          }
        }
      }
    }
  }

  return UI_SCREEN_TYPE_MAIN;
}

/// Draw the tile for a host
/// @return The action to take for the host
UIHostAction host_tile(int host_slot, VitaChiakiHost* host) {
  int active_id = context.ui_state.active_item;
  bool is_active = active_id == (UI_MAIN_WIDGET_HOST_TILE | host_slot);
  bool discovered = (host->type & DISCOVERED) && (host->discovery_state);
  bool registered = host->type & REGISTERED;
  bool added = host->type & MANUALLY_ADDED;
  bool mutable = (added || registered);
  bool at_rest = discovered && host->discovery_state->state ==
                                   CHIAKI_DISCOVERY_HOST_STATE_STANDBY;

  int x = HOST_SLOTS_X + (host_slot % 2) * (HOST_SLOT_W + 58);
  int y = HOST_SLOTS_Y;
  if (host_slot > 1) {
    y += HOST_SLOT_H + 11;
  }
  // Draw card with shadow for modern look
  if (is_active) {
    // Active selection border with glow effect
    ui_draw_rounded_rect(x - 3, y - 3, HOST_SLOT_W + 6, HOST_SLOT_H + 6, 8, UI_COLOR_PRIMARY_BLUE);
  }
  ui_draw_card_with_shadow(x, y, HOST_SLOT_W, HOST_SLOT_H, 8, UI_COLOR_CARD_BG);

  // Draw host name (nickname) and host id (mac)
  if (discovered) {
    vita2d_draw_texture(img_discovery_host, x, y);
    vita2d_font_draw_text(font, x + 68, y + 40, COLOR_WHITE, 40,
                         host->discovery_state->host_name);
    vita2d_font_draw_text(font, x + 255, y + 23, COLOR_WHITE, 20,
                         host->discovery_state->host_id);
  } else if (registered) {
    char* nickname = host->registered_state->server_nickname;
    if (!nickname) nickname = "";
    uint8_t* host_mac = host->server_mac;
    vita2d_font_draw_text(font, x + 68, y + 40, COLOR_WHITE, 40,
                          nickname);
    vita2d_font_draw_textf(font, x + 255, y + 23, COLOR_WHITE, 20,
                          "%X%X%X%X%X%X", host_mac[0], host_mac[1], host_mac[2],
                          host_mac[3], host_mac[4], host_mac[5]);
  }

  // Draw how many manually added instances of this console exist
  if (discovered && registered) {
    int num_mhosts = count_manual_hosts_of_console(host);
    if (num_mhosts == 1) {
      vita2d_font_draw_text(font, x + 10, y + HOST_SLOT_H - 10, COLOR_WHITE, 20, "(1 manual remote host)");
    } else if (num_mhosts > 1) {
      vita2d_font_draw_textf(font, x + 10, y + HOST_SLOT_H - 10, COLOR_WHITE, 20, "(%d manual remote hosts)", num_mhosts);
    } else {
      vita2d_font_draw_textf(font, x + 10, y + HOST_SLOT_H - 10, COLOR_WHITE, 20, "[%d manual remote hosts]", num_mhosts);
    }
  }

  // Draw host address
  vita2d_font_draw_text(font, x + 260, y + HOST_SLOT_H - 10, COLOR_WHITE, 20, host->hostname);

  vita2d_texture* console_img;
  bool is_ps5 = chiaki_target_is_ps5(host->target);
  // TODO: Don't use separate textures for off/on/rest, use tinting instead
  if (added) {// && !discovered) {
    console_img = is_ps5 ? img_ps5_off : img_ps4_off;
  } else if (at_rest) {
    console_img = is_ps5 ? img_ps5_rest : img_ps4_rest;
  } else {
    console_img = is_ps5 ? img_ps5 : img_ps4;
  }
  vita2d_draw_texture(console_img, x + 64, y + 64);
  if (discovered && !at_rest) {
    const char* app_name = host->discovery_state->running_app_name;
    const char* app_id = host->discovery_state->running_app_titleid;
    if (app_name && app_id) {
      vita2d_font_draw_text(font, x + 32, y + 16, COLOR_WHITE, 16, app_name);
      vita2d_font_draw_text(font, x + 300, y + 170, COLOR_WHITE, 16, app_id);
    }
  }

  // set tooltip
  if (is_active) {
    if (at_rest) {
      if (registered) {
        snprintf(active_tile_tooltip_msg, MAX_TOOLTIP_CHARS, "%s: send wake signal (note: console may be temporarily undetected during wakeup)", confirm_btn_str);
      } else {
        snprintf(active_tile_tooltip_msg, MAX_TOOLTIP_CHARS, "Cannot send wake signal to unregistered console.");
      }
    } else {
      if (discovered && !registered) {
        snprintf(active_tile_tooltip_msg, MAX_TOOLTIP_CHARS, "%s: begin pairing process", confirm_btn_str);
      } else if (discovered && registered) {
        snprintf(active_tile_tooltip_msg, MAX_TOOLTIP_CHARS, "%s: start remote play;  Square: re-pair", confirm_btn_str);
      } else if (added) {
        snprintf(active_tile_tooltip_msg, MAX_TOOLTIP_CHARS, "%s: send wake signal and/or start remote play (wakeup takes time);  SELECT button: delete host (no confirmation)", confirm_btn_str);
      } else {
        // there should never be tiles that are neither discovered nor added
        snprintf(active_tile_tooltip_msg, MAX_TOOLTIP_CHARS, "");
      }
    }
  }

  // Handle navigation
  int btn = context.ui_state.button_state;
  int old_btn = context.ui_state.old_button_state;
  int last_slot = context.num_hosts - 1;
  if (is_active) {
    if (context.active_host != host) {
      context.active_host = host;
    }
    if (btn_pressed(SCE_CTRL_UP)) {
      if (host_slot < 2) {
        // Set focus on the last button of the header bar
        context.ui_state.next_active_item = UI_MAIN_WIDGET_SETTINGS_BTN;
      } else {
        // Set focus on the host tile directly above
        context.ui_state.next_active_item =
            UI_MAIN_WIDGET_HOST_TILE | (host_slot - 2);
      }
    } else if (btn_pressed(SCE_CTRL_RIGHT)) {
      if (host_slot != last_slot && (host_slot == 0 || host_slot == 2)) {
        // Set focus on the host tile to the right
        context.ui_state.next_active_item =
            UI_MAIN_WIDGET_HOST_TILE | (host_slot + 1);
      }
    } else if (btn_pressed(SCE_CTRL_DOWN)) {
      if (last_slot >= host_slot + 2 && host_slot < 2) {
        // Set focus on the host tile directly below
        context.ui_state.next_active_item =
            UI_MAIN_WIDGET_HOST_TILE | (host_slot + 2);
      }
    } else if (btn_pressed(SCE_CTRL_LEFT)) {
      if (host_slot == 1 || host_slot == 3) {
        context.ui_state.next_active_item =
            UI_MAIN_WIDGET_HOST_TILE | (host_slot - 1);
      }
    }

    if (btn_pressed(SCE_CTRL_SELECT) && added) {
      delete_manual_host(host);
      // TODO delete from manual hosts

      // refresh tiles
      update_context_hosts();
    }

    if (registered && btn_pressed(SCE_CTRL_CONFIRM)) {
      if (takion_cooldown_gate_active()) {
        LOGD("Ignoring connect request — network recovery cooldown active");
        return UI_HOST_ACTION_NONE;
      }
      if (at_rest) {
        ui_connection_begin(UI_CONNECTION_STAGE_WAKING);
        host_wakeup(context.active_host);
        return UI_HOST_ACTION_WAKEUP;
      } else {
        // since we don't know if the remote host is awake, send wakeup signal
        if (added) host_wakeup(context.active_host);
        vita2d_end_drawing();
        vita2d_common_dialog_update();
        vita2d_swap_buffers();
        ui_connection_begin(UI_CONNECTION_STAGE_CONNECTING);
        if (!start_connection_thread(context.active_host)) {
          ui_connection_cancel();
          return UI_HOST_ACTION_NONE;
        }
        ui_state_set_waking_wait_for_stream_us(sceKernelGetProcessTimeWide());
        return UI_HOST_ACTION_STREAM;
      }
    } else if (!registered && !added && discovered && btn_pressed(SCE_CTRL_CONFIRM)){
      if (at_rest) {
        LOGD("Cannot wake unregistered console.");
        return UI_HOST_ACTION_NONE;
      }
      return UI_HOST_ACTION_REGISTER;
    } else if (discovered && btn_pressed(SCE_CTRL_SQUARE)) {
      return UI_HOST_ACTION_REGISTER;
    }
  }
  if (is_touched(x, y, HOST_SLOT_W, HOST_SLOT_H)) {
    context.ui_state.next_active_item = UI_MAIN_WIDGET_HOST_TILE | host_slot;
  }
  return UI_HOST_ACTION_NONE;
}

bool showingIME = false;



UIScreenType draw_main_menu() {
  // Update and render VitaRPS5 particle background
  ui_particles_update();
  ui_particles_render();

  UIScreenType nav_screen;
  if (handle_global_nav_shortcuts(&nav_screen, true))
    return nav_screen;

  // Render VitaRPS5 console cards instead of host tiles
  ui_cards_render_grid();

  // Count hosts
  int num_hosts = 0;
  for (int i = 0; i < MAX_NUM_HOSTS; i++) {
    if (context.hosts[i]) num_hosts++;
  }

  UIScreenType next_screen = UI_SCREEN_TYPE_MAIN;

  // === D-PAD NAVIGATION (content area only) ===
  // Note: Nav bar D-pad handling is done in ui_nav_handle_shortcuts() above.
  // We only handle content area D-pad here to avoid double-processing.

  if (current_focus == FOCUS_CONSOLE_CARDS) {
    if (btn_pressed(SCE_CTRL_UP) && num_hosts > 0) {
      // Move up within console cards (cycle through)
      ui_cards_set_selected_index((ui_cards_get_selected_index() - 1 + num_hosts) % num_hosts);
    } else if (btn_pressed(SCE_CTRL_DOWN) && num_hosts > 0) {
      // Move down within console cards (cycle through)
      ui_cards_set_selected_index((ui_cards_get_selected_index() + 1) % num_hosts);
    } else if (btn_pressed(SCE_CTRL_LEFT)) {
      // Move left to nav bar
      last_console_selection = ui_cards_get_selected_index();
      current_focus = FOCUS_NAV_BAR;
    }
  }
  // Note: SCE_CTRL_RIGHT from nav bar to content is handled in ui_nav_handle_shortcuts()

  // === X BUTTON (Activate/Select highlighted element) ===

  if (btn_pressed(SCE_CTRL_CROSS)) {
    if (current_focus == FOCUS_CONSOLE_CARDS && num_hosts > 0) {
      // Connect to selected console
      int host_idx = 0;
      for (int i = 0; i < MAX_NUM_HOSTS; i++) {
        if (context.hosts[i]) {
          if (host_idx == ui_cards_get_selected_index()) {
            context.active_host = context.hosts[i];

            if (takion_cooldown_gate_active()) {
              LOGD("Ignoring connect request — network recovery cooldown active");
              return UI_SCREEN_TYPE_MAIN;
            }

            bool discovered = (context.active_host->type & DISCOVERED) && (context.active_host->discovery_state);
            bool registered = context.active_host->type & REGISTERED;
            bool at_rest = discovered && context.active_host->discovery_state &&
                           context.active_host->discovery_state->state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY;

            if (!registered) {
              // Unregistered console - start registration
              next_screen = UI_SCREEN_TYPE_REGISTER_HOST;
            } else if (at_rest) {
              // Dormant console - wake and show waking screen
              LOGD("Waking dormant console...");
              ui_connection_begin(UI_CONNECTION_STAGE_WAKING);
              host_wakeup(context.active_host);
              next_screen = UI_SCREEN_TYPE_WAKING;
            } else if (registered) {
              // Ready console - start streaming with feedback
              ui_connection_begin(UI_CONNECTION_STAGE_CONNECTING);
              next_screen = UI_SCREEN_TYPE_WAKING;
              if (!start_connection_thread(context.active_host)) {
                ui_connection_cancel();
                next_screen = UI_SCREEN_TYPE_MAIN;
              } else {
                ui_state_set_waking_wait_for_stream_us(sceKernelGetProcessTimeWide());
              }
            }
            break;
          }
          host_idx++;
        }
      }
    }
  }

  // === OTHER BUTTONS ===

  // Square: Re-pair selected console (unregister + register again)
  if (btn_pressed(SCE_CTRL_SQUARE) && current_focus == FOCUS_CONSOLE_CARDS && num_hosts > 0) {
    int host_idx = 0;
    for (int i = 0; i < MAX_NUM_HOSTS; i++) {
      if (context.hosts[i]) {
        if (host_idx == ui_cards_get_selected_index()) {
          VitaChiakiHost* host = context.hosts[i];
          bool registered = host->type & REGISTERED;

          if (registered) {
            // Remove registration and trigger re-pairing
            LOGD("Re-pairing console: %s", host->hostname);

            // Free registered state memory
            if (host->registered_state) {
              free(host->registered_state);
              host->registered_state = NULL;
            }

            // Remove from config.registered_hosts array
            for (int j = 0; j < context.config.num_registered_hosts; j++) {
              if (context.config.registered_hosts[j] == host) {
                // Shift remaining elements left
                for (int k = j; k < context.config.num_registered_hosts - 1; k++) {
                  context.config.registered_hosts[k] = context.config.registered_hosts[k + 1];
                }
                context.config.registered_hosts[context.config.num_registered_hosts - 1] = NULL;
                context.config.num_registered_hosts--;
                break;
              }
            }

            // Clear registered flag
            host->type &= ~REGISTERED;

            // Save config to persist changes
            config_serialize(&context.config);

            LOGD("Registration data deleted for console: %s", host->hostname);

            // Trigger registration screen
            context.active_host = host;
            next_screen = UI_SCREEN_TYPE_REGISTER_HOST;
          }
          break;
        }
        host_idx++;
      }
    }
  }

  // Handle touch screen input for VitaRPS5 UI
  UIScreenType touch_screen = handle_vitarps5_touch_input(num_hosts);
  if (touch_screen != UI_SCREEN_TYPE_MAIN) {
    return touch_screen;
  }

  // Select button shows hints popup
  if (btn_pressed(SCE_CTRL_SELECT)) {
    trigger_hints_popup("D-Pad: Navigate | Cross: Connect/Wake | Square: Re-pair");
  }

  return next_screen;
}

// ============================================================================
// PHASE 2: SETTINGS SCREEN
// ============================================================================

typedef enum {
  SETTINGS_TAB_STREAMING = 0,
  SETTINGS_TAB_COUNT = 1  // Only Streaming tab (removed Video/Network - no backend support)
} SettingsTab;

typedef struct {
  SettingsTab current_tab;
  int selected_item;
  bool dropdown_expanded;
  int dropdown_selected_option;
} SettingsState;

static SettingsState settings_state = {0};

// Tab color (Blue) - Only Streaming settings, Video/Network removed (no backend support)
static uint32_t settings_tab_colors[SETTINGS_TAB_COUNT] = {
  RGBA8(0x00, 0x70, 0xCC, 255), // Blue - Streaming
};

static const char* settings_tab_names[SETTINGS_TAB_COUNT] = {
  "Streaming Quality"
};

// Resolution/FPS option strings for dropdowns
static const char* resolution_options[] = {"720p", "1080p"};
static const char* fps_options[] = {"30 FPS", "60 FPS"};

/// Get resolution string from ChiakiVideoResolutionPreset
static const char* get_resolution_string(ChiakiVideoResolutionPreset preset) {
  switch (preset) {
    case CHIAKI_VIDEO_RESOLUTION_PRESET_360p: return "360p";
    case CHIAKI_VIDEO_RESOLUTION_PRESET_540p: return "540p";
    case CHIAKI_VIDEO_RESOLUTION_PRESET_720p: return "720p (Experimental)";
    case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p: return "1080p (Experimental)";
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

/// Draw Streaming Quality tab content
static void draw_settings_streaming_tab(int content_x, int content_y, int content_w) {
  int item_h = 50;
  int item_spacing = 10;
  int y = content_y;

  // Quality Preset dropdown
  draw_dropdown(content_x, y, content_w, item_h, "Quality Preset",
                get_resolution_string(context.config.resolution),
                false, settings_state.selected_item == 0);
  y += item_h + item_spacing;

  // Latency/Bandwidth mode dropdown
  draw_dropdown(content_x, y, content_w, item_h, "Latency Mode",
                get_latency_mode_string(context.config.latency_mode),
                false, settings_state.selected_item == 1);
  y += item_h + item_spacing;

  // FPS Target dropdown
  draw_dropdown(content_x, y, content_w, item_h, "FPS Target",
                get_fps_string(context.config.fps),
                false, settings_state.selected_item == 2);
  y += item_h + item_spacing;

  // Force 30 FPS toggle
  draw_toggle_switch(content_x + content_w - 70, y + (item_h - 30)/2, 60, 30,
                     get_toggle_animation_value(3, context.config.force_30fps),
                     settings_state.selected_item == 3);
  vita2d_font_draw_text(font, content_x + 15, y + item_h/2 + 6,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_BODY, "Force 30 FPS Output");
  y += item_h + item_spacing;

  // Auto Discovery toggle
  draw_toggle_switch(content_x + content_w - 70, y + (item_h - 30)/2, 60, 30,
                     get_toggle_animation_value(4, context.config.auto_discovery),
                     settings_state.selected_item == 4);
  vita2d_font_draw_text(font, content_x + 15, y + item_h/2 + 6,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_BODY, "Auto Discovery");
  y += item_h + item_spacing;

  // Show Latency toggle
  draw_toggle_switch(content_x + content_w - 70, y + (item_h - 30)/2, 60, 30,
                     get_toggle_animation_value(5, context.config.show_latency),
                     settings_state.selected_item == 5);
  vita2d_font_draw_text(font, content_x + 15, y + item_h/2 + 6,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_BODY, "Show Latency");
  y += item_h + item_spacing;

  // Show Network Alerts toggle
  draw_toggle_switch(content_x + content_w - 70, y + (item_h - 30)/2, 60, 30,
                     get_toggle_animation_value(8, context.config.show_network_indicator),
                     settings_state.selected_item == 6);
  vita2d_font_draw_text(font, content_x + 15, y + item_h/2 + 6,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_BODY, "Show Network Alerts");
  y += item_h + item_spacing;

  // Clamp soft restart bitrate toggle
  draw_toggle_switch(content_x + content_w - 70, y + (item_h - 30)/2, 60, 30,
                     get_toggle_animation_value(7, context.config.clamp_soft_restart_bitrate),
                     settings_state.selected_item == 7);
  vita2d_font_draw_text(font, content_x + 15, y + item_h/2 + 6,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_BODY,
                        "Clamp Soft Restart Bitrate");
  y += item_h + item_spacing;

  // Video stretch toggle
  draw_toggle_switch(content_x + content_w - 70, y + (item_h - 30)/2, 60, 30,
                     get_toggle_animation_value(6, context.config.stretch_video),
                     settings_state.selected_item == 8);
  vita2d_font_draw_text(font, content_x + 15, y + item_h/2 + 6,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_BODY, "Fill Screen");
  y += item_h + item_spacing;

  // Keep navigation pinned toggle (prevents auto-collapse on content interaction)
  draw_toggle_switch(content_x + content_w - 70, y + (item_h - 30)/2, 60, 30,
                     get_toggle_animation_value(9, context.config.keep_nav_pinned),
                     settings_state.selected_item == 9);
  vita2d_font_draw_text(font, content_x + 15, y + item_h/2 + 6,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_BODY, "Keep Navigation Pinned");
}

/// Draw Controller Settings tab content
static void draw_settings_controller_tab(int content_x, int content_y, int content_w) {
  int item_h = 50;
  int item_spacing = 10;
  int y = content_y;

  // Controller Map ID dropdown
  char map_id_str[32];
  snprintf(map_id_str, sizeof(map_id_str), "Map %d", context.config.controller_map_id);
  draw_dropdown(content_x, y, content_w, item_h, "Controller Map", map_id_str,
                false, settings_state.selected_item == 0);
  y += item_h + item_spacing;

  // Button layout toggle (Circle vs Cross confirm)
  draw_toggle_switch(content_x + content_w - 70, y + (item_h - 30)/2, 60, 30,
                     get_toggle_animation_value(101, context.config.circle_btn_confirm),
                     settings_state.selected_item == 1);
  vita2d_font_draw_text(font, content_x + 15, y + item_h/2 + 6,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_BODY, "Circle Button Confirm");
  y += item_h + item_spacing;

  // TODO(PHASE2-STUB): Motion Controls - Not implemented
  draw_toggle_switch(content_x + content_w - 70, y + (item_h - 30)/2, 60, 30,
                     get_toggle_animation_value(102, false),
                     settings_state.selected_item == 2);
  vita2d_font_draw_text(font, content_x + 15, y + item_h/2 + 6,
                        UI_COLOR_TEXT_SECONDARY, FONT_SIZE_BODY, "Motion Controls (Stub)");
}

/// Main Settings screen rendering function
/// @return next screen to display
UIScreenType draw_settings() {
  // Render particle background
  ui_particles_update();
  ui_particles_render();

  UIScreenType nav_screen;
  if (handle_global_nav_shortcuts(&nav_screen, true))
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

  // Draw streaming settings content
  draw_settings_streaming_tab(tab_content_x, tab_content_y, tab_content_w);

  // Select button shows hints popup
  if (btn_pressed(SCE_CTRL_SELECT)) {
    trigger_hints_popup("Up/Down: Navigate | X: Toggle/Select | Circle: Back");
  }

  // === INPUT HANDLING ===
  // Note: Nav bar D-pad handling is done in ui_nav_handle_shortcuts() above.
  // We only handle content area D-pad here to avoid double-processing.

  // No tab switching needed - only one section
  int max_items = 10; // Resolution, Latency Mode, FPS, Force 30 FPS, Auto Discovery, Show Latency, Network Alerts, Clamp, Fill Screen, Keep Nav Pinned

  // Up/Down: Navigate items (only when focused on content, not nav bar)
  if (current_focus != FOCUS_NAV_BAR) {
    if (btn_pressed(SCE_CTRL_UP)) {
      settings_state.selected_item = (settings_state.selected_item - 1 + max_items) % max_items;
    } else if (btn_pressed(SCE_CTRL_DOWN)) {
      settings_state.selected_item = (settings_state.selected_item + 1) % max_items;
    }
  }

  // X: Activate selected item (toggle or cycle dropdown)
  if (btn_pressed(SCE_CTRL_CROSS)) {
    if (settings_state.selected_item == 0) {
          // Cycle resolution: 360p → 540p → 720p → 1080p → 360p
          switch (context.config.resolution) {
            case CHIAKI_VIDEO_RESOLUTION_PRESET_360p:
              context.config.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
              break;
            case CHIAKI_VIDEO_RESOLUTION_PRESET_540p:
              context.config.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
              break;
            case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
              context.config.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_1080p;
              break;
            case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p:
            default:
              context.config.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_360p;
              break;
          }
          config_serialize(&context.config);
        } else if (settings_state.selected_item == 1) {
          // Cycle latency modes
          context.config.latency_mode =
            (context.config.latency_mode + 1) % VITA_LATENCY_MODE_COUNT;
          config_serialize(&context.config);
        } else if (settings_state.selected_item == 2) {
          // Cycle FPS
          context.config.fps = (context.config.fps == CHIAKI_VIDEO_FPS_PRESET_30) ?
            CHIAKI_VIDEO_FPS_PRESET_60 : CHIAKI_VIDEO_FPS_PRESET_30;
          config_serialize(&context.config);
        } else if (settings_state.selected_item == 3) {
          context.config.force_30fps = !context.config.force_30fps;
          start_toggle_animation(3, context.config.force_30fps);
          config_serialize(&context.config);
          apply_force_30fps_runtime();
        } else if (settings_state.selected_item == 4) {
          // Auto discovery toggle
          context.config.auto_discovery = !context.config.auto_discovery;
          start_toggle_animation(4, context.config.auto_discovery);
          config_serialize(&context.config);
    } else if (settings_state.selected_item == 5) {
      // Show latency toggle
      context.config.show_latency = !context.config.show_latency;
      start_toggle_animation(5, context.config.show_latency);
      config_serialize(&context.config);
        } else if (settings_state.selected_item == 6) {
          context.config.show_network_indicator = !context.config.show_network_indicator;
          start_toggle_animation(8, context.config.show_network_indicator);
          if (!context.config.show_network_indicator) {
            vitavideo_hide_poor_net_indicator();
          }
          config_serialize(&context.config);
        } else if (settings_state.selected_item == 7) {
          context.config.clamp_soft_restart_bitrate = !context.config.clamp_soft_restart_bitrate;
          start_toggle_animation(7, context.config.clamp_soft_restart_bitrate);
          config_serialize(&context.config);
        } else if (settings_state.selected_item == 8) {
          context.config.stretch_video = !context.config.stretch_video;
          start_toggle_animation(6, context.config.stretch_video);
          config_serialize(&context.config);
        } else if (settings_state.selected_item == 9) {
          context.config.keep_nav_pinned = !context.config.keep_nav_pinned;
          start_toggle_animation(9, context.config.keep_nav_pinned);
          config_serialize(&context.config);
    }
  }

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

  // "Account ID: xxxx" label at bottom
  vita2d_font_draw_text(font, content_x, y + height - 30,
                        UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL, "Account ID");
  vita2d_font_draw_text(font, content_x, y + height - 12,
                        UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                        psn_id);
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

  // Network Type
  vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                        "Network Type");
  vita2d_font_draw_text(font, col2_x, content_y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SMALL,
                        "Local WiFi");
  content_y += line_h;

  // Console IP
  const char* console_ip = "Not Connected";
  if (context.active_host && context.active_host->discovery_state &&
      context.active_host->discovery_state->host_addr) {
    console_ip = context.active_host->discovery_state->host_addr;
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
  bool is_connected = context.active_host != NULL;
  const char* connection_text = is_connected ? "Direct" : "None";
  vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                        "Connection");
  vita2d_font_draw_text(font, col2_x, content_y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SMALL,
                        connection_text);
  content_y += line_h;

  // Remote Play status
  const char* remote_play = is_connected ? "Available" : "Unavailable";
  vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                        "Remote Play");
  vita2d_font_draw_text(font, col2_x, content_y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SMALL,
                        remote_play);
  content_y += line_h;

  // Quality Setting
  const char* quality_text = "Auto";
  if (context.config.resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_720p) {
    quality_text = "720p";
  } else if (context.config.resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_1080p) {
    quality_text = "1080p";
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
UIScreenType draw_profile_screen() {
  // Render particle background
  ui_particles_update();
  ui_particles_render();

  UIScreenType nav_screen;
  if (handle_global_nav_shortcuts(&nav_screen, true))
    return nav_screen;

  // Main content area (nav is overlay - content centered on full screen)
  int content_w = 800;  // Fixed width for content
  int content_x = (VITA_WIDTH - content_w) / 2;  // Center on screen
  int content_y = 60;

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
    trigger_hints_popup("Left/Right: Switch Card | Circle: Back");
  }

  UIScreenType next_screen = UI_SCREEN_TYPE_PROFILE;

  // === INPUT HANDLING ===

  // Left/Right: Navigate between Profile and Connection cards
  if (btn_pressed(SCE_CTRL_LEFT)) {
    profile_state.current_section = PROFILE_SECTION_INFO;
  } else if (btn_pressed(SCE_CTRL_RIGHT)) {
    profile_state.current_section = PROFILE_SECTION_CONNECTION;
  }

  // Circle: Back to main menu
  if (btn_pressed(SCE_CTRL_CIRCLE)) {
    next_screen = UI_SCREEN_TYPE_MAIN;
  }

  return next_screen;
}

// ============================================================================
// PHASE 2: CONTROLLER CONFIGURATION SCREEN (REDESIGNED WITH TABS)
// ============================================================================

typedef enum {
  CONTROLLER_TAB_MAPPINGS = 0,
  CONTROLLER_TAB_SETTINGS = 1,
  CONTROLLER_TAB_COUNT = 2
} ControllerTab;

typedef struct {
  ControllerTab current_tab;
  int selected_item;
} ControllerState;

static ControllerState controller_state = {0};

// Tab colors (PlayStation Blue theme for consistency)
static uint32_t controller_tab_colors[CONTROLLER_TAB_COUNT] = {
  RGBA8(0x34, 0x90, 0xFF, 255), // PlayStation Blue - Mappings
  RGBA8(0x00, 0x9E, 0xD8, 255), // Lighter Blue - Settings
};

static const char* controller_tab_names[CONTROLLER_TAB_COUNT] = {
  "Button Mappings",
  "Controller Settings"
};

// Button mapping data structure
typedef struct {
  const char* vita_button;
  const char* ps5_button;
} ButtonMapping;

/// Get scheme name from controller map ID
static const char* get_scheme_name(int map_id) {
  if (map_id >= 0 && map_id <= 7) {
    return "Official Layout";
  } else if (map_id == 25) {
    return "No Touchpad";
  } else if (map_id == 99) {
    return "Vitaki Custom";
  } else if (map_id >= 100) {
    return "L2/L3 R2/R3 Swap";
  }
  return "Unknown";
}

/// Get button mappings based on controller map ID (dynamically generated from controller.c logic)
static void get_button_mappings(int map_id, ButtonMapping* mappings, int* count) {
  int idx = 0;

  // Common mappings for ALL schemes
  mappings[idx++] = (ButtonMapping){"D-Pad", "D-Pad"};
  mappings[idx++] = (ButtonMapping){"Face Buttons", "Face Buttons"};
  mappings[idx++] = (ButtonMapping){"L1", "L1"};
  mappings[idx++] = (ButtonMapping){"R1", "R1"};
  mappings[idx++] = (ButtonMapping){"Select+Start", "PS Button"};

  // Map-specific L2/R2/L3/R3/Touchpad assignments
  if (map_id == 99) {
    // Map 99: Vitaki Custom (rear touch L2/R2, physical buttons L3/R3)
    mappings[idx++] = (ButtonMapping){"Rear L (near L1)", "L2"};
    mappings[idx++] = (ButtonMapping){"Rear R (near R1)", "R2"};
    mappings[idx++] = (ButtonMapping){"Left + Square", "L3"};
    mappings[idx++] = (ButtonMapping){"Right + Circle", "R3"};
    mappings[idx++] = (ButtonMapping){"Front Touch", "Touchpad"};
  } else if (map_id == 1 || map_id == 101) {
    // Map 1/101: Front touch arcs for all triggers
    mappings[idx++] = (ButtonMapping){"Front Upper-Left", "L2"};
    mappings[idx++] = (ButtonMapping){"Front Upper-Right", "R2"};
    mappings[idx++] = (ButtonMapping){"Front Lower-Left", "L3"};
    mappings[idx++] = (ButtonMapping){"Front Lower-Right", "R3"};
    mappings[idx++] = (ButtonMapping){"Front Center", "Touchpad"};
  } else if (map_id == 2 || map_id == 102) {
    // Map 2/102: Rear touch left/right for L3/R3, front arcs for L2/R2
    mappings[idx++] = (ButtonMapping){"Front Lower-Left", "L2"};
    mappings[idx++] = (ButtonMapping){"Front Lower-Right", "R2"};
    mappings[idx++] = (ButtonMapping){"Rear Left", "L3"};
    mappings[idx++] = (ButtonMapping){"Rear Right", "R3"};
    mappings[idx++] = (ButtonMapping){"Front Center", "Touchpad"};
  } else if (map_id == 3 || map_id == 103) {
    // Map 3/103: Rear touch for L3/R3, front upper arcs for L2/R2
    mappings[idx++] = (ButtonMapping){"Front Upper-Left", "L2"};
    mappings[idx++] = (ButtonMapping){"Front Upper-Right", "R2"};
    mappings[idx++] = (ButtonMapping){"Rear Left", "L3"};
    mappings[idx++] = (ButtonMapping){"Rear Right", "R3"};
    mappings[idx++] = (ButtonMapping){"Front Center", "Touchpad"};
  } else if (map_id == 4 || map_id == 104) {
    // Map 4/104: Touchpad only, no L2/R2/L3/R3
    mappings[idx++] = (ButtonMapping){"Front Touch", "Touchpad"};
    mappings[idx++] = (ButtonMapping){"L2/R2/L3/R3", "Not Mapped"};
  } else if (map_id == 5 || map_id == 105) {
    // Map 5/105: No triggers or touchpad at all
    mappings[idx++] = (ButtonMapping){"All Extras", "Not Mapped"};
  } else if (map_id == 6 || map_id == 106) {
    // Map 6/106: No L3/R3, front arcs for L2/R2
    mappings[idx++] = (ButtonMapping){"Front Lower-Left", "L2"};
    mappings[idx++] = (ButtonMapping){"Front Lower-Right", "R2"};
    mappings[idx++] = (ButtonMapping){"L3/R3", "Not Mapped"};
    mappings[idx++] = (ButtonMapping){"Front Center", "Touchpad"};
  } else if (map_id == 7 || map_id == 107) {
    // Map 7/107: No L3/R3, front upper arcs for L2/R2
    mappings[idx++] = (ButtonMapping){"Front Upper-Left", "L2"};
    mappings[idx++] = (ButtonMapping){"Front Upper-Right", "R2"};
    mappings[idx++] = (ButtonMapping){"L3/R3", "Not Mapped"};
    mappings[idx++] = (ButtonMapping){"Front Center", "Touchpad"};
  } else if (map_id == 25 || map_id == 125) {
    // Map 25/125: No touchpad, front arcs for all triggers
    mappings[idx++] = (ButtonMapping){"Front Upper-Left", "L2"};
    mappings[idx++] = (ButtonMapping){"Front Upper-Right", "R2"};
    mappings[idx++] = (ButtonMapping){"Front Lower-Left", "L3"};
    mappings[idx++] = (ButtonMapping){"Front Lower-Right", "R3"};
    mappings[idx++] = (ButtonMapping){"Touchpad", "Not Mapped"};
  } else if (map_id == 100) {
    // Map 100: Rear touch quadrants
    mappings[idx++] = (ButtonMapping){"Rear Lower-Left", "L2"};
    mappings[idx++] = (ButtonMapping){"Rear Lower-Right", "R2"};
    mappings[idx++] = (ButtonMapping){"Rear Upper-Left", "L3"};
    mappings[idx++] = (ButtonMapping){"Rear Upper-Right", "R3"};
    mappings[idx++] = (ButtonMapping){"Front Touch", "Touchpad"};
  } else {
    // Map 0 (default): Front touch arcs (same as map 1)
    mappings[idx++] = (ButtonMapping){"Front Upper-Left", "L2"};
    mappings[idx++] = (ButtonMapping){"Front Upper-Right", "R2"};
    mappings[idx++] = (ButtonMapping){"Front Lower-Left", "L3"};
    mappings[idx++] = (ButtonMapping){"Front Lower-Right", "R3"};
    mappings[idx++] = (ButtonMapping){"Front Center", "Touchpad"};
  }

  *count = idx;
}

/// Draw Mappings Tab content (scheme selector + button table + vita diagram)
static void draw_controller_mappings_tab(int content_x, int content_y, int content_w) {
  // Scheme selector at top with Left/Right arrows
  int selector_y = content_y;
  int selector_h = 50;

  // Draw scheme selector card
  ui_draw_card_with_shadow(content_x, selector_y, content_w, selector_h, 8, UI_COLOR_CARD_BG);

  // Left arrow
  vita2d_font_draw_text(font, content_x + 30, selector_y + selector_h/2 + 8,
                        UI_COLOR_PRIMARY_BLUE, FONT_SIZE_HEADER, "<");

  // Scheme text (centered)
  char scheme_text[64];
  snprintf(scheme_text, sizeof(scheme_text), "Scheme %d: %s",
           context.config.controller_map_id, get_scheme_name(context.config.controller_map_id));
  int text_w = vita2d_font_text_width(font, FONT_SIZE_SUBHEADER, scheme_text);
  vita2d_font_draw_text(font, content_x + (content_w - text_w)/2, selector_y + selector_h/2 + 6,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SUBHEADER, scheme_text);

  // Right arrow
  vita2d_font_draw_text(font, content_x + content_w - 50, selector_y + selector_h/2 + 8,
                        UI_COLOR_PRIMARY_BLUE, FONT_SIZE_HEADER, ">");

  // Hint below selector
  const char* hint = "Press Left/Right to cycle schemes";
  int hint_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, hint);
  vita2d_font_draw_text(font, content_x + (content_w - hint_w)/2, selector_y + selector_h + 18,
                        UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL, hint);

  // Layout for mapping table and diagram (side by side)
  int panel_y = selector_y + selector_h + 35;
  int panel_h = 300;
  int panel_spacing = 20;
  int panel_w = (content_w - panel_spacing) / 2;

  // Button mapping table (left panel)
  int table_x = content_x;
  ui_draw_card_with_shadow(table_x, panel_y, panel_w, panel_h, 8, UI_COLOR_CARD_BG);

  vita2d_font_draw_text(font, table_x + 15, panel_y + 30,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SUBHEADER, "Button Mappings");

  // Table headers
  int row_y = panel_y + 55;
  int col1_x = table_x + 15;
  int col2_x = table_x + panel_w/2 + 10;

  vita2d_font_draw_text(font, col1_x, row_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL, "Vita");
  vita2d_font_draw_text(font, col2_x, row_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL, "PS5");
  row_y += 25;

  // Get mappings
  ButtonMapping mappings[20];
  int mapping_count = 0;
  get_button_mappings(context.config.controller_map_id, mappings, &mapping_count);

  // Draw first 8 mappings with zebra striping for better readability
  int row_h = 28;  // Increased from 24 for better spacing
  for (int i = 0; i < mapping_count && i < 8; i++) {
    int current_row_y = row_y + (i * row_h);

    // Zebra striping (alternating row backgrounds)
    uint32_t row_bg = (i % 2 == 0) ?
        RGBA8(0x30, 0x30, 0x38, 255) :  // Darker rows (even)
        RGBA8(0x38, 0x38, 0x40, 255);   // Lighter rows (odd)

    // Draw row background
    vita2d_draw_rectangle(table_x + 5, current_row_y - 16, panel_w - 10, row_h, row_bg);

    // Draw button names with padding
    vita2d_font_draw_text(font, col1_x, current_row_y,
                          UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SMALL, mappings[i].vita_button);
    vita2d_font_draw_text(font, col2_x, current_row_y,
                          UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SMALL, mappings[i].ps5_button);
  }

  // Vita diagram (right panel) - professional assets with white background
  int diagram_x = content_x + panel_w + panel_spacing;
  ui_draw_card_with_shadow(diagram_x, panel_y, panel_w, panel_h, 8, RGBA8(255, 255, 255, 255));

  vita2d_font_draw_text(font, diagram_x + 15, panel_y + 30,
                        RGBA8(0, 0, 0, 255), FONT_SIZE_SUBHEADER, "Vita Layout");

  // Draw Vita Front diagram (centered in card)
  if (vita_front) {
    int img_w = vita2d_texture_get_width(vita_front);
    int img_h = vita2d_texture_get_height(vita_front);

    // Scale to fit panel height while maintaining aspect ratio
    float max_h = panel_h - 60;  // Leave space for title
    float max_w = panel_w - 30;  // Leave margins
    float scale = 1.0f;

    if (img_h > max_h || img_w > max_w) {
      float scale_h = max_h / img_h;
      float scale_w = max_w / img_w;
      scale = (scale_h < scale_w) ? scale_h : scale_w;
    }

    int scaled_w = (int)(img_w * scale);
    int scaled_h = (int)(img_h * scale);
    int img_x = diagram_x + (panel_w - scaled_w) / 2;
    int img_y = panel_y + 50 + (panel_h - 50 - scaled_h) / 2;

    vita2d_draw_texture_scale(vita_front, img_x, img_y, scale, scale);
  }
}

/// Draw Settings Tab content (controller-related settings)
static void draw_controller_settings_tab(int content_x, int content_y, int content_w) {
  int item_h = 50;
  int item_spacing = 10;
  int y = content_y;

  // Circle Button Confirm toggle
  draw_toggle_switch(content_x + content_w - 70, y + (item_h - 30)/2, 60, 30,
                     context.config.circle_btn_confirm, controller_state.selected_item == 0);
  vita2d_font_draw_text(font, content_x + 15, y + item_h/2 + 6,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_BODY, "Circle Button Confirm");
  y += item_h + item_spacing;

  // Motion Controls - requires gyro backend integration (Phase 4)
  draw_toggle_switch(content_x + content_w - 70, y + (item_h - 30)/2, 60, 30,
                     false, controller_state.selected_item == 1);
  vita2d_font_draw_text(font, content_x + 15, y + item_h/2 + 6,
                        UI_COLOR_TEXT_TERTIARY, FONT_SIZE_BODY, "Motion Controls");
  vita2d_font_draw_text(font, content_x + content_w - 165, y + item_h/2 + 6,
                        UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL, "(Coming Soon)");
}

/// Main Controller Configuration screen with tabs
UIScreenType draw_controller_config_screen() {
  // Render particle background
  ui_particles_update();
  ui_particles_render();

  UIScreenType nav_screen;
  if (handle_global_nav_shortcuts(&nav_screen, true))
    return nav_screen;

  // Main content area (nav is overlay - content centered on full screen)
  int content_w = 800;  // Fixed width for content
  int content_x = (VITA_WIDTH - content_w) / 2;  // Center on screen
  int content_y = 100;

  // Controller title (centered on full screen width)
  const char* title = "Controller Configuration";
  int title_width = vita2d_font_text_width(font, FONT_SIZE_HEADER, title);
  int title_x = (VITA_WIDTH - title_width) / 2;
  int min_title_x = NAV_PILL_X + NAV_PILL_WIDTH + 20;
  if (title_x < min_title_x) title_x = min_title_x;
  vita2d_font_draw_text(font, title_x, 50, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_HEADER, title);

  // Tab bar
  int tab_bar_y = 70;
  int tab_bar_h = 40;
  draw_tab_bar(content_x, tab_bar_y, content_w, tab_bar_h,
               controller_tab_names, controller_tab_colors,
               CONTROLLER_TAB_COUNT, controller_state.current_tab);

  // Tab content area
  int tab_content_y = tab_bar_y + tab_bar_h + 20;
  int tab_content_w = content_w - 40;
  int tab_content_x = content_x + 20;

  // Draw current tab content
  switch (controller_state.current_tab) {
    case CONTROLLER_TAB_MAPPINGS:
      draw_controller_mappings_tab(tab_content_x, tab_content_y, tab_content_w);
      break;
    case CONTROLLER_TAB_SETTINGS:
      draw_controller_settings_tab(tab_content_x, tab_content_y, tab_content_w);
      break;
  }

  // Select button shows hints popup (different hint per tab)
  if (btn_pressed(SCE_CTRL_SELECT)) {
    const char* hint = (controller_state.current_tab == CONTROLLER_TAB_MAPPINGS) ?
      "L1/R1: Switch Tab | Left/Right: Change Scheme | Circle: Back" :
      "L1/R1: Switch Tab | Up/Down: Navigate | X: Toggle | Circle: Back";
    trigger_hints_popup(hint);
  }

  // === INPUT HANDLING ===

  // L1/R1: Switch tabs
  if (btn_pressed(SCE_CTRL_LTRIGGER)) {
    controller_state.current_tab = (controller_state.current_tab - 1 + CONTROLLER_TAB_COUNT) % CONTROLLER_TAB_COUNT;
    controller_state.selected_item = 0;
  } else if (btn_pressed(SCE_CTRL_RTRIGGER)) {
    controller_state.current_tab = (controller_state.current_tab + 1) % CONTROLLER_TAB_COUNT;
    controller_state.selected_item = 0;
  }

  // Tab-specific navigation
  if (controller_state.current_tab == CONTROLLER_TAB_MAPPINGS) {
    // Left/Right: Cycle through schemes
    if (btn_pressed(SCE_CTRL_LEFT)) {
      int current_id = context.config.controller_map_id;
      if (current_id == 0) {
        context.config.controller_map_id = 99;
      } else if (current_id == 25) {
        context.config.controller_map_id = 7;
      } else if (current_id == 99) {
        context.config.controller_map_id = 25;
      } else if (current_id > 0 && current_id <= 7) {
        context.config.controller_map_id = current_id - 1;
      }
      config_serialize(&context.config);
    } else if (btn_pressed(SCE_CTRL_RIGHT)) {
      int current_id = context.config.controller_map_id;
      if (current_id < 7) {
        context.config.controller_map_id = current_id + 1;
      } else if (current_id == 7) {
        context.config.controller_map_id = 25;
      } else if (current_id == 25) {
        context.config.controller_map_id = 99;
      } else {
        context.config.controller_map_id = 0;
      }
      config_serialize(&context.config);
    }
  } else if (controller_state.current_tab == CONTROLLER_TAB_SETTINGS) {
    // Up/Down: Navigate items (only when focused on content, not nav bar)
    int max_items = 2;
    if (current_focus != FOCUS_NAV_BAR) {
      if (btn_pressed(SCE_CTRL_UP)) {
        controller_state.selected_item = (controller_state.selected_item - 1 + max_items) % max_items;
      } else if (btn_pressed(SCE_CTRL_DOWN)) {
        controller_state.selected_item = (controller_state.selected_item + 1) % max_items;
      }
    }

    // X: Toggle selected item
    if (btn_pressed(SCE_CTRL_CROSS)) {
      if (controller_state.selected_item == 0) {
        // Circle button confirm toggle
        context.config.circle_btn_confirm = !context.config.circle_btn_confirm;
        start_toggle_animation(101, context.config.circle_btn_confirm);
        config_serialize(&context.config);
      }
      // Item 1 (Motion Controls) is not yet implemented - do nothing
    }
  }

  // Circle: Back to main menu
  if (btn_pressed(SCE_CTRL_CIRCLE)) {
    return UI_SCREEN_TYPE_MAIN;
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
bool draw_registration_dialog() {
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
bool draw_stream() {
  // Match ywnico: immediately return false, let video callback handle everything
  // UI loop will skip rendering when is_streaming is true
  if (context.stream.is_streaming) context.stream.is_streaming = false;
  return false;
}


/// Draw the "Waking up console..." screen with spinner animation
/// Waits indefinitely for console to wake, then auto-transitions to streaming
/// @return the next screen to show
UIScreenType draw_waking_screen() {
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
UIScreenType draw_reconnecting_screen() {
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
bool draw_messages() {
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

UIScreenType ui_screen_draw_main(void) {
  return draw_main_menu();
}

UIScreenType ui_screen_draw_settings(void) {
  return draw_settings();
}

UIScreenType ui_screen_draw_profile(void) {
  return draw_profile_screen();
}

UIScreenType ui_screen_draw_controller(void) {
  return draw_controller_config_screen();
}

UIScreenType ui_screen_draw_waking(void) {
  return draw_waking_screen();
}

UIScreenType ui_screen_draw_reconnecting(void) {
  return draw_reconnecting_screen();
}

bool ui_screen_draw_registration(void) {
  return draw_registration_dialog();
}

bool ui_screen_draw_stream(void) {
  return draw_stream();
}

bool ui_screen_draw_messages(void) {
  return draw_messages();
}
