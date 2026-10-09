/**
 * @file ui_components.c
 * @brief Debug menu implementation for VitaRPS5 (VITARPS5_DEBUG_MENU builds)
 */

#include <stdio.h>

#include "context.h"
#include "ui/ui_components.h"
#include "ui/ui_internal.h"
#include "ui/ui_graphics.h"
#include "ui/ui_focus.h"
#include "ui/ui_console_cards.h"
#include "ui/ui_text.h"
#include "ui/ui_component.h"
#include "ui/ui_shapes.h"
#include "ui/ui_theme.h"
#include "host_feedback.h"
#include "video.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/kernel/clib.h>

// ============================================================================
// Internal State
// ============================================================================

// Debug menu configuration
const bool debug_menu_enabled = VITARPS5_DEBUG_MENU != 0;
const uint32_t DEBUG_MENU_COMBO_MASK = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_START;
const char *debug_menu_options[] = {
    "Show Remote Play error popup",
    "Simulate disconnect banner",
    "Trigger network unstable badge",
    "Spawn fake consoles (x12)",
};

// ============================================================================
// Debug Menu (VITARPS5_DEBUG_MENU must be enabled)
// ============================================================================

// Forward declare helper function
static void ensure_active_host_for_debug(void);

/** How long the debug menu's forced failure stays as the console's status message. */
#define DEBUG_FAILURE_HINT_DURATION_US (7 * 1000 * 1000ULL)

/**
 * Ensure there's an active host for debug actions
 */
static void ensure_active_host_for_debug(void) {
  if (context.active_host)
    return;
  for (int i = 0; i < MAX_CONTEXT_HOSTS; i++) {
    if (context.hosts[i]) {
      context.active_host = context.hosts[i];
      break;
    }
  }
}

/**
 * Apply selected debug menu action
 */
static void debug_menu_apply_action(int action_index) {
  if (!debug_menu_enabled)
    return;
  if (action_index < 0 || action_index >= DEBUG_MENU_OPTION_COUNT)
    return;

  switch (action_index) {
    case 0: {
      // Post a Remote Play failure through the real path: the hint opens the Could not connect
      // popup on Home.
      ensure_active_host_for_debug();
      if (context.active_host) {
        host_set_hint(context.active_host, "Remote Play already active on console", true,
                      DEBUG_FAILURE_HINT_DURATION_US);
        LOGD("Debug menu: forced Remote Play connection failure");
      } else {
        LOGE("Debug menu: no console to post a connection failure for");
      }
      break;
    }
    case 1: {
      // Simulate disconnect banner
      ensure_active_host_for_debug();
      uint64_t now_us = sceKernelGetProcessTimeWide();
      const uint64_t demo_duration_us = 4 * 1000 * 1000ULL;
      sceClibSnprintf(context.stream.disconnect_reason, sizeof(context.stream.disconnect_reason),
                      "Connection interrupted (debug)");
      context.stream.disconnect_banner_until_us = now_us + demo_duration_us;
      context.stream.next_stream_allowed_us = now_us + demo_duration_us;
      LOGD("Debug menu: simulated disconnect banner for %llums",
           (unsigned long long)(demo_duration_us / 1000ULL));
      break;
    }
    case 2: {
      // Trigger network unstable badge
      uint64_t now_us = sceKernelGetProcessTimeWide();
      const uint64_t alert_duration_us = 3 * 1000 * 1000ULL;
      context.stream.loss_alert_duration_us = alert_duration_us;
      context.stream.loss_alert_until_us = now_us + alert_duration_us;
      vitavideo_show_poor_net_indicator();
      LOGD("Debug menu: triggered network unstable indicator for %llums",
           (unsigned long long)(alert_duration_us / 1000ULL));
      break;
    }
    case 3: {
      // Spawn/remove fake consoles for carousel testing
      // Check if fakes already exist (look for our sentinel MAC prefix 0xDE, 0xAD)
      bool fakes_exist = false;
      for (int i = 0; i < MAX_CONTEXT_HOSTS; i++) {
        if (context.hosts[i] && context.hosts[i]->server_mac[0] == 0xDE &&
            context.hosts[i]->server_mac[1] == 0xAD) {
          fakes_exist = true;
          break;
        }
      }

      if (fakes_exist) {
        // Remove all fake hosts
        int removed = 0;
        for (int i = 0; i < MAX_CONTEXT_HOSTS; i++) {
          if (context.hosts[i] && context.hosts[i]->server_mac[0] == 0xDE &&
              context.hosts[i]->server_mac[1] == 0xAD) {
            host_free(context.hosts[i]);
            context.hosts[i] = NULL;
            removed++;
          }
        }
        update_context_hosts();
        ui_cards_update_cache(true);
        LOGD("Debug menu: removed %d fake consoles", removed);
      } else {
        // Spawn 12 fake consoles
        const char *names[] = {
            "PS5-Dorm-001", "PS5-Dorm-002", "PS5-Dorm-003", "PS5-Dorm-004",
            "PS5-Dorm-005", "PS5-Dorm-006", "PS5-Lounge",   "PS5-Library",
            "PS5-Lab",      "PS4-Room-A",   "PS4-Room-B",   "PS4-Room-C",
        };
        const char *ips[] = {
            "192.168.1.101", "192.168.1.102", "192.168.1.103", "192.168.1.104",
            "192.168.1.105", "192.168.1.106", "192.168.1.107", "192.168.1.108",
            "192.168.1.109", "192.168.1.110", "192.168.1.111", "192.168.1.112",
        };

        int spawned = 0;
        for (int n = 0; n < 12; n++) {
          // Find empty slot
          int slot = -1;
          for (int i = 0; i < MAX_CONTEXT_HOSTS; i++) {
            if (!context.hosts[i]) {
              slot = i;
              break;
            }
          }
          if (slot < 0)
            break;  // No room

          VitaChiakiHost *h = (VitaChiakiHost *)malloc(sizeof(VitaChiakiHost));
          if (!h)
            break;
          memset(h, 0, sizeof(VitaChiakiHost));

          h->type = DISCOVERED;
          h->source = VITA_HOST_SOURCE_LOCAL_DISCOVERY;
          // PS4 for last 3, PS5 for first 9
          h->target = (n < 9) ? CHIAKI_TARGET_PS5_1 : CHIAKI_TARGET_PS4_10;

          // Sentinel MAC: DE:AD:xx:xx:xx:xx
          h->server_mac[0] = 0xDE;
          h->server_mac[1] = 0xAD;
          h->server_mac[2] = 0x00;
          h->server_mac[3] = 0x00;
          h->server_mac[4] = 0x00;
          h->server_mac[5] = (uint8_t)(n + 1);

          snprintf(h->hostname, sizeof(h->hostname), "%s", ips[n]);
          snprintf(h->display_name, sizeof(h->display_name), "%s", names[n]);

          // Create discovery state
          ChiakiDiscoveryHost *ds = (ChiakiDiscoveryHost *)malloc(sizeof(ChiakiDiscoveryHost));
          if (!ds) {
            free(h);
            break;
          }
          memset(ds, 0, sizeof(ChiakiDiscoveryHost));

          // Alternate between Ready and Standby
          ds->state = (n % 3 == 2) ? CHIAKI_DISCOVERY_HOST_STATE_STANDBY
                                   : CHIAKI_DISCOVERY_HOST_STATE_READY;
          ds->host_name = strdup(names[n]);
          ds->host_addr = strdup(ips[n]);
          h->discovery_state = ds;
          h->discovery_state_snapshot = ds->state;

          h->last_discovery_seen_us = sceKernelGetProcessTimeWide();
          h->status_hint[0] = '\0';
          h->status_hint_expire_us = 0;
          h->status_hint_is_error = false;

          context.hosts[slot] = h;
          context.num_hosts++;
          spawned++;
        }

        ui_cards_update_cache(true);
        LOGD("Debug menu: spawned %d fake consoles", spawned);
      }
      break;
    }
    default:
      break;
  }

  ui_debug_close();
}

/**
 * Open the debug menu
 */
void ui_debug_open(void) {
  if (!debug_menu_enabled)
    return;
  if (context.ui_state.debug_menu_active)
    return;

  context.ui_state.debug_menu_active = true;
  context.ui_state.debug_menu_selection = 0;

  // Block inputs
  uint32_t *button_block_mask = ui_input_get_button_block_mask_ptr();
  bool *touch_block_active = ui_input_get_touch_block_active_ptr();
  *button_block_mask |= context.ui_state.button_state;
  *touch_block_active = true;

  // Push modal focus once per debug menu activation.
  if (!context.ui_state.debug_menu_modal_pushed) {
    ui_focus_push_modal();
    context.ui_state.debug_menu_modal_pushed = true;
  }
}

/**
 * Close the debug menu
 */
void ui_debug_close(void) {
  if (!context.ui_state.debug_menu_active)
    return;

  context.ui_state.debug_menu_active = false;
  context.ui_state.debug_menu_selection = 0;

  // Block inputs
  uint32_t *button_block_mask = ui_input_get_button_block_mask_ptr();
  bool *touch_block_active = ui_input_get_touch_block_active_ptr();
  *button_block_mask |= context.ui_state.button_state;
  *touch_block_active = true;

  // Pop only if this menu owns a modal push.
  if (context.ui_state.debug_menu_modal_pushed) {
    ui_focus_pop_modal();
    context.ui_state.debug_menu_modal_pushed = false;
  }
}

/** Debug menu panel size, in pixels. */
#define DEBUG_MENU_PANEL_W 560
#define DEBUG_MENU_PANEL_H 304
/** Top of the title text box, below the panel's top edge. */
#define DEBUG_MENU_TITLE_TOP UI_S3
/** Top of the first option row, below the panel's top edge. */
#define DEBUG_MENU_LIST_TOP 72
/** Gap between option rows. */
#define DEBUG_MENU_ROW_GAP 2
/** Space between the option rows and the panel's left and right edges. */
#define DEBUG_MENU_ROW_INSET UI_S4
/** Space between the row label and the row's left edge. */
#define DEBUG_MENU_ROW_PAD UI_S2
/** Top of the hint text box, above the panel's bottom edge. */
#define DEBUG_MENU_HINT_BOTTOM_GAP 28

/**
 * Render the debug menu in the popup style: scrim, panel with border, T28 title, focus-bar rows
 * in T20, T14 hint.
 */
void ui_debug_render(void) {
  if (!context.ui_state.debug_menu_active)
    return;

  vita2d_draw_rectangle(0.0f, 0.0f, (float)VITA_WIDTH, (float)VITA_HEIGHT, UI_SCRIM);

  const UiRect panel = {(VITA_WIDTH - DEBUG_MENU_PANEL_W) / 2,
                        (VITA_HEIGHT - DEBUG_MENU_PANEL_H) / 2, DEBUG_MENU_PANEL_W,
                        DEBUG_MENU_PANEL_H};
  ui_shape9_draw(UI_SHAPE9_MD, panel, UI_PANEL);
  ui_shape9_draw(UI_SHAPE9_MD_BORDER, panel, UI_LINE);

  const char *title = "Debug Actions";
  ui_text_draw_face_centered_v(UI_FACE_T28,
                               panel.x + (panel.w - ui_text_face_width(UI_FACE_T28, title)) / 2,
                               panel.y + DEBUG_MENU_TITLE_TOP, UI_T28_LINE, UI_TEXT, title);

  const int row_x = panel.x + DEBUG_MENU_ROW_INSET;
  const int row_w = panel.w - 2 * DEBUG_MENU_ROW_INSET;
  for (int i = 0; i < DEBUG_MENU_OPTION_COUNT; i++) {
    const bool focused = i == context.ui_state.debug_menu_selection;
    const int row_y = panel.y + DEBUG_MENU_LIST_TOP + i * (UI_ROW_H + DEBUG_MENU_ROW_GAP);
    const int text_x = row_x + DEBUG_MENU_ROW_PAD;
    if (focused) {
      const UiRect label = {text_x, row_y + (UI_ROW_H - UI_T20_LINE) / 2,
                            ui_text_face_width(UI_FACE_T20, debug_menu_options[i]), UI_T20_LINE};
      ui_glow_draw_rect(label, UI_ROW_GLOW,
                        ui_color_scale_alpha(UI_GLOW, (float)UI_ROW_GLOW_PCT / 100.0f));
      ui_shape3_draw(UI_SHAPE3_BAR_48, row_x, row_y, row_w, UI_FILL_FOCUS);
    } else {
      vita2d_draw_rectangle((float)row_x, (float)(row_y + UI_ROW_H - UI_LW1), (float)row_w,
                            (float)UI_LW1, UI_LINE_FAINT);
    }
    ui_text_draw_face_centered_v(UI_FACE_T20, text_x, row_y, UI_ROW_H,
                                 focused ? UI_TEXT : UI_TEXT_2, debug_menu_options[i]);
  }

  const char *hint = "D-Pad: Select  |  X: Trigger  |  Circle: Close";
  ui_text_draw_face_centered_v(
      UI_FACE_T14, panel.x + (panel.w - ui_text_face_width(UI_FACE_T14, hint)) / 2,
      panel.y + panel.h - DEBUG_MENU_HINT_BOTTOM_GAP, UI_T14_LINE, UI_TEXT_3, hint);
}

/**
 * Handle input for debug menu
 */
void ui_debug_handle_input(void) {
  if (!context.ui_state.debug_menu_active)
    return;

  uint32_t buttons = context.ui_state.button_state;
  uint32_t prev_buttons = context.ui_state.old_button_state;

  // Navigate up
  if ((buttons & SCE_CTRL_UP) && !(prev_buttons & SCE_CTRL_UP)) {
    context.ui_state.debug_menu_selection--;
    if (context.ui_state.debug_menu_selection < 0)
      context.ui_state.debug_menu_selection = DEBUG_MENU_OPTION_COUNT - 1;
  }
  // Navigate down
  else if ((buttons & SCE_CTRL_DOWN) && !(prev_buttons & SCE_CTRL_DOWN)) {
    context.ui_state.debug_menu_selection++;
    if (context.ui_state.debug_menu_selection >= DEBUG_MENU_OPTION_COUNT)
      context.ui_state.debug_menu_selection = 0;
  }
  // Trigger action
  else if ((buttons & SCE_CTRL_CROSS) && !(prev_buttons & SCE_CTRL_CROSS)) {
    debug_menu_apply_action(context.ui_state.debug_menu_selection);
  }
  // Close menu
  else if ((buttons & SCE_CTRL_CIRCLE) && !(prev_buttons & SCE_CTRL_CIRCLE)) {
    ui_debug_close();
  }
}

// ============================================================================
// Legacy Compatibility Wrappers (for ui.c internal use)
// ============================================================================

void open_debug_menu(void) {
  ui_debug_open();
}

void render_debug_menu(void) {
  ui_debug_render();
}

void handle_debug_menu_input(void) {
  ui_debug_handle_input();
}
