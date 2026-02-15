/**
 * @file ui_internal.h
 * @brief Internal shared state and declarations for UI modules
 *
 * This header is for internal use by UI modules only.
 * External code should use ui.h for the public API.
 *
 * Provides access to:
 * - Shared texture pointers
 * - Shared fonts
 * - Global state accessors
 * - Cross-module function declarations
 */

#pragma once

#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>

#include "ui_constants.h"
#include "ui_types.h"

// Forward declaration to avoid circular include (context.h includes ui.h)
// Actual context will be available in ui.c and other implementation files
struct vita_chiaki_context_t;
typedef struct vita_chiaki_context_t VitaChiakiContext;

// ============================================================================
// Shared Texture Pointers (defined in ui.c, will move to ui_main.c)
// ============================================================================

// Fonts
extern vita2d_font* font;
extern vita2d_font* font_mono;

// Console icons
extern vita2d_texture *img_ps4, *img_ps4_off, *img_ps4_rest;
extern vita2d_texture *img_ps5, *img_ps5_off, *img_ps5_rest;
extern vita2d_texture *img_discovery_host;

// UI symbols (particles)
extern vita2d_texture *symbol_triangle, *symbol_circle, *symbol_ex, *symbol_square;

// Wave textures
extern vita2d_texture *wave_top, *wave_bottom;

// Status ellipses
extern vita2d_texture *ellipse_green, *ellipse_yellow, *ellipse_red;

// Navigation icons
extern vita2d_texture *icon_play, *icon_settings, *icon_controller, *icon_profile;
extern vita2d_texture *icon_button_triangle;

// Other UI textures
extern vita2d_texture *button_add_new;
extern vita2d_texture *background_gradient, *vita_rps5_logo;
extern vita2d_texture *vita_front, *ps5_logo;

// ============================================================================
// Shared Global State (defined in ui.c, will be organized into modules)
// ============================================================================

// Button configuration (set during init)
extern int SCE_CTRL_CONFIRM;
extern int SCE_CTRL_CANCEL;
extern char* confirm_btn_str;
extern char* cancel_btn_str;

// Tooltip buffer
extern char active_tile_tooltip_msg[MAX_TOOLTIP_CHARS];

// PIN entry cursor blink state
extern bool show_cursor;

// Navigation state (defined in ui_navigation.c, exposed for backward compatibility)
// Note: New code should use ui_nav_* query functions instead of direct access
extern NavCollapseState nav_collapse;

// Navigation selection (defined in ui_navigation.c)
// Note: New code should use ui_nav_get/set functions instead of direct access
// Exposed for backward compatibility during refactoring
extern int selected_nav_icon;
// Legacy: current_focus and last_console_selection removed in Phase 4
// Use focus manager (ui_focus.h) instead

// ============================================================================
// Shared Context Access
// ============================================================================

// Global app context (defined in context.c)
// Note: Full definition is in context.h, but we forward declare here
// to avoid circular include issues (context.h includes ui.h)
extern VitaChiakiContext context;

// ============================================================================
// Utility Macros
// ============================================================================

/**
 * Get current time in microseconds
 */
#define UI_NOW_US() sceKernelGetProcessTimeWide()

/**
 * Calculate elapsed milliseconds from start time
 */
#define UI_ELAPSED_MS(start_us) \
    ((float)(UI_NOW_US() - (start_us)) / 1000.0f)

// ============================================================================
// Inline Utility Functions
// ============================================================================

/**
 * Linear interpolation
 */
static inline float ui_lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

/**
 * Clamp value between min and max
 */
static inline float ui_clamp(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

/**
 * Ease in-out cubic function for smooth animations
 */
static inline float ui_ease_in_out_cubic(float t) {
    if (t < 0.5f) {
        return 4.0f * t * t * t;
    } else {
        float f = (2.0f * t - 2.0f);
        return 0.5f * f * f * f + 1.0f;
    }
}

/**
 * Calculate dynamic content center X accounting for nav width
 */
static inline int ui_get_dynamic_content_center_x(void) {
    // Menu is an overlay - content centers on FULL screen
    return VITA_WIDTH / 2;  // 480px
}

// ============================================================================
// Cross-Module Function Declarations
// ============================================================================

// Input handling (ui_input.c)
bool btn_pressed(SceCtrlButtons btn);
bool btn_down(SceCtrlButtons btn);
bool btn_released(SceCtrlButtons btn);
void block_inputs_for_transition(void);
bool is_point_in_circle(float px, float py, int cx, int cy, int radius);
bool is_point_in_rect(float px, float py, int rx, int ry, int rw, int rh);
uint32_t* ui_input_get_button_block_mask_ptr(void);
bool* ui_input_get_touch_block_active_ptr(void);
bool* ui_input_get_touch_block_pending_clear_ptr(void);

// Graphics primitives (ui_graphics.c)
void ui_draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
void ui_draw_card_with_shadow(int x, int y, int w, int h, int radius, uint32_t color);
void ui_draw_circle(int cx, int cy, int radius, uint32_t color);
void ui_draw_circle_outline(int cx, int cy, int radius, uint32_t color);
void ui_draw_spinner(int cx, int cy, int radius, int thickness, float rotation_deg, uint32_t color);
void ui_draw_content_focus_overlay(void);
void ui_draw_loss_indicator(void);

// Navigation (ui_navigation.c)
#include "ui_navigation.h"

// Focus Manager (ui_focus.c)
#include "ui_focus.h"

// Legacy compatibility wrappers for ui.c (map to new navigation module)
#define render_wave_navigation() ui_nav_render()
#define nav_request_collapse() ui_nav_request_collapse()
#define nav_request_expand() ui_nav_request_expand()
#define nav_toggle_collapse() ui_nav_toggle()
#define nav_reset_to_collapsed() ui_nav_reset_collapsed()
#define update_nav_collapse_animation() ui_nav_update_collapse_animation()
#define update_wave_animation() ui_nav_update_wave_animation()
#define show_nav_collapse_toast() ui_nav_update_toast()
#define render_nav_pill() ui_nav_render_pill()
#define render_nav_collapse_toast() ui_nav_render_toast()
#define nav_screen_for_index(i) ui_nav_screen_for_icon(i)
#define nav_touch_hit(x, y, out) ui_nav_handle_touch(x, y, out)
#define pill_touch_hit(x, y) ui_nav_handle_pill_touch(x, y)
#define handle_global_nav_shortcuts(screen, out, dpad) ui_nav_handle_shortcuts(screen, out, dpad)

// Procedural icon drawing (available globally)
#define draw_play_icon(cx, cy, sz) ui_nav_draw_play_icon(cx, cy, sz)
#define draw_settings_icon(cx, cy, sz) ui_nav_draw_settings_icon(cx, cy, sz)
#define draw_controller_icon(cx, cy, sz) ui_nav_draw_controller_icon(cx, cy, sz)
#define draw_profile_icon(cx, cy, sz) ui_nav_draw_profile_icon(cx, cy, sz)
#define draw_hamburger_icon(x, cy, sz, col) ui_nav_draw_hamburger_icon(x, cy, sz, col)

// Console cards (ui_console_cards.c)
#include "ui_console_cards.h"

// Animation (ui_animation.c)
void ui_particles_init(void);
void ui_particles_update(void);
void ui_particles_render(void);
uint64_t ui_anim_now_us(void);
float ui_anim_elapsed_ms(uint64_t start_us);

// State management (ui_state.c)
bool stream_cooldown_active(void);
uint64_t stream_cooldown_until_us(void);
bool takion_cooldown_gate_active(void);
bool start_connection_thread(VitaChiakiHost *host);
int get_text_width_cached(const char* text, int font_size);
bool ui_connection_overlay_active(void);
UIConnectionStage ui_connection_stage(void);
void ui_clear_waking_wait(void);

// Components (ui_components.c)
// Legacy compatibility wrappers - internal use only
void draw_toggle_switch(int x, int y, int width, int height, float anim_value, bool selected);
void draw_dropdown(int x, int y, int width, int height, const char* label,
                   const char* value, bool expanded, bool selected);
void draw_tab_bar(int x, int y, int width, int height,
                  const char* tabs[], uint32_t colors[], int num_tabs, int selected);
void draw_status_dot(int x, int y, int radius, int status);
void draw_section_header(int x, int y, int width, const char* title);
void render_pin_digit(int x, int y, uint32_t digit, bool is_current, bool has_value);
void start_toggle_animation(int toggle_index, bool target_state);
float get_toggle_animation_value(int toggle_index, bool current_state);
void render_error_popup(void);
void handle_error_popup_input(void);
void trigger_hints_popup(const char* hint_text);
void render_hints_popup(void);
void render_hints_indicator(void);
void open_debug_menu(void);
void close_debug_menu(void);
void render_debug_menu(void);
void handle_debug_menu_input(void);

// Screens (ui_screens.c)
#include "ui_screens.h"

// ============================================================================
// Debug Menu Configuration
// ============================================================================

extern const bool debug_menu_enabled;
extern const uint32_t DEBUG_MENU_COMBO_MASK;
extern const char *debug_menu_options[];
#define DEBUG_MENU_OPTION_COUNT 4
