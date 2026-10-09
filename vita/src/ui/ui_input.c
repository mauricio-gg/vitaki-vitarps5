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

#include "ui/ui_gesture.h"
#include "ui/ui_input.h"
#include "ui/ui_internal.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/touch.h>
#include <string.h>

#include "ui/ui_theme.h"

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

  // Block all input when the debug menu is active
  if (context.ui_state.debug_menu_active)
    return false;

  // Edge detection: button is down now but wasn't down last frame
  return (context.ui_state.button_state & btn) && !(context.ui_state.old_button_state & btn);
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

void ui_input_block_button(SceCtrlButtons btn) {
  // Block specific button(s) for the rest of this frame
  // Useful when a button action should not be processed by subsequent handlers
  button_block_mask |= btn;
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
// Per-Frame Snapshot
// ============================================================================

/** Maps one logical button to the physical Vita button(s) that produce it. */
typedef struct {
  UiButton logical;
  uint32_t physical;
} ButtonMapping;

/** Touch-snapshot state carried between frames. */
static bool snap_touch_active = false;  ///< last frame's unblocked touch-down state
static bool snap_touch_dragged = false;
static bool snap_touch_long_press_fired = false;  ///< the long-press already fired in this touch
static bool snap_touch_consumed = false;          ///< a screen acted on this touch
static uint64_t snap_touch_down_us = 0;
static float snap_touch_start_x = 0.0f;
static float snap_touch_start_y = 0.0f;
static float snap_touch_x = 0.0f;
static float snap_touch_y = 0.0f;

/** D-pad hold-repeat state, indexed by position in snap_dpad[]. */
static uint64_t snap_hold_start_us[4];
static uint64_t snap_last_repeat_us[4];

static const UiButton snap_dpad[4] = {UI_BTN_UP, UI_BTN_DOWN, UI_BTN_LEFT, UI_BTN_RIGHT};

/** Convert a physical button mask to logical UiButton bits. */
static uint32_t to_logical(uint32_t physical, const ButtonMapping *map, int count) {
  uint32_t logical = 0;
  for (int i = 0; i < count; i++) {
    if (physical & map[i].physical)
      logical |= (uint32_t)map[i].logical;
  }
  return logical;
}

/** Fill out->repeat: the press edge of each D-pad direction, then hold-repeat while held. */
static void snapshot_dpad_repeat(UiInput *out, uint64_t now_us) {
  out->repeat = 0;
  for (int i = 0; i < 4; i++) {
    uint32_t bit = (uint32_t)snap_dpad[i];
    if (out->pressed & bit) {
      snap_hold_start_us[i] = now_us;
      snap_last_repeat_us[i] = now_us;
      out->repeat |= bit;
    } else if (out->down & bit) {
      bool held_long_enough = now_us - snap_hold_start_us[i] >= UI_REPEAT_DELAY_MS * 1000ULL;
      bool interval_elapsed = now_us - snap_last_repeat_us[i] >= UI_REPEAT_INTERVAL_MS * 1000ULL;
      if (held_long_enough && interval_elapsed) {
        snap_last_repeat_us[i] = now_us;
        out->repeat |= bit;
      }
    } else {
      snap_hold_start_us[i] = 0;
    }
  }
}

/**
 * Fill out->touch from the front panel, honouring the transition touch block. Tap, swipe and
 * long-press are decided by ui_gesture_classify(); @now_us is the frame's clock.
 */
static void snapshot_touch(UiInput *out, bool suppressed, uint64_t now_us) {
  const SceTouchData *panel = &context.ui_state.touch_state_front;
  bool raw_down = panel->reportNum > 0;

  if (touch_block_active) {
    if (!raw_down) {
      touch_block_active = false;
      touch_block_pending_clear = false;
    }
  }
  bool active = raw_down && !touch_block_active && !suppressed;

  if (raw_down) {
    snap_touch_x = ((float)panel->report[0].x / (float)VITA_TOUCH_PANEL_WIDTH) * (float)VITA_WIDTH;
    snap_touch_y =
        ((float)panel->report[0].y / (float)VITA_TOUCH_PANEL_HEIGHT) * (float)VITA_HEIGHT;
  }

  UiTouch *t = &out->touch;
  t->pressed = active && !snap_touch_active;
  t->released = !raw_down && snap_touch_active;
  t->down = active;

  if (t->pressed) {
    snap_touch_start_x = snap_touch_x;
    snap_touch_start_y = snap_touch_y;
    snap_touch_dragged = false;
    snap_touch_long_press_fired = false;
    snap_touch_consumed = false;
    snap_touch_down_us = now_us;
  }
  t->x = snap_touch_x;
  t->y = snap_touch_y;
  t->dx = snap_touch_x - snap_touch_start_x;
  t->dy = snap_touch_y - snap_touch_start_y;

  t->long_press = false;
  if (active || t->released) {
    const UiGestureTouch gesture = {
        .released = t->released,
        .held_ms = (uint32_t)((now_us - snap_touch_down_us) / 1000ULL),
        .dx = t->dx,
        .dy = t->dy,
        .swiped = snap_touch_dragged,
        .long_press_fired = snap_touch_long_press_fired,
        .consumed = snap_touch_consumed,
    };
    const UiGestureKind kind = ui_gesture_classify(&gesture);
    if (kind == UI_GESTURE_SWIPE)
      snap_touch_dragged = true;
    if (kind == UI_GESTURE_LONG_PRESS) {
      snap_touch_long_press_fired = true;
      t->long_press = true;
    }
  }
  t->dragged = (active || t->released) && snap_touch_dragged;
  t->consumed = (active || t->released) && snap_touch_consumed;

  /* A touch that becomes blocked mid-way is swallowed: it neither taps nor releases. */
  snap_touch_active = active;
}

static UiInput frame_snapshot;

void ui_input_consume_touch(void) {
  snap_touch_consumed = true;
}

void ui_input_update_snapshot(void) {
  UiInput *out = &frame_snapshot;
  const bool circle_confirm = context.config.circle_btn_confirm;
  const ButtonMapping map[] = {
      {UI_BTN_CONFIRM, circle_confirm ? SCE_CTRL_CIRCLE : SCE_CTRL_CROSS},
      {UI_BTN_CANCEL, circle_confirm ? SCE_CTRL_CROSS : SCE_CTRL_CIRCLE},
      {UI_BTN_OPTIONS, SCE_CTRL_TRIANGLE},
      {UI_BTN_CLEAR, SCE_CTRL_SQUARE},
      {UI_BTN_FILTER, SCE_CTRL_START},
      {UI_BTN_BROWSER, SCE_CTRL_SELECT},
      {UI_BTN_L, SCE_CTRL_LTRIGGER},
      {UI_BTN_R, SCE_CTRL_RTRIGGER},
      {UI_BTN_UP, SCE_CTRL_UP},
      {UI_BTN_DOWN, SCE_CTRL_DOWN},
      {UI_BTN_LEFT, SCE_CTRL_LEFT},
      {UI_BTN_RIGHT, SCE_CTRL_RIGHT},
  };
  const int map_count = (int)(sizeof(map) / sizeof(map[0]));

  memset(out, 0, sizeof(*out));
  const bool suppressed = context.ui_state.debug_menu_active;

  if (!suppressed) {
    const uint32_t state = context.ui_state.button_state;
    const uint32_t old_state = context.ui_state.old_button_state;
    const uint32_t unblocked = ~button_block_mask;
    out->down = to_logical(state & unblocked, map, map_count);
    out->pressed = to_logical(state & ~old_state & unblocked, map, map_count);
    out->released = to_logical(~state & old_state & unblocked, map, map_count);
  }

  const uint64_t now_us = sceKernelGetProcessTimeWide();
  snapshot_dpad_repeat(out, now_us);
  snapshot_touch(out, suppressed, now_us);
}

const UiInput *ui_input_snapshot(void) {
  return &frame_snapshot;
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
  return (px >= (float)rx && px <= (float)(rx + rw) && py >= (float)ry && py <= (float)(ry + rh));
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

bool btn_down(SceCtrlButtons btn) {
  if (button_block_mask & btn)
    return false;
  if (context.ui_state.debug_menu_active)
    return false;
  return (context.ui_state.button_state & btn) != 0;
}

bool btn_released(SceCtrlButtons btn) {
  if (button_block_mask & btn)
    return false;
  if (context.ui_state.debug_menu_active)
    return false;
  return !(context.ui_state.button_state & btn) && (context.ui_state.old_button_state & btn);
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
uint32_t *ui_input_get_button_block_mask_ptr(void) {
  return &button_block_mask;
}

/**
 * Get direct access to touch block active flag (for ui.c touch handling)
 *
 * Touch handling code in ui.c needs to check and modify this flag.
 * This function provides controlled access.
 */
bool *ui_input_get_touch_block_active_ptr(void) {
  return &touch_block_active;
}

/**
 * Get direct access to touch block pending clear flag (for nav collapse logic)
 *
 * Navigation collapse logic uses this flag for delayed clearing.
 * This function provides controlled access.
 */
bool *ui_input_get_touch_block_pending_clear_ptr(void) {
  return &touch_block_pending_clear;
}
