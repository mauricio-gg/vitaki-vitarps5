/**
 * @file ui_input.c
 * @brief Input handling implementation for VitaRPS5
 *
 * This module manages all controller and touch input for the UI system,
 * providing button press detection, touch state tracking, and geometric
 * hit testing utilities.
 *
 * Implementation notes:
 * - button_block_mask prevents button presses during screen transitions
 * - touch_block_active prevents touch events until finger is lifted
 * - Hit testing uses simple geometric calculations optimized for PS Vita
 */

// Include context.h BEFORE ui_internal.h to avoid circular dependency issues
// context.h -> ui.h has duplicate definitions with ui_types.h (included by ui_internal.h)
// Including context.h first ensures ui.h types take precedence
#include "context.h"

#include "ui/ui_input.h"
#include "ui/ui_internal.h"

#include <psp2/touch.h>

// ============================================================================
// Module State
// ============================================================================

/**
 * Button block mask - prevents specific buttons from being detected as pressed
 * Used during screen transitions to avoid accidental carryover presses
 */
static uint32_t button_block_mask = 0;

/**
 * Touch block state - prevents touch input processing
 * Activated during transitions, cleared when finger is lifted
 */
static bool touch_block_active = false;

/**
 * Touch block pending clear flag
 * Used to delay clearing touch block (prevents immediate re-collapse in nav)
 */
static bool touch_block_pending_clear = false;

// ============================================================================
// Initialization
// ============================================================================

void ui_input_init(void) {
  button_block_mask = 0;
  touch_block_active = false;
  touch_block_pending_clear = false;
}

// ============================================================================
// Button Input Implementation
// ============================================================================

bool ui_input_btn_pressed(SceCtrlButtons btn) {
  // Check if button is currently blocked
  if (button_block_mask & btn)
    return false;

  // Block all input when error popup or debug menu is active
  if (context.ui_state.error_popup_active || context.ui_state.debug_menu_active)
    return false;

  // Edge detection: button is down now but wasn't down last frame
  return (context.ui_state.button_state & btn) &&
         !(context.ui_state.old_button_state & btn);
}

void ui_input_block_for_transition(void) {
  // Block all currently pressed buttons
  button_block_mask |= context.ui_state.button_state;

  // Activate touch blocking until finger is lifted
  touch_block_active = true;
}

void ui_input_clear_button_blocks(void) {
  // Clear button blocks by keeping only currently pressed buttons blocked
  // This allows buttons to work again once they're released and re-pressed
  button_block_mask &= context.ui_state.button_state;
}

// ============================================================================
// Touch Input Implementation
// ============================================================================

bool ui_input_is_touching(void) {
  SceTouchData touch;
  sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
  return touch.reportNum > 0;
}

float ui_input_get_touch_x(void) {
  SceTouchData touch;
  sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
  if (touch.reportNum > 0) {
    return (float)touch.report[0].x;
  }
  return 0.0f;
}

float ui_input_get_touch_y(void) {
  SceTouchData touch;
  sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
  if (touch.reportNum > 0) {
    return (float)touch.report[0].y;
  }
  return 0.0f;
}

bool ui_input_is_touch_blocked(void) {
  return touch_block_active;
}

// ============================================================================
// Hit Testing Utilities
// ============================================================================

bool ui_input_point_in_circle(float px, float py, int cx, int cy, int radius) {
  float dx = px - (float)cx;
  float dy = py - (float)cy;
  return (dx * dx + dy * dy) <= (float)(radius * radius);
}

bool ui_input_point_in_rect(float px, float py, int rx, int ry, int rw, int rh) {
  return (px >= (float)rx && px <= (float)(rx + rw) &&
          py >= (float)ry && py <= (float)(ry + rh));
}

// ============================================================================
// Internal Functions (used by ui.c, exposed via ui_internal.h)
// ============================================================================

/**
 * Check if a button has been newly pressed (internal alias for compatibility)
 *
 * This is the original function name used throughout ui.c.
 * Exposed via ui_internal.h for cross-module access.
 */
bool btn_pressed(SceCtrlButtons btn) {
  return ui_input_btn_pressed(btn);
}

/**
 * Block inputs for transition (internal alias for compatibility)
 *
 * Original function name used in ui.c.
 * Exposed via ui_internal.h for cross-module access.
 */
void block_inputs_for_transition(void) {
  ui_input_block_for_transition();
}

/**
 * Check if a point is inside a circle (internal alias for compatibility)
 *
 * Original function name used in ui.c for wave navigation.
 * Exposed via ui_internal.h for cross-module access.
 */
bool is_point_in_circle(float px, float py, int cx, int cy, int radius) {
  return ui_input_point_in_circle(px, py, cx, cy, radius);
}

/**
 * Check if a point is inside a rectangle (internal alias for compatibility)
 *
 * Original function name used in ui.c for card/button hit testing.
 * Exposed via ui_internal.h for cross-module access.
 */
bool is_point_in_rect(float px, float py, int rx, int ry, int rw, int rh) {
  return ui_input_point_in_rect(px, py, rx, ry, rw, rh);
}

/**
 * Get direct access to button block mask (for ui.c screen transition logic)
 *
 * Some transition code directly manipulates button_block_mask.
 * This function provides controlled access for those cases.
 */
uint32_t* ui_input_get_button_block_mask_ptr(void) {
  return &button_block_mask;
}

/**
 * Get direct access to touch block active flag (for ui.c touch handling)
 *
 * Touch handling code in ui.c needs to check and modify this flag.
 * This function provides controlled access.
 */
bool* ui_input_get_touch_block_active_ptr(void) {
  return &touch_block_active;
}

/**
 * Get direct access to touch block pending clear flag (for nav collapse logic)
 *
 * Navigation collapse logic uses this flag for delayed clearing.
 * This function provides controlled access.
 */
bool* ui_input_get_touch_block_pending_clear_ptr(void) {
  return &touch_block_pending_clear;
}
