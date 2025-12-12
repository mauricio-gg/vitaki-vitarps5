// Very very simple homegrown immediate mode GUI
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
#include "ui/ui_internal.h"

#ifndef VIDEO_LOSS_ALERT_DEFAULT_US
#define VIDEO_LOSS_ALERT_DEFAULT_US (5 * 1000 * 1000ULL)
#endif

#ifndef VITARPS5_DEBUG_MENU
#define VITARPS5_DEBUG_MENU 0
#endif

// Legacy colors (kept for compatibility)
#define COLOR_WHITE RGBA8(255, 255, 255, 255)
#define COLOR_GRAY50 RGBA8(129, 129, 129, 255)
#define COLOR_BLACK RGBA8(0, 0, 0, 255)
#define COLOR_ACTIVE RGBA8(255, 170, 238, 255)
#define COLOR_TILE_BG RGBA8(51, 51, 51, 255)
#define COLOR_BANNER RGBA8(22, 45, 80, 255)

// Modern VitaRPS5 colors (ABGR format for vita2d)
#define UI_COLOR_PRIMARY_BLUE 0xFFFF9034     // PlayStation Blue #3490FF
#define UI_COLOR_BACKGROUND 0xFF1A1614       // Animated charcoal gradient base
#define UI_COLOR_CARD_BG 0xFF37322D          // Dark charcoal (45,50,55)
#define UI_COLOR_TEXT_PRIMARY 0xFFFAFAFA     // Off-white (reduced eye strain)
#define UI_COLOR_TEXT_SECONDARY 0xFFB4B4B4   // Light Gray
#define UI_COLOR_TEXT_TERTIARY 0xFFA0A0A0    // Medium Gray
#define UI_COLOR_STATUS_AVAILABLE 0xFF50AF4C    // Success Green #4CAF50
#define UI_COLOR_STATUS_CONNECTING 0xFF0098FF   // Warning Orange #FF9800
#define UI_COLOR_STATUS_UNAVAILABLE 0xFF3643F4  // Error Red #F44336
#define UI_COLOR_ACCENT_PURPLE 0xFFB0279C       // Accent Purple #9C27B0
#define UI_COLOR_SHADOW 0x3C000000           // Semi-transparent black for shadows

// Particle colors (ABGR with alpha for transparency - 80% opacity = 0xCC)
#define PARTICLE_COLOR_RED    0xCCFF5555  // 80% transparent red
#define PARTICLE_COLOR_GREEN  0xCC55FF55  // 80% transparent green
#define PARTICLE_COLOR_BLUE   0xCC5555FF  // 80% transparent blue
#define PARTICLE_COLOR_ORANGE 0xCC55AAFF  // 80% transparent orange

#define VITA_WIDTH 960
#define VITA_HEIGHT 544

// VitaRPS5 UI Layout Constants
#define WAVE_NAV_WIDTH 130        // Per UI spec line 45
#define CONTENT_AREA_X WAVE_NAV_WIDTH
#define CONTENT_AREA_WIDTH (VITA_WIDTH - WAVE_NAV_WIDTH)  // 830px
#define PARTICLE_COUNT 8          // Optimized from 12 for better performance

// Particle animation constants (Batch 3: Particle Background Enhancements)
#define PARTICLE_LAYER_BG_SPEED 0.7f
#define PARTICLE_LAYER_FG_SPEED 1.0f
#define PARTICLE_SWAY_AMPLITUDE 2.0f
#define PARTICLE_SWAY_SPEED_MIN 0.5f
#define PARTICLE_SWAY_SPEED_MAX 1.5f

// Wave animation constants (per SCOPING_UI_POLISH.md)
#define WAVE_SPEED_BOTTOM 0.7f    // radians per second for bottom wave
#define WAVE_SPEED_TOP 1.1f       // radians per second for top wave
#define WAVE_ALPHA_BOTTOM 160     // 160/255 opacity for bottom wave
#define WAVE_ALPHA_TOP 100        // 100/255 opacity for top wave (less opaque for depth)

// Collapsible navigation constants (per SCOPING_NAV_COLLAPSIBLE_BAR.md)
#define NAV_COLLAPSE_DURATION_MS 280      // Total animation duration
#define NAV_PHASE1_END_MS 80              // Preparation phase end
#define NAV_PHASE2_END_MS 200             // Collapse phase end
#define NAV_PILL_WIDTH 140                // Pill width when fully collapsed
#define NAV_PILL_HEIGHT 44                // Pill height
#define NAV_PILL_X 16                     // Pill X position
#define NAV_PILL_Y 16                     // Pill Y position
#define NAV_PILL_RADIUS 22                // Pill corner radius (fully rounded)
#define NAV_TOAST_DURATION_MS 2000        // Toast display duration
#define NAV_TOAST_FADE_MS 300             // Toast fade in/out duration

// Legacy layout constants moved to ui_constants.h

// Particle type moved to ui_types.h

#define TEXTURE_PATH "app0:/assets/"
#define IMG_PS4_PATH TEXTURE_PATH "ps4.png"
#define IMG_PS4_OFF_PATH TEXTURE_PATH "ps4_off.png"
#define IMG_PS4_REST_PATH TEXTURE_PATH "ps4_rest.png"
#define IMG_PS5_PATH TEXTURE_PATH "ps5.png"
#define IMG_PS5_OFF_PATH TEXTURE_PATH "ps5_off.png"
#define IMG_PS5_REST_PATH TEXTURE_PATH "ps5_rest.png"
#define IMG_DISCOVERY_HOST TEXTURE_PATH "discovered_host.png"

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
// Content area centering
#define CONTENT_CENTER_X (WAVE_NAV_WIDTH + (CONTENT_AREA_WIDTH / 2))

/**
 * get_dynamic_content_center_x() - Calculate horizontal center of content area
 *
 * Returns the X coordinate of the content area's horizontal center, accounting
 * for the current navigation sidebar width during collapse/expand animations.
 * This ensures content stays centered within the available space as the nav
 * sidebar animates between 0px and 130px width.
 *
 * Returns: X coordinate for centering content in the available area
 */
static inline int get_dynamic_content_center_x(void) {
    // Menu is an overlay - content centers on FULL screen
    return VITA_WIDTH / 2;  // 480px
}

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
// Toggle animation functions moved to ui_components.c

/// Linear interpolation between two values
static inline float lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

/// Ease-in-out cubic interpolation for smooth animation
static inline float ease_in_out_cubic(float t) {
  return t < 0.5f ? 4.0f * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

// ============================================================================
// PHASE 2: REUSABLE UI COMPONENTS
// ============================================================================
// Widget drawing functions (toggle, dropdown, tabs, status_dot, section_header) moved to ui_components.c
// StatusType enum moved to ui_components.h as UIStatusType

/// Draw a rotating spinner animation (for loading/waiting states)
/// @param cx Center X position
/// @param cy Center Y position
/// @param radius Spinner radius
/// @param thickness Arc thickness
/// @param rotation_deg Current rotation angle in degrees
/// @param color Spinner color
static void draw_spinner(int cx, int cy, int radius, int thickness, float rotation_deg, uint32_t color) {
  // Draw a circular arc that rotates continuously
  // We'll draw 3/4 of a circle (270 degrees) that rotates around
  int segments = 32;  // Smooth circle with 32 segments
  float arc_length = 270.0f;  // 3/4 circle in degrees

  for (int i = 0; i < segments * 3 / 4; i++) {  // 3/4 of total segments
    float angle1 = rotation_deg + (i * arc_length / (segments * 3 / 4));
    float angle2 = rotation_deg + ((i + 1) * arc_length / (segments * 3 / 4));

    // Convert to radians
    float rad1 = angle1 * 3.14159f / 180.0f;
    float rad2 = angle2 * 3.14159f / 180.0f;

    // Draw outer arc segment
    int x1_outer = cx + (int)(cos(rad1) * radius);
    int y1_outer = cy + (int)(sin(rad1) * radius);
    int x2_outer = cx + (int)(cos(rad2) * radius);
    int y2_outer = cy + (int)(sin(rad2) * radius);

    // Draw inner arc segment (for thickness)
    int x1_inner = cx + (int)(cos(rad1) * (radius - thickness));
    int y1_inner = cy + (int)(sin(rad1) * (radius - thickness));
    int x2_inner = cx + (int)(cos(rad2) * (radius - thickness));
    int y2_inner = cy + (int)(sin(rad2) * (radius - thickness));

    // Draw lines to create filled arc segment
    vita2d_draw_line(x1_outer, y1_outer, x2_outer, y2_outer, color);
    vita2d_draw_line(x1_inner, y1_inner, x2_inner, y2_inner, color);

    // Fill between inner and outer arcs
    vita2d_draw_line(x1_outer, y1_outer, x1_inner, y1_inner, color);
  }
}


/// Map VitaChiakiHost to ConsoleCardInfo
// map_host_to_console_card moved to ui_console_cards.c (ui_cards_map_host)

// Console card functions moved to ui_console_cards.c (ui_cards_*)

/// Load all textures required for rendering the UI

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

  // Load new professional assets
  background_gradient = vita2d_load_PNG_file("app0:/assets/background.png");
  vita_rps5_logo = vita2d_load_PNG_file("app0:/assets/Vita_RPS5_Logo.png");
  vita_front = vita2d_load_PNG_file("app0:/assets/Vita_Front.png");
  ps5_logo = vita2d_load_PNG_file("app0:/assets/PS5_logo.png");
}

/// Check if a given region is touched on the front touch screen
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

/// Handle VitaRPS5 touch screen input

// ============================================================================
// Touch input handler moved to ui_screens.c
// ============================================================================

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

/// Draw the main menu screen with the list of hosts and header bar
/// @return the screen to draw during the next cycle

// ============================================================================
// All screen rendering functions moved to ui_screens.c:
// - ui_screen_draw_main() → ui_screen_draw_main()
// - ui_screen_draw_settings() → ui_screen_ui_screen_draw_settings()
// - ui_screen_draw_profile() → ui_screen_draw_profile()
// - ui_screen_draw_controller() → ui_screen_draw_controller()
// - ui_screen_draw_waking() → ui_screen_draw_waking()
// - ui_screen_draw_reconnecting() → ui_screen_draw_reconnecting()
// - ui_screen_draw_registration() → ui_screen_draw_registration()
// - ui_screen_draw_stream() → ui_screen_ui_screen_draw_stream()
// - ui_screen_draw_messages() → ui_screen_ui_screen_draw_messages()
// ============================================================================

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

  // Get pointers to input state for direct manipulation (legacy compatibility)
  button_block_mask = ui_input_get_button_block_mask_ptr();
  touch_block_active = ui_input_get_touch_block_active_ptr();
  touch_block_pending_clear = ui_input_get_touch_block_pending_clear_ptr();
}

/// Main UI loop
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
