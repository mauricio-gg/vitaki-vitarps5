/**
 * @file ui.c
 * @brief VitaRPS5 UI Coordinator - Main rendering loop and initialization
 *
 * This file serves as the central coordinator for VitaRPS5's modular UI system.
 * It orchestrates the rendering pipeline, manages the main UI loop, and dispatches
 * to specialized UI modules for specific functionality.
 *
 * Architecture:
 * - ui_graphics.c: Low-level drawing primitives and shapes
 * - ui_animation.c: Animation timing utilities
 * - ui_input.c: Button/touch input handling and gesture detection
 * - ui_state.c: UI state management and transitions
 * - ui_components.c: Reusable UI widgets (toggles, dropdowns, popups)
 * - ui_console_cards.c: Console selection card grid
 * - ui_screens.c: Full-screen rendering (main, settings, profile, etc.)
 *
 * This coordinator:
 * 1. Initializes vita2d, fonts, textures, and all UI modules
 * 2. Runs the main rendering loop
 * 3. Dispatches input to appropriate handlers
 * 4. Routes rendering to the correct screen based on current state
 * 5. Manages global overlays (debug menu, error popups, hints)
 *
 * All UI constants, types, and shared state are defined in ui/ui_*.h headers.
 */

#include <sys/param.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/message_dialog.h>
#include <psp2/registrymgr.h>
#include <psp2/ime_dialog.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <chiaki/base64.h>

#include "context.h"
#include "debug_tools.h"
#include "host.h"
#include "ui.h"
#include "util.h"
#include "video.h"
#include "host_constants.h"
#include "host_metrics.h"
#include "host_quit.h"
#include "psn_auth.h"
#include "psn_remote.h"
#include "ui/ui_graphics.h"
#include "ui/ui_animation.h"
#include "ui/ui_background.h"
#include "ui/ui_draw_stats.h"
#include "ui/ui_freeze.h"
#include "ui/ui_list_popup.h"
#include "ui/ui_pair_popup.h"
#include "ui/ui_pin.h"
#include "ui/ui_result_popup.h"
#include "ui/ui_input.h"
#include "ui/ui_state.h"
#include "ui/ui_components.h"
#include "ui/ui_focus.h"
#include "ui/ui_internal.h"
#include "ui/ui_controller_diagram.h"
#include "ui/ui_text.h"
#include "ui/ui_component.h"
#include "ui/ui_connecting.h"
#include "ui/ui_home.h"
#include "ui/ui_room_icons.h"
#include "ui/ui_controller_page.h"
#include "ui/ui_profile.h"
#include "ui/ui_settings.h"
#include "ui/ui_settings_actions.h"
#include "ui/ui_shapes.h"
#include "ui/ui_toast.h"

vita2d_texture *img_ps4;

// VitaRPS5 UI textures
vita2d_texture *symbol_triangle, *symbol_circle, *symbol_ex, *symbol_square;
vita2d_texture *icon_play, *icon_settings;
vita2d_texture *vita_rps5_logo;
vita2d_texture *ps5_logo;

// Input state (managed by ui_input.c - accessed via pointers for direct manipulation)
static uint32_t *button_block_mask = NULL;

// State management convenience macros (for legacy code compatibility)
#define waking_wait_for_stream_us ui_state_get_waking_wait_for_stream_us()
#define SET_waking_wait_for_stream_us(val) ui_state_set_waking_wait_for_stream_us(val)
#define connection_overlay_active ui_connection_overlay_active()
#define connection_overlay_stage ui_connection_stage()
#define connection_thread_id (-1)  // Thread ID access not needed in ui.c (managed by ui_state.c)

// Console card system (updated per UI spec)
// Console card constants moved to ui_constants.h

// ConsoleCardInfo type moved to ui_types.h
// selected_console_index moved to ui_console_cards.c
// Console card cache moved to ui_console_cards.c
// CardFocusAnimState moved to ui_console_cards.c

// Component functions moved to ui_components.c (accessible via ui_internal.h)

// Debug menu configuration moved to ui_components.c

// Connection overlay, cooldown, thread management, and text cache moved to ui_state.c

// FocusArea and UIHostAction enums moved to ui_types.h (included via ui_state.h)

/// Types of screens that can be rendered
// UIScreenType enum moved to ui_types.h (included via ui_state.h)

// btn_pressed() and block_inputs_for_transition() moved to ui_input.c

// Error popup and debug menu functions moved to ui_components.c

#if VITARPS5_DEBUG_TOOLS
/** Name logged with the draw counts of a frame drawn over a frozen popup background. */
#define DRAW_STATS_POPUP_NAME "popup"

/** draw_stats_screen_name() - Name logged with the draw counts of @screen, or NULL for a screen
 * that is not counted. */
static const char *draw_stats_screen_name(UIScreenType screen) {
  switch (screen) {
    case UI_SCREEN_TYPE_MAIN:
      return "home";
    case UI_SCREEN_TYPE_WAKING:
      return "connecting";
    case UI_SCREEN_TYPE_RECONNECTING:
      return "reconnecting";
    case UI_SCREEN_TYPE_SETTINGS:
      return "settings";
    case UI_SCREEN_TYPE_PROFILE:
      return "profile";
    case UI_SCREEN_TYPE_CONTROLLER:
      return "controller";
    case UI_SCREEN_TYPE_REGISTER_HOST:
      return "pin";
    default:
      return NULL;
  }
}
#endif

// Debug menu render and input functions moved to ui_components.c

// ============================================================================
// ANIMATION HELPERS
// ============================================================================
// Animation helper functions (lerp, ease_in_out_cubic) moved to ui_internal.h
// Toggle animation functions moved to ui_components.c

// ============================================================================
// REUSABLE UI COMPONENTS
// ============================================================================
// Widget drawing functions (toggle, dropdown, tabs, status_dot, section_header) moved to
// ui_components.c StatusType enum moved to ui_components.h as UIStatusType

// ============================================================================
// CONSOLE CARDS
// ============================================================================
// Map VitaChiakiHost to ConsoleCardInfo moved to ui_console_cards.c (ui_cards_map_host)
// Console card functions moved to ui_console_cards.c (ui_cards_*)

// ============================================================================
// TEXTURE LOADING
// ============================================================================

/**
 * load_textures() - Load all UI textures and assets into memory
 *
 * Loads console icons, UI symbols, navigation icons, and other graphical
 * assets required for rendering the VitaRPS5 interface. Called once during
 * UI initialization.
 *
 * Note: Textures are loaded from app0:/assets/ directory as defined in
 * ui_constants.h. Failed loads result in NULL texture pointers which must
 * be checked before rendering.
 */
void load_textures() {
  img_ps4 = ui_load_png_linear(IMG_PS4_PATH);

  // Load VitaRPS5 UI assets
  symbol_triangle = ui_load_png_linear("app0:/assets/symbol_triangle.png");
  symbol_circle = ui_load_png_linear("app0:/assets/symbol_circle.png");
  symbol_ex = ui_load_png_linear("app0:/assets/symbol_ex.png");
  symbol_square = ui_load_png_linear("app0:/assets/symbol_square.png");

  // Load the Home category icons
  icon_play = ui_load_png_linear("app0:/assets/icon_play.png");
  icon_settings = ui_load_png_linear("app0:/assets/icon_settings.png");

  // Load new professional assets
  vita_rps5_logo = ui_load_png_linear("app0:/assets/Vita_RPS5_Logo.png");
  ps5_logo = ui_load_png_linear("app0:/assets/PS5_logo.png");

  // Controller diagram textures are managed separately by the controller
  // diagram module, so load_textures() does not load them here.
}

// ============================================================================
// LEGACY TOUCH HELPERS
// ============================================================================
// TODO: Move to ui_input.c in future cleanup

/**
 * is_touched() - Check if a rectangular region is currently touched
 * @param x: Left edge of region
 * @param y: Top edge of region
 * @param width: Width of region
 * @param height: Height of region
 *
 * Returns true if any active touch point falls within the specified region.
 * This is a legacy helper that should be replaced with ui_input.c functions.
 *
 * Returns: true if region is touched, false otherwise
 */
bool is_touched(int x, int y, int width, int height) {
  SceTouchData *tdf = &(context.ui_state.touch_state_front);
  if (!tdf) {
    return false;
  }
  // TODO: Do the coordinate systems really match?
  return tdf->report->x > x && tdf->report->x <= x + width && tdf->report->y > y &&
         tdf->report->y <= y + height;
}

// is_point_in_circle() and is_point_in_rect() moved to ui_input.c

// ============================================================================
// Touch input handler moved to ui_screens.c
// ============================================================================

// ============================================================================
// PSN ACCOUNT INITIALIZATION
// ============================================================================

static bool load_psn_id_from_registry(bool force_reload) {
  if (!force_reload && context.config.psn_account_id && strlen(context.config.psn_account_id) > 0) {
    return true;
  }

  char acc_id_buf[8];
  memset(acc_id_buf, 0, sizeof(acc_id_buf));

  int reg_result = sceRegMgrGetKeyBin("/CONFIG/NP/", "account_id", acc_id_buf, sizeof(acc_id_buf));
  if (reg_result < 0) {
    LOGE("Failed to read PSN account_id from registry: 0x%08X", reg_result);
    return false;
  }

  int b64_strlen = get_base64_size(sizeof(acc_id_buf));
  char *new_psn_id = (char *)malloc((size_t)b64_strlen + 1);
  if (!new_psn_id) {
    LOGE("Failed to allocate memory for PSN account ID");
    return false;
  }

  new_psn_id[b64_strlen] = '\0';
  chiaki_base64_encode(acc_id_buf, sizeof(acc_id_buf), new_psn_id,
                       get_base64_size(sizeof(acc_id_buf)));

  if (context.config.psn_account_id) {
    free(context.config.psn_account_id);
  }
  context.config.psn_account_id = new_psn_id;
  LOGD("Loaded PSN account ID (base64, len=%d)", (int)strlen(new_psn_id));
  return true;
}

/**
 * load_psn_id_if_needed() - Load PSN account ID from Vita registry if missing.
 */
void load_psn_id_if_needed() {
  load_psn_id_from_registry(false);
}

/**
 * ui_reload_psn_account_id() - Force refresh PSN account ID from Vita registry.
 */
bool ui_reload_psn_account_id(void) {
  return load_psn_id_from_registry(true);
}

// ============================================================================
// SCREEN RENDERING
// ============================================================================
// All screen rendering functions moved to ui_screens.c:
// - ui_screen_draw_main()
// - ui_screen_draw_waking()
// - ui_screen_draw_reconnecting()
// ============================================================================

// ============================================================================
// UI INITIALIZATION
// ============================================================================

/**
 * init_ui() - Initialize the VitaRPS5 UI system
 *
 * Performs one-time initialization of the UI subsystem:
 * 1. Initializes vita2d graphics library
 * 2. Loads all textures and fonts
 * 3. Initializes touch screen input
 * 4. Configures confirm/cancel button layout
 * 5. Initializes all UI modules (input, screens, state, background, cards)
 *
 * Must be called before draw_ui() main loop.
 */
void init_ui() {
  int vita2d_init_ret =
      vita2d_init_advanced_with_msaa(SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE, SCE_GXM_MULTISAMPLE_4X);
  if (vita2d_init_ret < 0) {
    sceClibPrintf("[WARN] MSAA init failed (0x%08x), falling back to default init\n",
                  vita2d_init_ret);
    vita2d_init();
  }
  vita2d_set_clear_color(RGBA8(0x40, 0x40, 0x40, 0xFF));
  load_textures();
  ui_background_init();  // Build the wave background geometry
  ui_cards_init();       // Initialize console card system

  /* Initialize text helper: measures per-face metrics from the loaded fonts.
   * Must happen after font load and before the first draw_ui() frame. */
  vita2d_font *font = vita2d_load_font_file("app0:/assets/fonts/Roboto-Regular.ttf");
  vita2d_font *font_light = vita2d_load_font_file("app0:/assets/fonts/Roboto-Light.ttf");
  ui_text_init(font, font_light);
  ui_glow_init();
  ui_shapes_init();
  ui_room_icons_init();
  ui_result_popup_init();  // shared by Home and the PIN screen, so loaded before either
  ui_home_init();
  ui_connecting_init();
  ui_toast_init();
  ui_settings_init();
  ui_profile_init();
  ui_controller_page_init();
  ui_pin_init();
  ui_list_popup_init();
  ui_pair_popup_init();

  vita2d_set_vblank_wait(true);

  // Initialize touch screen
  sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
  sceTouchEnableTouchForce(SCE_TOUCH_PORT_FRONT);

  // Initialize UI modules
  ui_input_init();
  ui_state_init();
  ui_focus_init();  // Initialize centralized focus manager (Phase 1)

  // Get pointers to input state for direct manipulation (legacy compatibility)
  button_block_mask = ui_input_get_button_block_mask_ptr();
}

// ============================================================================
// MAIN UI LOOP
// ============================================================================

/**
 * draw_ui() - Main UI rendering and event loop
 *
 * Infinite loop that:
 * 1. Reads controller and touch input
 * 2. Handles global popups (error, debug menu, hints)
 * 3. Dispatches to the appropriate screen renderer
 * 4. Renders navigation overlay and global UI elements
 * 5. Swaps buffers and updates display
 *
 * This function never returns - it runs for the lifetime of the application.
 * Streaming mode bypasses all rendering to minimize latency.
 */
void draw_ui() {
  init_ui();
  SceCtrlData ctrl;
  memset(&ctrl, 0, sizeof(ctrl));

  UIScreenType screen = UI_SCREEN_TYPE_MAIN;
  /* Screen drawn on the previous frame; lets Home reset the focus manager and the old
   * sidebar when it becomes active again. Starts as NONE so the first frame counts as entry. */
  UIScreenType drawn_screen = UI_SCREEN_TYPE_NONE;
  context.ui_state.debug_menu_active = false;
  context.ui_state.debug_menu_modal_pushed = false;
  context.ui_state.debug_menu_selection = 0;

  load_psn_id_if_needed();
  time_t startup_t = time(NULL);
  uint64_t startup_unix = 0;
  if (startup_t != (time_t)-1) {
    startup_unix = (uint64_t)startup_t;
    /* psn_remote_refresh_hosts() refreshes the OAuth token, fetches the PSN
     * device list, and persists the config. It is a no-op when PSN internet
     * mode is disabled. Doing this at startup means the user does not have to
     * navigate to Profile -> Connection card and press X to see their PS5/PS4. */
    psn_remote_refresh_hosts();
    /* Drain any token refresh that happened but didn't persist (e.g. host
     * fetch failed after a successful token refresh). */
    if (context.config_persist_pending) {
      if (!config_serialize(&context.config))
        CHIAKI_LOGW(&(context.log), "PSN auth: failed to persist refreshed token at startup");
      context.config_persist_pending = false;
    }
  } else {
    CHIAKI_LOGW(&(context.log), "PSN auth: skipping startup host refresh — system clock not set");
  }

  /*
   * Glyph atlas warm-up flag: set once here so the first main-loop iteration
   * runs the prewarm pass inside the main drawing pair (after
   * vita2d_start_drawing / vita2d_clear_screen, before the background draw).
   * Keeping it inside the main pair avoids opening a second GXM scene in the
   * same frame, which would risk a GXM assertion or scene corruption.
   */
  int ui_text_prewarm_pending = ui_text_needs_prewarm();

  while (true) {
    // --- Deferred session finalization (join + fini on UI thread) ---
    // Must run BEFORE input processing to prevent reconnect races
    if (context.stream.session_finalize_pending) {
      host_finalize_deferred_session();
    }

#if VITARPS5_DEBUG_TOOLS
    // GH #275: widget tap -> resync, and the post-resync abs report.
    debug_tools_ui_tick();
#endif

    // --- RP_IN_USE single auto-retry (armed by host_handle_quit_event in
    // host_quit.c) --- Must run on this UI main-loop thread, never on the
    // chiaki event callback thread: that thread only arms the flag, the
    // actual reconnect kickoff happens here. Gated on screen == MAIN so the
    // retry never hijacks Settings/Profile/PIN-entry/Controller
    // config if the user navigated away while the timer was counting down;
    // rp_in_use_retry_pending is left armed (not cleared) whenever the
    // connect can't actually be attempted yet, so it fires later once the
    // user returns to MAIN or the in-flight connection finishes.
    if (context.stream.rp_in_use_retry_pending && screen == UI_SCREEN_TYPE_MAIN &&
        sceKernelGetProcessTimeWide() >= context.stream.rp_in_use_retry_at_us) {
      bool connection_busy = context.stream.session_init || ui_state_connection_thread_active();
      if (context.active_host && !connection_busy) {
        context.stream.rp_in_use_retry_pending = false;
        context.stream.rp_in_use_retry_used = true;
        // Restore the PSN-vs-LAN choice snapshotted when the retry was armed
        // (host_quit.c) -- host_stream() consumes and clears force_psn_holepunch
        // on every connect attempt, so it must be re-applied here.
        context.stream.force_psn_holepunch = context.stream.rp_in_use_retry_psn_holepunch;
        ui_connection_begin(UI_CONNECTION_STAGE_CONNECTING);
        if (start_connection_thread(context.active_host)) {
          screen = UI_SCREEN_TYPE_WAKING;
        } else {
          /* Thread never started -- clear the flag so it cannot bleed into
           * the next connection attempt. */
          context.stream.force_psn_holepunch = false;
          ui_connection_cancel();
        }
      }
    }

    // --- Hard-fallback recovery connect (GH #272, armed by host_handle_quit_event in
    // host_quit.c) --- Runs on this UI thread, never the dying session's thread: that thread only
    // schedules the attempt. Not gated on screen == MAIN (the screen is RECONNECTING for the whole
    // recovery) and never switches to the WAKING screen. Waits until the old session has been
    // joined and finalized (session_finalize_pending cleared by the block above) so the new
    // connect can never overlap, or be finalized as, the old session.
    if (context.stream.recovery_active && context.stream.loss_retry_pending &&
        !context.stream.session_finalize_pending && !context.stream.session_init &&
        !ui_state_connection_thread_active() &&
        sceKernelGetProcessTimeWide() >= context.stream.loss_retry_ready_us) {
      context.stream.loss_retry_pending = false;
      if (!context.active_host) {
        host_recovery_abort("no active host for fallback connect");
      } else {
        // Restores the PSN-vs-LAN choice of the connect that just ended (host_stream()
        // consumes force_psn_holepunch every call).
        context.stream.force_psn_holepunch = context.stream.last_connect_used_psn_holepunch;
        LOGD("Restarting stream after transport/packet-loss fallback attempt %u/%u at %u kbps",
             context.stream.loss_retry_attempts, LOSS_RETRY_MAX_ATTEMPTS,
             context.stream.recovery_bitrate_kbps);
        if (!start_connection_thread(context.active_host)) {
          context.stream.force_psn_holepunch = false;
          host_recovery_abort("could not start connection thread for fallback connect");
        }
      }
    }

    /* Drain any pending config persist on every idle frame. config_persist_pending
     * can be set by any refresh path (startup, idle timer, pre-stream token check),
     * so draining it here — outside the 60s gate — ensures a power-cycle right after
     * any refresh still saves the new token. */
    if (!context.stream.is_streaming && context.config_persist_pending) {
      if (!config_serialize(&context.config))
        CHIAKI_LOGW(&(context.log), "PSN auth: failed to persist refreshed token");
      context.config_persist_pending = false;
    }

    /* Proactively refresh PSN token once per minute while idle so it never
     * expires unnoticed between user actions. Skip during streaming to avoid
     * network contention with the media path. */
    if (!context.stream.is_streaming) {
      static uint64_t last_token_check_unix = 0;
      time_t t = time(NULL);
      if (t != (time_t)-1) {
        uint64_t now_unix = (uint64_t)t;
        if (last_token_check_unix == 0)
          last_token_check_unix = startup_unix;
        if (now_unix - last_token_check_unix >= 60) {
          last_token_check_unix = now_unix;
          psn_auth_refresh_token_if_needed(now_unix, false);
        }
      }
    }

    // Always read controller input - input thread uses Ext2 variant to access controller
    // independently
    if (!sceCtrlReadBufferPositive(0, &ctrl, 1)) {
      // Try again...
      LOGE("Failed to get controller state");
      continue;
    }
    context.ui_state.old_button_state = context.ui_state.button_state;
    context.ui_state.button_state = ctrl.buttons;
    *button_block_mask &= context.ui_state.button_state;

    // Get current touch state
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &(context.ui_state.touch_state_front), 1);

    handle_debug_menu_input();

    if (debug_menu_enabled && !context.stream.is_streaming && !context.ui_state.debug_menu_active) {
      if ((context.ui_state.button_state & DEBUG_MENU_COMBO_MASK) == DEBUG_MENU_COMBO_MASK &&
          (context.ui_state.old_button_state & DEBUG_MENU_COMBO_MASK) != DEBUG_MENU_COMBO_MASK) {
        open_debug_menu();
      }
    }

    // Skip ALL rendering when streaming - match ywnico pattern
    if (!context.stream.is_streaming) {
      if (context.stream.reconnect_overlay_active) {
        screen = UI_SCREEN_TYPE_RECONNECTING;
      } else if (screen == UI_SCREEN_TYPE_RECONNECTING) {
        screen = UI_SCREEN_TYPE_MAIN;
      }

      ui_input_update_snapshot();

      /*
       * The background resets vita2d's pool and renders its blur target (a scene of its own)
       * before the main scene opens, so the main scene must not call vita2d_start_drawing(),
       * which would reset the pool again. While a popup is open the screen behind it is a frozen
       * copy (ui_freeze.h): the wave is neither prepared nor drawn, only the pool is reset.
       */
      const bool frozen = ui_freeze_is_ready();
      if (frozen) {
        vita2d_pool_reset();
      } else {
        ui_background_prepare(screen == UI_SCREEN_TYPE_WAKING ||
                              screen == UI_SCREEN_TYPE_RECONNECTING);
      }
      vita2d_start_drawing_advanced(NULL, 0);
      vita2d_clear_screen();

      /*
       * One-shot atlas prewarm: runs inside the main drawing pair so there is
       * only ever one vita2d_start_drawing / vita2d_end_drawing open at a time.
       * Prewarm draws are at alpha=0 and off-screen (UI_FONT_PREWARM_OFFSCREEN_X/Y)
       * so they produce no visible output even on the first rendered frame.
       */
      if (ui_text_prewarm_pending) {
        ui_text_prewarm();
        ui_text_prewarm_pending = 0;
        LOGD("PIPE/UI_PREWARM_DONE us=%llu", (unsigned long long)sceKernelGetProcessTimeWide());
      }

      UI_DRAW_STATS_FRAME_BEGIN();

      // Wave background under every screen; it updates at half rate while connecting. Behind a
      // popup the frozen copy of the screen stands in for the wave and everything on it.
      if (frozen) {
        ui_freeze_draw();
      } else {
        ui_background_draw(screen == UI_SCREEN_TYPE_WAKING ||
                           screen == UI_SCREEN_TYPE_RECONNECTING);
      }

      UIScreenType prev_screen = screen;
      UIScreenType next_screen = screen;

      // Render the current screen
      if (screen == UI_SCREEN_TYPE_MAIN) {
        if (drawn_screen != UI_SCREEN_TYPE_MAIN)
          ui_home_on_enter();
        next_screen = ui_screen_draw_main();
      } else if (screen == UI_SCREEN_TYPE_REGISTER_HOST) {
        if (drawn_screen != UI_SCREEN_TYPE_REGISTER_HOST)
          ui_pin_on_enter();
        next_screen = ui_pin_frame();
      } else if (screen == UI_SCREEN_TYPE_WAKING) {
        next_screen = ui_screen_draw_waking();
      } else if (screen == UI_SCREEN_TYPE_RECONNECTING) {
        next_screen = ui_screen_draw_reconnecting();
      } else if (screen == UI_SCREEN_TYPE_SETTINGS) {
        next_screen = ui_settings_frame();
      } else if (screen == UI_SCREEN_TYPE_PROFILE) {
        next_screen = ui_profile_frame();
      } else if (screen == UI_SCREEN_TYPE_CONTROLLER) {
        next_screen = ui_controller_page_frame();
      }

      if (next_screen != prev_screen) {
        block_inputs_for_transition();
      }
      drawn_screen = prev_screen;
      screen = next_screen;

      render_debug_menu();
      /* A freeze belongs to the screen that opened the popup: leaving it releases the freeze. */
      if (next_screen != prev_screen)
        ui_freeze_release();
      UI_DRAW_STATS_FRAME_END(frozen ? DRAW_STATS_POPUP_NAME : draw_stats_screen_name(prev_screen));
      vita2d_end_drawing();
      vita2d_common_dialog_update();
      vita2d_swap_buffers();
      ui_freeze_frame_end();
    } else {
      // Streaming active — render decoded frames from the UI thread.
      // This decouples GPU display from the Takion network receive thread,
      // freeing ~15-20ms per frame on the decode path.
      if (!vita_video_render_latest_frame()) {
        sceKernelDelayThread(1000);  // 1ms sleep to avoid busy-spin
      }
      // Metrics update runs here so the 1Hz sceNetCtlInetGetInfo probe and
      // diag_mutex trylock are off the Takion recv thread entirely.
      host_metrics_update_latency();
    }
  }
}
