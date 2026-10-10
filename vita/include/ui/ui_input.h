/**
 * @file ui_input.h
 * @brief Input handling API for VitaRPS5 UI system
 *
 * This module handles all controller and touch screen input for the UI,
 * including button state tracking, hit testing, and input blocking during
 * screen transitions.
 *
 * Key features:
 * - Button press detection with transition blocking
 * - Touch state management and blocking
 * - Geometric hit testing utilities (circle, rectangle)
 * - Input cooldown/gating mechanisms
 *
 * Usage:
 * - Call ui_input_init() during UI initialization
 * - Use ui_input_btn_pressed() to check for new button presses
 * - Use hit testing functions for touch/pointer collision detection
 * - Call ui_input_block_for_transition() before screen changes
 */

#pragma once

#include <psp2/ctrl.h>
#include <stdbool.h>
#include <stdint.h>

#include "ui_component.h"

// ============================================================================
// Initialization
// ============================================================================

/**
 * Initialize input handling subsystem
 *
 * Sets up button block mask and touch blocking state.
 * Must be called before other input functions.
 */
void ui_input_init(void);

// ============================================================================
// Button Input
// ============================================================================

/**
 * Check if a button has been newly pressed this frame
 *
 * Returns true only on the first frame a button is pressed (edge detection).
 * Automatically filters out:
 * - Buttons blocked by transition blocking
 * - Input when error popup or debug menu is active
 *
 * @param btn Button(s) to check (can use bitwise OR for multiple buttons)
 * @return true if button was just pressed, false otherwise
 */
bool ui_input_btn_pressed(SceCtrlButtons btn);

/**
 * Block currently pressed buttons for screen transitions
 *
 * Prevents accidental button presses from carrying over into the next screen.
 * Also activates touch blocking to prevent touch events during transition.
 *
 * Call this before changing UI screens or showing overlays.
 */
void ui_input_block_for_transition(void);

/**
 * Block a specific button for the rest of this frame
 *
 * Prevents the specified button from being detected as pressed by subsequent
 * handlers in the same frame. Useful when zone crossing or other global
 * handlers consume a button and want to prevent it from being processed
 * by screen-specific handlers.
 *
 * @param btn Button(s) to block
 */
void ui_input_block_button(SceCtrlButtons btn);

// ============================================================================
// Per-Frame Snapshot
// ============================================================================

/**
 * Rebuild the per-frame input snapshot used by XMB components.
 *
 * Call once per UI frame, after the controller and front touch have been read and
 * before any screen runs. Buttons are resolved to logical actions (Circle Button
 * Confirm swap applied), D-pad hold-repeat is computed, and touch is mapped to
 * screen pixels. Respects the button block mask and the touch block set by
 * ui_input_block_for_transition(), and reports nothing while an error popup or the
 * debug menu owns input, so a press that opened a screen never acts on the next.
 */
void ui_input_update_snapshot(void);

/**
 * Mark the touch that is down right now as used by the screen (a long-press that opened
 * Options). The release of that touch is then not a tap anywhere (ui_touch_tap() is false), so
 * lifting the finger neither closes what the long-press opened nor activates the row under it.
 * Takes effect from the next frame's snapshot and resets on the next touch-down.
 */
void ui_input_consume_touch(void);

/**
 * The snapshot built by the last ui_input_update_snapshot() call (never NULL).
 */
const UiInput *ui_input_snapshot(void);

// ============================================================================
// Hit Testing Utilities
// ============================================================================

/**
 * Check if a point is inside a rectangle
 *
 * Used for button/card collision detection.
 *
 * @param px Point X coordinate
 * @param py Point Y coordinate
 * @param rx Rectangle left edge
 * @param ry Rectangle top edge
 * @param rw Rectangle width
 * @param rh Rectangle height
 * @return true if point is inside rectangle (inclusive)
 */
bool ui_input_point_in_rect(float px, float py, int rx, int ry, int rw, int rh);

/**
 * Check if a point is inside a circle
 *
 * Used for wave navigation icon hit detection.
 *
 * @param px Point X coordinate
 * @param py Point Y coordinate
 * @param cx Circle center X
 * @param cy Circle center Y
 * @param radius Circle radius
 * @return true if point is inside circle
 */
bool ui_input_point_in_circle(float px, float py, int cx, int cy, int radius);
