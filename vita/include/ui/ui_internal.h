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
#include <psp2/kernel/clib.h>
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
extern vita2d_font *font;

// Console icons
extern vita2d_texture *img_ps4;

// UI symbols
extern vita2d_texture *symbol_triangle, *symbol_circle, *symbol_ex, *symbol_square;

// Status ellipses
extern vita2d_texture *ellipse_green, *ellipse_yellow, *ellipse_red;

// Home category icons
extern vita2d_texture *icon_play, *icon_settings;

// Other UI textures
extern vita2d_texture *button_add_new;
extern vita2d_texture *vita_rps5_logo;
extern vita2d_texture *ps5_logo;

// ============================================================================
// Shared Global State (defined in ui.c, will be organized into modules)
// ============================================================================

// Button configuration (set during init)
extern char *confirm_btn_str;
extern char *cancel_btn_str;

// Tooltip buffer
extern char active_tile_tooltip_msg[MAX_TOOLTIP_CHARS];

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
#define UI_ELAPSED_MS(start_us) ((float)(UI_NOW_US() - (start_us)) / 1000.0f)

// ============================================================================
// Inline Utility Functions
// ============================================================================

/**
 * ui_load_png_linear() - Load a PNG file and enable bilinear filtering
 * @param path: Filesystem path to the PNG (e.g. "app0:/assets/foo.png")
 *
 * Wraps vita2d_load_PNG_file() and immediately sets SCE_GXM_TEXTURE_FILTER_LINEAR
 * on both the minification and magnification samplers. This ensures all UI
 * textures scale smoothly, especially when rendered at non-native sizes.
 *
 * Returns the loaded texture pointer, or NULL if the load failed. Callers
 * must check for NULL before rendering (consistent with vita2d_load_PNG_file).
 *
 * Do NOT use this for streaming frame textures in video.c; those have their
 * own upload path and filter requirements.
 */
static inline vita2d_texture *ui_load_png_linear(const char *path) {
  vita2d_texture *tex = vita2d_load_PNG_file(path);
  if (!tex) {
    sceClibPrintf("[ERROR] ui_load_png_linear: failed to load '%s'\n", path);
    return NULL;
  }
  vita2d_texture_set_filters(tex, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
  return tex;
}

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
  if (val < min)
    return min;
  if (val > max)
    return max;
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
uint32_t *ui_input_get_button_block_mask_ptr(void);
bool *ui_input_get_touch_block_active_ptr(void);
bool *ui_input_get_touch_block_pending_clear_ptr(void);

// Graphics primitives (ui_graphics.c)
void ui_draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
void ui_draw_card_with_shadow(int x, int y, int w, int h, int radius, uint32_t color);
void ui_draw_circle(int cx, int cy, int radius, uint32_t color);
void ui_draw_circle_outline(int cx, int cy, int radius, uint32_t color);

// Focus Manager (ui_focus.c)
#include "ui_focus.h"

// Console cards (ui_console_cards.c)
#include "ui_console_cards.h"

// Animation (ui_animation.c)
uint64_t ui_anim_now_us(void);
float ui_anim_elapsed_ms(uint64_t start_us);

// State management (ui_state.c)
bool stream_cooldown_active(void);
uint64_t stream_cooldown_until_us(void);
bool takion_cooldown_gate_active(void);
bool start_connection_thread(VitaChiakiHost *host);
int get_text_width_cached(const char *text, int font_size);
bool ui_connection_overlay_active(void);
UIConnectionStage ui_connection_stage(void);
void ui_clear_waking_wait(void);

// Components (ui_components.c)
// Legacy compatibility wrappers - internal use only
void draw_tab_bar(int x, int y, int width, int height, const char *tabs[], uint32_t colors[],
                  int num_tabs, int selected);
void draw_status_dot(int x, int y, int radius, int status);
void draw_section_header(int x, int y, int width, const char *title);
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
