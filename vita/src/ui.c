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
 * - ui_animation.c: Particle effects and animation timing
 * - ui_input.c: Button/touch input handling and gesture detection
 * - ui_state.c: UI state management and transitions
 * - ui_components.c: Reusable UI widgets (toggles, dropdowns, popups)
 * - ui_navigation.c: Wave navigation sidebar and menu system
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
#include <stdio.h>
#include <string.h>
#include <math.h>
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
#include "host.h"
#include "ui.h"
#include "util.h"
#include "video.h"
#include "ui/ui_graphics.h"
#include "ui/ui_animation.h"
#include "ui/ui_input.h"
#include "ui/ui_state.h"
#include "ui/ui_components.h"
#include "ui/ui_navigation.h"
#include "ui/ui_focus.h"
#include "ui/ui_internal.h"

vita2d_font* font;
vita2d_font* font_mono;
vita2d_texture *img_ps4, *img_ps4_off, *img_ps4_rest,
    *img_ps5, *img_ps5_off, *img_ps5_rest, *img_discovery_host;

// VitaRPS5 UI textures
vita2d_texture *symbol_triangle, *symbol_circle, *symbol_ex, *symbol_square;
vita2d_texture *wave_top, *wave_bottom;
vita2d_texture *ellipse_green, *ellipse_yellow, *ellipse_red;
vita2d_texture *button_add_new;
vita2d_texture *icon_play, *icon_settings, *icon_controller, *icon_profile;
vita2d_texture *icon_button_triangle;
vita2d_texture *background_gradient, *vita_rps5_logo;
vita2d_texture *vita_front, *ps5_logo;

// Input state (managed by ui_input.c - accessed via pointers for direct manipulation)
static uint32_t *button_block_mask = NULL;
static bool *touch_block_active = NULL;
static bool *touch_block_pending_clear = NULL;

// State management convenience macros (for legacy code compatibility)
#define waking_start_time ui_state_get_waking_start_time_us()
#define SET_waking_start_time(val) ui_state_set_waking_start_time_us(val)
#define waking_wait_for_stream_us ui_state_get_waking_wait_for_stream_us()
#define SET_waking_wait_for_stream_us(val) ui_state_set_waking_wait_for_stream_us(val)
#define reconnect_start_time ui_state_get_reconnect_start_time()
#define SET_reconnect_start_time(val) ui_state_set_reconnect_start_time(val)
#define reconnect_animation_frame ui_state_get_reconnect_animation_frame()
#define SET_reconnect_animation_frame(val) ui_state_set_reconnect_animation_frame(val)
#define connection_overlay_active ui_connection_overlay_active()
#define connection_overlay_stage ui_connection_stage()
#define connection_thread_id (-1)  // Thread ID access not needed in ui.c (managed by ui_state.c)

// Wave navigation state
#define WAVE_NAV_ICON_SIZE 32       // Per spec: 32x32px icons
#define WAVE_NAV_ICON_X 50          // Positioned left of center to avoid overlapping wave edge
#define WAVE_NAV_ICON_SPACING 80    // Spacing between icon centers
// Vertically center 4 icons: 3 gaps of 80px = 240px total span between first and last
// Center Y = VITA_HEIGHT / 2 = 272px
// First icon Y = 272px - (240px / 2) = 272px - 120px = 152px
#define WAVE_NAV_ICON_START_Y 152

// Navigation state moved to ui_navigation.c
// Access via ui_nav_* functions or extern declarations in ui_internal.h

// HintsPopupState type moved to ui_types.h
// HintsPopupState instance moved to ui_components.c

// Console card system (updated per UI spec)
// Console card constants moved to ui_constants.h

// ConsoleCardInfo type moved to ui_types.h
// selected_console_index moved to ui_console_cards.c
// Console card cache moved to ui_console_cards.c
// CardFocusAnimState moved to ui_console_cards.c

// ToggleAnimationState type moved to ui_types.h
// ToggleAnimationState instance moved to ui_components.c

// Component functions moved to ui_components.c (accessible via ui_internal.h)
static void render_loss_indicator_preview(void);

// Navigation functions moved to ui_navigation.c (accessible via ui_internal.h)

// Debug menu configuration moved to ui_components.c

// Connection overlay, cooldown, thread management, and text cache moved to ui_state.c

// Wave navigation sidebar uses simple colored bar (no animation)

// PinEntryState type moved to ui_types.h


// PIN entry state moved to ui_screens.c
// cursor_blink_timer moved to ui_screens.c

// FocusArea and UIHostAction enums moved to ui_types.h (included via ui_state.h)
// current_focus and last_console_selection moved to ui_navigation.c

#define MAX_TOOLTIP_CHARS 200
char active_tile_tooltip_msg[MAX_TOOLTIP_CHARS] = {0};

/// Types of screens that can be rendered
// UIScreenType enum moved to ui_types.h (included via ui_state.h)

// Initialize Yes and No button from settings (will be updated in init_ui)
int SCE_CTRL_CONFIRM = SCE_CTRL_CROSS;
int SCE_CTRL_CANCEL  = SCE_CTRL_CIRCLE;
char* confirm_btn_str = "Cross";
char* cancel_btn_str  = "Circle";

// btn_pressed() and block_inputs_for_transition() moved to ui_input.c

// ============================================================================
// Navigation functions moved to ui_navigation.c
// ============================================================================

// Pill rendering, overlay, and touch functions moved to ui_navigation.c

// Error popup and debug menu functions moved to ui_components.c

static void render_loss_indicator_preview(void) {
  if (context.stream.is_streaming)
    return;
  if (!context.config.show_network_indicator)
    return;
  uint64_t now_us = sceKernelGetProcessTimeWide();
  if (!context.stream.loss_alert_until_us ||
      now_us >= context.stream.loss_alert_until_us)
    return;

  uint64_t duration = context.stream.loss_alert_duration_us ?
      context.stream.loss_alert_duration_us : VIDEO_LOSS_ALERT_DEFAULT_US;
  if (!duration)
    duration = VIDEO_LOSS_ALERT_DEFAULT_US;
  uint64_t remaining = context.stream.loss_alert_until_us - now_us;
  float alpha_ratio = (float)remaining / (float)duration;
  if (alpha_ratio < 0.0f)
    alpha_ratio = 0.0f;
  uint8_t alpha = (uint8_t)(alpha_ratio * 255.0f);

  const int margin = 18;
  const int dot_radius = 6;
  const int padding_x = 18;
  const int padding_y = 6;
  const char *headline = "Network Unstable";
  int text_width = vita2d_font_text_width(font, FONT_SIZE_SMALL, headline);
  int box_w = padding_x * 2 + dot_radius * 2 + 10 + text_width;
  int box_h = padding_y * 2 + FONT_SIZE_SMALL + 4;
  int box_x = VITA_WIDTH - box_w - margin;
  int box_y = VITA_HEIGHT - box_h - margin;

  uint8_t bg_alpha = (uint8_t)(alpha_ratio * 200.0f);
  if (bg_alpha < 40)
    bg_alpha = 40;
  ui_draw_rounded_rect(box_x, box_y, box_w, box_h, box_h / 2,
                         RGBA8(0, 0, 0, bg_alpha));

  int dot_x = box_x + padding_x;
  int dot_y = box_y + box_h / 2;
  vita2d_draw_fill_circle(dot_x, dot_y, dot_radius,
                          RGBA8(0xF4, 0x43, 0x36, alpha));

  int text_x = dot_x + dot_radius + 10;
  int text_y = box_y + box_h / 2 + (FONT_SIZE_SMALL / 2) - 2;
  vita2d_font_draw_text(font, text_x, text_y,
                        RGBA8(0xFF, 0xFF, 0xFF, alpha),
                        FONT_SIZE_SMALL,
                        headline);
}

// Debug menu render and input functions moved to ui_components.c

// ============================================================================
// ANIMATION HELPERS
// ============================================================================
// Animation helper functions (lerp, ease_in_out_cubic) moved to ui_internal.h
// Toggle animation functions moved to ui_components.c

// ============================================================================
// REUSABLE UI COMPONENTS
// ============================================================================
// Widget drawing functions (toggle, dropdown, tabs, status_dot, section_header) moved to ui_components.c
// StatusType enum moved to ui_components.h as UIStatusType
// Spinner drawing moved to ui_graphics.c (ui_draw_spinner)

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
  img_ps4 = vita2d_load_PNG_file(IMG_PS4_PATH);
  img_ps4_off = vita2d_load_PNG_file(IMG_PS4_OFF_PATH);
  img_ps4_rest = vita2d_load_PNG_file(IMG_PS4_REST_PATH);
  img_ps5 = vita2d_load_PNG_file(IMG_PS5_PATH);
  img_ps5_off = vita2d_load_PNG_file(IMG_PS5_OFF_PATH);
  img_ps5_rest = vita2d_load_PNG_file(IMG_PS5_REST_PATH);
  img_discovery_host = vita2d_load_PNG_file(IMG_DISCOVERY_HOST);

  // Load VitaRPS5 UI assets
  symbol_triangle = vita2d_load_PNG_file("app0:/assets/symbol_triangle.png");
  symbol_circle = vita2d_load_PNG_file("app0:/assets/symbol_circle.png");
  symbol_ex = vita2d_load_PNG_file("app0:/assets/symbol_ex.png");
  symbol_square = vita2d_load_PNG_file("app0:/assets/symbol_square.png");
  wave_top = vita2d_load_PNG_file("app0:/assets/wave_top.png");
  wave_bottom = vita2d_load_PNG_file("app0:/assets/wave_bottom.png");
  ellipse_green = vita2d_load_PNG_file("app0:/assets/ellipse_green.png");
  ellipse_yellow = vita2d_load_PNG_file("app0:/assets/ellipse_yellow.png");
  ellipse_red = vita2d_load_PNG_file("app0:/assets/ellipse_red.png");
  button_add_new = vita2d_load_PNG_file("app0:/assets/button_add_new.png");

  // Load navigation icons
  icon_play = vita2d_load_PNG_file("app0:/assets/icon_play.png");
  icon_settings = vita2d_load_PNG_file("app0:/assets/icon_settings.png");
  icon_controller = vita2d_load_PNG_file("app0:/assets/icon_controller.png");
  icon_profile = vita2d_load_PNG_file("app0:/assets/icon_profile.png");
  icon_button_triangle = vita2d_load_PNG_file("app0:/assets/icon_button_triangle.png");

  // Load new professional assets
  background_gradient = vita2d_load_PNG_file("app0:/assets/background.png");
  vita_rps5_logo = vita2d_load_PNG_file("app0:/assets/Vita_RPS5_Logo.png");
  vita_front = vita2d_load_PNG_file("app0:/assets/Vita_Front.png");
  ps5_logo = vita2d_load_PNG_file("app0:/assets/PS5_logo.png");
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
  SceTouchData* tdf = &(context.ui_state.touch_state_front);
  if (!tdf) {
    return false;
  }
  // TODO: Do the coordinate systems really match?
  return tdf->report->x > x && tdf->report->x <= x + width &&
         tdf->report->y > y && tdf->report->y <= y + height;
}

// is_point_in_circle() and is_point_in_rect() moved to ui_input.c

// ============================================================================
// Touch input handler moved to ui_screens.c
// ============================================================================

// ============================================================================
// PSN ACCOUNT INITIALIZATION
// ============================================================================

/**
 * load_psn_id_if_needed() - Load PSN account ID from Vita registry
 *
 * Queries the system registry for the PlayStation Network account ID and
 * stores it in base64-encoded form in the context config. This is required
 * for PS5/PS4 remote play authentication.
 *
 * Only loads if not already present in config. Called once during UI init.
 */
void load_psn_id_if_needed() {
  if (context.config.psn_account_id == NULL || strlen(context.config.psn_account_id) < 1) {
    char accIDBuf[8];
    memset(accIDBuf, 0, sizeof(accIDBuf));
    if (context.config.psn_account_id) {
      free(context.config.psn_account_id);
    }
    sceRegMgrGetKeyBin("/CONFIG/NP/", "account_id", accIDBuf, sizeof(accIDBuf));

    int b64_strlen = get_base64_size(sizeof(accIDBuf));
    context.config.psn_account_id = (char*)malloc(b64_strlen+1); // + 1 for null termination
    context.config.psn_account_id[b64_strlen] = 0; // null terminate
    chiaki_base64_encode(accIDBuf, sizeof(accIDBuf), context.config.psn_account_id, get_base64_size(sizeof(accIDBuf)));
    LOGD("size of id %d", strlen(context.config.psn_account_id));
  }
}

// ============================================================================
// SCREEN RENDERING
// ============================================================================
// All screen rendering functions moved to ui_screens.c:
// - ui_screen_draw_main()
// - ui_screen_draw_settings()
// - ui_screen_draw_profile()
// - ui_screen_draw_controller()
// - ui_screen_draw_waking()
// - ui_screen_draw_reconnecting()
// - ui_screen_draw_registration()
// - ui_screen_draw_stream()
// - ui_screen_draw_messages()
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
 * 5. Initializes all UI modules (input, screens, state, particles, cards)
 *
 * Must be called before draw_ui() main loop.
 */
void init_ui() {
  vita2d_init();
  vita2d_set_clear_color(RGBA8(0x40, 0x40, 0x40, 0xFF));
  load_textures();
  ui_particles_init();  // Initialize VitaRPS5 particle background
  ui_cards_init();      // Initialize console card system
  font = vita2d_load_font_file("app0:/assets/fonts/Roboto-Regular.ttf");
  font_mono = vita2d_load_font_file("app0:/assets/fonts/RobotoMono-Regular.ttf");
  vita2d_set_vblank_wait(true);

  // Initialize touch screen
  sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
  sceTouchEnableTouchForce(SCE_TOUCH_PORT_FRONT);

  // Set yes/no buttons (circle = yes on Japanese vitas, typically)
  SCE_CTRL_CONFIRM = context.config.circle_btn_confirm ? SCE_CTRL_CIRCLE : SCE_CTRL_CROSS;
  SCE_CTRL_CANCEL  = context.config.circle_btn_confirm ? SCE_CTRL_CROSS : SCE_CTRL_CIRCLE;
  confirm_btn_str = context.config.circle_btn_confirm ? "Circle" : "Cross";
  cancel_btn_str  = context.config.circle_btn_confirm ? "Cross" : "Circle";

  // Initialize UI modules
  ui_input_init();
  ui_screens_init();
  ui_state_init();
  ui_nav_init();     // Initialize navigation module
  ui_focus_init();   // Initialize centralized focus manager (Phase 1)

  // Get pointers to input state for direct manipulation (legacy compatibility)
  button_block_mask = ui_input_get_button_block_mask_ptr();
  touch_block_active = ui_input_get_touch_block_active_ptr();
  touch_block_pending_clear = ui_input_get_touch_block_pending_clear_ptr();
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
  context.ui_state.debug_menu_active = false;
  context.ui_state.debug_menu_selection = 0;

  load_psn_id_if_needed();

  while (true) {
    // Always read controller input - input thread uses Ext2 variant to access controller independently
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

    // Allow popup dismissal to capture inputs before we process other widgets
    handle_error_popup_input();
    handle_debug_menu_input();

    if (debug_menu_enabled && !context.stream.is_streaming &&
        !context.ui_state.debug_menu_active) {
      if ((context.ui_state.button_state & DEBUG_MENU_COMBO_MASK) == DEBUG_MENU_COMBO_MASK &&
          (context.ui_state.old_button_state & DEBUG_MENU_COMBO_MASK) != DEBUG_MENU_COMBO_MASK) {
        open_debug_menu();
      }
    }


      // handle invalid items
      int this_active_item = context.ui_state.next_active_item;
      if (this_active_item == -1) {
        this_active_item = context.ui_state.active_item;
      }
      if (this_active_item > -1) {
        if (this_active_item & UI_MAIN_WIDGET_HOST_TILE) {
          if (context.num_hosts == 0) {
            // return to toolbar
            context.ui_state.next_active_item = UI_MAIN_WIDGET_SETTINGS_BTN;
          } else {
            int host_j = this_active_item - UI_MAIN_WIDGET_HOST_TILE;
            if (host_j >= context.num_hosts) {
              context.ui_state.next_active_item = UI_MAIN_WIDGET_HOST_TILE | (context.num_hosts-1);
            }
          }
        }
      }

      if (context.ui_state.next_active_item >= 0) {
        context.ui_state.active_item = context.ui_state.next_active_item;
        context.ui_state.next_active_item = -1;
      }

      // Skip ALL rendering when streaming - match ywnico pattern
      if (!context.stream.is_streaming) {
        if (context.stream.reconnect_overlay_active) {
          screen = UI_SCREEN_TYPE_RECONNECTING;
        } else if (screen == UI_SCREEN_TYPE_RECONNECTING) {
          screen = UI_SCREEN_TYPE_MAIN;
        }

        vita2d_start_drawing();
        vita2d_clear_screen();

        // Draw full-screen background - nav is a pure overlay
        if (background_gradient) {
          vita2d_draw_texture_part(background_gradient, 0, 0,
                                   0, 0, VITA_WIDTH, VITA_HEIGHT);
        } else {
          vita2d_draw_rectangle(0, 0, VITA_WIDTH, VITA_HEIGHT, UI_COLOR_BACKGROUND);
        }

        // Wave navigation area removed - nav is a pure overlay with no background

        // Focus overlay moved to after screen rendering for correct z-order

        // Draw Vita RPS5 logo in top-right corner for professional branding (small with transparency)
        if (vita_rps5_logo) {
          int logo_w = vita2d_texture_get_width(vita_rps5_logo);
          int logo_h = vita2d_texture_get_height(vita_rps5_logo);
          float logo_scale = 0.1f;  // 10% of original size
          int scaled_w = (int)(logo_w * logo_scale);
          int scaled_h = (int)(logo_h * logo_scale);
          int logo_x = VITA_WIDTH - scaled_w - 20;  // 20px margin from right
          int logo_y = 20;  // 20px margin from top

          // Draw with 50% transparency (alpha = 128)
          vita2d_draw_texture_tint_scale(vita_rps5_logo, logo_x, logo_y,
                                         logo_scale, logo_scale,
                                         RGBA8(255, 255, 255, 128));
        }

        UIScreenType prev_screen = screen;
        UIScreenType next_screen = screen;

        // Handle zone-crossing navigation (LEFT/RIGHT between nav bar and content)
        // This must happen before screen-specific input handling
        ui_focus_handle_zone_crossing(screen);

        // Render the current screen
        if (screen == UI_SCREEN_TYPE_MAIN) {
          next_screen = ui_screen_draw_main();
        } else if (screen == UI_SCREEN_TYPE_REGISTER_HOST) {
          context.ui_state.next_active_item = (UI_MAIN_WIDGET_TEXT_INPUT | 0);
          if (!ui_screen_draw_registration()) {
            next_screen = UI_SCREEN_TYPE_MAIN;
          }
        } else if (screen == UI_SCREEN_TYPE_MESSAGES) {
          if (!ui_screen_draw_messages()) {
            next_screen = UI_SCREEN_TYPE_MAIN;
          }
        } else if (screen == UI_SCREEN_TYPE_STREAM) {
          if (!ui_screen_draw_stream()) {
            next_screen = UI_SCREEN_TYPE_MAIN;
          }
        } else if (screen == UI_SCREEN_TYPE_WAKING) {
          next_screen = ui_screen_draw_waking();
        } else if (screen == UI_SCREEN_TYPE_RECONNECTING) {
          next_screen = ui_screen_draw_reconnecting();
        } else if (screen == UI_SCREEN_TYPE_SETTINGS) {
          if (context.ui_state.active_item != (UI_MAIN_WIDGET_TEXT_INPUT | 2)) {
            context.ui_state.next_active_item = (UI_MAIN_WIDGET_TEXT_INPUT | 1);
          }
          next_screen = ui_screen_draw_settings();
        } else if (screen == UI_SCREEN_TYPE_PROFILE) {
          // Phase 2: Profile & Registration screen
          next_screen = ui_screen_draw_profile();
        } else if (screen == UI_SCREEN_TYPE_CONTROLLER) {
          // Phase 2: Controller Configuration screen
          next_screen = ui_screen_draw_controller();
        }

        if (next_screen != prev_screen) {
          block_inputs_for_transition();
          // Menu stays in current state - user controls collapse via Triangle or content tap

          // Handle modal focus for PIN entry screen only
          // Connection screens (WAKING/RECONNECTING) are handled by ui_state.c
          // Pop modal when leaving PIN entry screen
          if (prev_screen == UI_SCREEN_TYPE_REGISTER_HOST) {
            ui_focus_pop_modal();
          }

          // Push modal when entering PIN entry screen
          if (next_screen == UI_SCREEN_TYPE_REGISTER_HOST) {
            ui_focus_push_modal();
          }
        }
        screen = next_screen;

        // Render focus overlay after all screen content (correct z-order)
        ui_nav_render_content_overlay();

        // Render navigation menu overlay (on top of tint)
        render_wave_navigation();

        // Render hints system (indicator + popup)
        render_hints_indicator();
        render_hints_popup();

        render_loss_indicator_preview();
        render_debug_menu();
        render_error_popup();
        vita2d_end_drawing();
        vita2d_common_dialog_update();
        vita2d_swap_buffers();
      }
  }
}
