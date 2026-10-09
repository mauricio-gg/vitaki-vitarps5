/**
 * @file ui_screens.c
 * @brief Screen rendering implementations for VitaRPS5
 *
 * All screen implementations extracted from ui.c (~2000 lines):
 * - Main menu with console cards
 * - Waking/connecting overlay
 * - Reconnecting overlay
 * - Registration dialog (PIN entry)
 * - Stream overlay
 * - Messages screen
 *
 * This is Phase 7 of the UI refactoring - the largest extraction.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <vita2d.h>
#include <psp2/ctrl.h>
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
