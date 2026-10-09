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
#include <stdlib.h>
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
#include "psn_startup_refresh.h"
#include "ui/ui_graphics.h"
#include "ui/ui_animation.h"
#include "ui/ui_asset_preload.h"
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
#include "ui/ui_splash.h"
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
 * load_textures() - Load all UI textures and assets into memory (except the logo)
 *
 * Loads console icons, UI symbols, navigation icons, and other graphical
 * assets required for rendering the VitaRPS5 interface. Called once during
 * UI initialization.
 *
 * The VitaRPS5 logo is loaded earlier, by init_ui(), because the splash draws it first.
 *
 * Note: Textures are loaded from app0:/assets/ directory as defined in
 * ui_constants.h. Failed loads result in NULL texture pointers which must
 * be checked before rendering.
 */
static void load_textures(void) {
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
// UI INITIALIZATION AND START-UP SPLASH
// ============================================================================

/** One piece of start-up work. Steps run in table order on the main thread. */
typedef struct {
  const char *name; /**< Name in the PIPE/SPLASH_STEP log line. */
  void (*run)(void);
} StartupStep;

/** Unix time at the startup PSN refresh; seeds the once-a-minute token check in draw_ui(). */
static uint64_t s_startup_unix = 0;

/** When the last splash frame was presented (0 before the first), for the frame gate. */
static uint64_t s_last_present_us = 0;
/** Longest gap between two presented splash frames, and the longest step run inside it. */
static uint64_t s_max_gap_us = 0;
static const char *s_max_gap_step = "none";
static uint64_t s_interval_longest_us = 0;
static const char *s_interval_longest_name = "none";
/** True while splash_frame() runs, so the frame hook cannot start a frame inside a frame. */
static bool s_in_splash_frame = false;
/** Process time at which the last loading step finished. */
static uint64_t s_load_done_us = 0;
/** Set when loading ends; the main loop then blocks held inputs once (see draw_ui()). */
static bool s_splash_block_pending = false;

/** startup_note_step() - Remember @name if it is the longest step since the last splash frame. */
static void startup_note_step(const char *name, uint64_t duration_us) {
  if (duration_us > s_interval_longest_us) {
    s_interval_longest_us = duration_us;
    s_interval_longest_name = name;
  }
}

/** startup_note_presented() - Account for a splash frame that has just been presented. */
static void startup_note_presented(void) {
  const uint64_t now = sceKernelGetProcessTimeWide();
  if (s_last_present_us == 0) {
    LOGD("PIPE/SPLASH_FIRST_FRAME us=%llu", (unsigned long long)now);
  } else if (now - s_last_present_us > s_max_gap_us) {
    s_max_gap_us = now - s_last_present_us;
    s_max_gap_step = s_interval_longest_name;
  }
  s_last_present_us = now;
  s_interval_longest_us = 0;
  s_interval_longest_name = "none";
}

/**
 * splash_frame() - Draw and present one splash frame.
 * @more_work: NULL for a plain frame. Otherwise glyphs are baked inside this frame's scene until
 *             UI_PREWARM_FRAME_BUDGET_US is spent (FreeType/GXM needs an active render pass, and
 *             only one scene may be open at a time) and *@more_work is set to whether any glyph
 *             remains to bake.
 *
 * A frame is: poll skip, open the scene, clear, [bake glyphs], draw the splash (3 draws), close,
 * swap. The draw counters start after the bake, so the glyph quads are not counted.
 */
static void splash_frame(bool *more_work) {
  s_in_splash_frame = true;
  ui_splash_poll_skip();
  vita2d_start_drawing();
  vita2d_clear_screen();
  if (more_work) {
    UiPrewarmReport report;
    const uint64_t t0 = sceKernelGetProcessTimeWide();
    *more_work = ui_text_prewarm_step(UI_PREWARM_FRAME_BUDGET_US, &report) != 0;
    /* The gap bookkeeping keeps the pointer, so it takes a literal. */
    startup_note_step("glyphs", sceKernelGetProcessTimeWide() - t0);
    if (report.face_done)
      LOGD("PIPE/SPLASH_STEP name=glyphs_%d us=%llu frames=%d metrics_us=%llu first_glyph_us=%llu",
           report.face, (unsigned long long)report.bake_us, report.frames,
           (unsigned long long)report.metrics_us, (unsigned long long)report.first_glyph_us);
    if (!*more_work)
      LOGD("PIPE/UI_PREWARM_DONE us=%llu", (unsigned long long)sceKernelGetProcessTimeWide());
  }
  UI_DRAW_STATS_FRAME_BEGIN();
  ui_splash_draw();
  UI_DRAW_STATS_FRAME_END("splash");
  vita2d_end_drawing();
  vita2d_swap_buffers();
  startup_note_presented();
  s_in_splash_frame = false;
}

/**
 * startup_frame_hook() - Draw a splash frame from inside a loading step, if one is due.
 *
 * Called through ui_asset_preload_pump(): after each texture is made, while the main thread waits
 * for a PNG decode, and after each baked texture. It is set only while loading runs, after the
 * first splash frame, and never starts a frame while one is being drawn (so never inside an open
 * scene). Uses the same time gate as startup_run_step().
 */
static void startup_frame_hook(void) {
  if (s_in_splash_frame)
    return;
  if (sceKernelGetProcessTimeWide() - s_last_present_us >= UI_SPLASH_FRAME_INTERVAL_US)
    splash_frame(NULL);
}

/** startup_run_step() - Run one step, log its duration, then draw a splash frame if one is due. */
static void startup_run_step(const StartupStep *step) {
  const uint64_t t0 = sceKernelGetProcessTimeWide();
  step->run();
  const uint64_t duration_us = sceKernelGetProcessTimeWide() - t0;
  LOGD("PIPE/SPLASH_STEP name=%s us=%llu", step->name, (unsigned long long)duration_us);
  startup_note_step(step->name, duration_us);
  if (sceKernelGetProcessTimeWide() - s_last_present_us >= UI_SPLASH_FRAME_INTERVAL_US)
    splash_frame(NULL);
}

/**
 * Roboto TTF bytes read by the preload worker. FreeType reads the font from this memory for as
 * long as the font lives, which is the whole run (fonts are never freed), so it is never freed
 * either. NULL when the font was opened from its file instead.
 */
static uint8_t *s_font_data_regular = NULL;
static uint8_t *s_font_data_light = NULL;

/**
 * open_font() - Open a Roboto TTF, from the bytes the preload worker read when it has them.
 * @path:     The TTF path (UI_ASSET_FONT_*_PATH).
 * @data_out: Where the owned TTF bytes are kept (see s_font_data_regular).
 *
 * Opening from the file makes FreeType read app0: through many small reads during the first glyph
 * lookups, on the main thread. If the worker's bytes are missing the font is opened from its file
 * so text still draws, after a warning naming the path.
 */
static vita2d_font *open_font(const char *path, uint8_t **data_out) {
  unsigned int size = 0;
  if (ui_asset_preload_take_font(path, data_out, &size)) {
    vita2d_font *font = vita2d_load_font_mem(*data_out, size);
    if (font)
      return font;
    LOGE("UI/FONT could not open '%s' from memory", path);
    free(*data_out);
    *data_out = NULL;
  }
  LOGW("UI/FONT '%s' is not available from the preload, opening it from the file", path);
  return vita2d_load_font_file(path);
}

/** step_fonts() - Open the two Roboto weights and hand them to the text module. */
static void step_fonts(void) {
  vita2d_font *font = open_font(UI_ASSET_FONT_REGULAR_PATH, &s_font_data_regular);
  vita2d_font *font_light = open_font(UI_ASSET_FONT_LIGHT_PATH, &s_font_data_light);
  ui_text_init(font, font_light);
}

/** step_input() - Start touch sampling and initialise the input, state and focus modules. */
static void step_input(void) {
  sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
  sceTouchEnableTouchForce(SCE_TOUCH_PORT_FRONT);

  ui_input_init();
  ui_state_init();
  ui_focus_init();  // Initialize centralized focus manager (Phase 1)

  // Get pointers to input state for direct manipulation (legacy compatibility)
  button_block_mask = ui_input_get_button_block_mask_ptr();
}

/** step_psn_id() - Load the PSN account id from the registry if the config has none. */
static void step_psn_id(void) {
  load_psn_id_if_needed();
}

/**
 * step_psn_refresh() - Start the startup PSN token refresh and host-list fetch in the background.
 *
 * Returns at once so the splash keeps drawing; draw_ui() commits the result once it is ready
 * (ticket #366). A no-op when PSN internet mode is disabled.
 */
static void step_psn_refresh(void) {
  time_t startup_t = time(NULL);
  if (startup_t != (time_t)-1) {
    s_startup_unix = (uint64_t)startup_t;
    psn_startup_refresh_begin();
  } else {
    CHIAKI_LOGW(&(context.log), "PSN auth: skipping startup host refresh — system clock not set");
  }
}

/** Loading steps after the splash is up, in the order init_ui() and draw_ui() always did them. */
static const StartupStep STARTUP_STEPS[] = {
    {"textures", load_textures},
    {"background", ui_background_init},  // Build the wave background geometry
    {"cards", ui_cards_init},            // Initialize console card system
    /* Text helper: needs the fonts loaded; metrics are measured later, in the glyph steps. */
    {"fonts", step_fonts},
    {"glow", ui_glow_init},
    {"shapes", ui_shapes_init},
    {"room_icons", ui_room_icons_init},
    {"result_popup", ui_result_popup_init},  // shared by Home and the PIN screen, so before both
    {"home", ui_home_init},
    {"connecting", ui_connecting_init},
    {"toast", ui_toast_init},
    {"settings", ui_settings_init},
    {"profile", ui_profile_init},
    {"controller_page", ui_controller_page_init},
    {"pin", ui_pin_init},
    {"list_popup", ui_list_popup_init},
    {"pair_popup", ui_pair_popup_init},
    {"input", step_input},
    {"psn_id", step_psn_id},
    {"psn_refresh", step_psn_refresh},
};

/**
 * init_ui() - Bring the UI up behind the splash screen
 *
 * 1. Starts the asset preload worker (it reads and decodes the start-up PNGs in RAM from here on),
 *    then initializes vita2d.
 * 2. Makes the logo texture from the decoded logo, picks the particles from its RAM pixels and
 *    draws the first splash frame.
 * 3. Runs STARTUP_STEPS one after another on the main thread. Their PNG loads are served from the
 *    worker, and the splash draws a frame between textures (startup_frame_hook()) and after each
 *    step, only when one display frame interval has passed since the last.
 * 4. Joins the worker, bakes the glyph atlas a time budget per splash frame (it needs an open
 * scene).
 * 5. Marks loading done and keeps drawing splash frames until the logo has assembled; the fade
 *    over Home is then drawn by the main loop in draw_ui().
 *
 * Must be called before draw_ui() main loop.
 */
void init_ui() {
  ui_asset_preload_start();
  const uint64_t vita2d_start_us = sceKernelGetProcessTimeWide();
  int vita2d_init_ret =
      vita2d_init_advanced_with_msaa(SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE, SCE_GXM_MULTISAMPLE_4X);
  if (vita2d_init_ret < 0) {
    sceClibPrintf("[WARN] MSAA init failed (0x%08x), falling back to default init\n",
                  vita2d_init_ret);
    vita2d_init();
  }
  /* The splash needs a black screen and nothing else may show first. Nothing blocks on the
   * display while loading: the splash frames are time gated and animation is time based. */
  vita2d_set_clear_color(UI_SPLASH_CLEAR_COLOR);
  vita2d_set_vblank_wait(false);
  const uint64_t logo_wait_start_us = sceKernelGetProcessTimeWide();
  UiAssetPixels logo_pixels;
  const bool logo_listed = ui_asset_preload_wait(UI_ASSET_LOGO_PATH, &logo_pixels);
  const uint64_t logo_upload_start_us = sceKernelGetProcessTimeWide();
  vita_rps5_logo =
      logo_pixels.rgba ? ui_asset_preload_upload(&logo_pixels, UI_ASSET_LOGO_PATH) : NULL;
  const uint64_t logo_done_us = sceKernelGetProcessTimeWide();
  if (!logo_listed)
    LOGE("UI/SPLASH logo '%s' is not available from the preload, splash draws black only",
         UI_ASSET_LOGO_PATH);
  ui_splash_start(vita_rps5_logo, &logo_pixels);
  ui_asset_pixels_free(&logo_pixels);
  LOGD("PIPE/SPLASH_INIT vita2d_us=%llu logo_wait_us=%llu logo_upload_us=%llu sample_us=%llu",
       (unsigned long long)(logo_wait_start_us - vita2d_start_us),
       (unsigned long long)(logo_upload_start_us - logo_wait_start_us),
       (unsigned long long)(logo_done_us - logo_upload_start_us),
       (unsigned long long)ui_splash_sample_us());
  splash_frame(NULL);
  ui_asset_preload_set_frame_hook(startup_frame_hook);

  for (size_t i = 0; i < sizeof(STARTUP_STEPS) / sizeof(STARTUP_STEPS[0]); i++)
    startup_run_step(&STARTUP_STEPS[i]);
  ui_asset_preload_finish();

  /* Glyphs are baked inside splash frames, a time budget per frame, whatever the frame gate says.
   */
  bool more_work = ui_text_needs_prewarm() != 0;
  while (more_work)
    splash_frame(&more_work);

  s_load_done_us = sceKernelGetProcessTimeWide();
  vita2d_set_clear_color(UI_SCENE_CLEAR_COLOR);
  vita2d_set_vblank_wait(true);
  ui_splash_loading_done();
  while (!ui_splash_ready_to_exit())
    splash_frame(NULL);
  s_splash_block_pending = true;
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
  bool first_home_frame_logged = false;
  context.ui_state.debug_menu_active = false;
  context.ui_state.debug_menu_modal_pushed = false;
  context.ui_state.debug_menu_selection = 0;

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

    /* Commit the startup PSN refresh once its worker has finished (ticket #366). */
    if (!context.stream.is_streaming)
      psn_startup_refresh_poll();

    /* Proactively refresh PSN token once per minute while idle so it never
     * expires unnoticed between user actions. Skip during streaming to avoid
     * network contention with the media path. */
    if (!context.stream.is_streaming) {
      static uint64_t last_token_check_unix = 0;
      time_t t = time(NULL);
      if (t != (time_t)-1) {
        uint64_t now_unix = (uint64_t)t;
        if (last_token_check_unix == 0)
          last_token_check_unix = s_startup_unix;
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
    /* The fade over Home starts now. A button or finger the user is still holding from skipping
     * the splash must not act on Home, so it is blocked until it is released. */
    if (s_splash_block_pending) {
      block_inputs_for_transition();
      s_splash_block_pending = false;
    }

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
      /* The splash fades out over Home as the last draw of the frame, after the Home and debug
       * menu draws it covers (and after the draw counters, so they stay Home's own). */
      const bool splash_was_active = ui_splash_active();
      if (splash_was_active && ui_splash_draw_exit()) {
        LOGD(
            "PIPE/SPLASH_DONE frames=%u expected=%llu elapsed_us=%llu load_done_us=%llu "
            "max_gap_us=%llu max_gap_step=%s",
            (unsigned int)ui_splash_frames_drawn(),
            (unsigned long long)(ui_splash_elapsed_us() / UI_SPLASH_FRAME_INTERVAL_US),
            (unsigned long long)ui_splash_elapsed_us(), (unsigned long long)s_load_done_us,
            (unsigned long long)s_max_gap_us, s_max_gap_step);
      }
      vita2d_end_drawing();
      vita2d_common_dialog_update();
      vita2d_swap_buffers();
      if (splash_was_active)
        startup_note_presented();
      if (!first_home_frame_logged && prev_screen == UI_SCREEN_TYPE_MAIN) {
        LOGD("PIPE/UI_FIRST_HOME_FRAME us=%llu", (unsigned long long)sceKernelGetProcessTimeWide());
        first_home_frame_logged = true;
      }
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
