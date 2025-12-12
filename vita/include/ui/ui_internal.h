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

// Navigation state (defined in ui.c, will move to ui_navigation.c)
extern NavCollapseState nav_collapse;

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

// Graphics primitives (ui_graphics.c)
void ui_draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
void ui_draw_card_with_shadow(int x, int y, int w, int h, int radius, uint32_t color);
void ui_draw_circle(int cx, int cy, int radius, uint32_t color);
void ui_draw_circle_outline(int cx, int cy, int radius, uint32_t color);
void ui_draw_spinner(int cx, int cy, int radius, int thickness, float rotation_deg, uint32_t color);
void ui_draw_content_focus_overlay(void);
void ui_draw_loss_indicator(void);

// Navigation (ui_navigation.c - will be created)
// void ui_nav_render(void);
// bool ui_nav_is_expanded(void);

// Console cards (ui_console_cards.c - will be created)
// void ui_cards_render_grid(int x, int y);

// Animation (ui_animation.c)
void ui_particles_init(void);
void ui_particles_update(void);
void ui_particles_render(void);
uint64_t ui_anim_now_us(void);
float ui_anim_elapsed_ms(uint64_t start_us);

// State (ui_state.c - will be created)
// void ui_connection_begin(UIConnectionStage stage);
// void ui_connection_complete(void);

// ============================================================================
// Debug Menu Configuration
// ============================================================================

extern const bool debug_menu_enabled;
extern const uint32_t DEBUG_MENU_COMBO_MASK;
extern const char *debug_menu_options[];
#define DEBUG_MENU_OPTION_COUNT 3
