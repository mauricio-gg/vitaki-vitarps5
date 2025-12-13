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
 * Check if a button has been newly pressed AND content area can receive it
 *
 * Like ui_input_btn_pressed() but also blocks input when the navigation
 * menu is expanded or expanding. Use this for content area input (screens,
 * cards, etc.) to ensure nav menu has exclusive input priority when visible.
 *
 * Input priority hierarchy:
 * 1. Error popup (blocks all)
 * 2. Debug menu (blocks all)
 * 3. Nav menu expanded (blocks content, nav receives input)
 * 4. Content area (receives input when nav collapsed)
 *
 * @param btn Button(s) to check
 * @return true if button was just pressed and content can receive it
 */
bool ui_input_content_btn_pressed(SceCtrlButtons btn);

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
 * Clear all button blocks
 *
 * Resets the button block mask to allow all inputs again.
 * Typically called after a transition animation completes.
 */
void ui_input_clear_button_blocks(void);

// ============================================================================
// Touch Input
// ============================================================================

/**
 * Check if screen is currently being touched
 *
 * @return true if at least one touch point is active
 */
bool ui_input_is_touching(void);

/**
 * Get current touch X coordinate
 *
 * @return X coordinate of first touch point (0-1920), or 0 if not touching
 */
float ui_input_get_touch_x(void);

/**
 * Get current touch Y coordinate
 *
 * @return Y coordinate of first touch point (0-1088), or 0 if not touching
 */
float ui_input_get_touch_y(void);

/**
 * Check if touch blocking is currently active
 *
 * Touch blocking prevents touch input processing after screen transitions
 * until the user lifts their finger.
 *
 * @return true if touch events should be ignored
 */
bool ui_input_is_touch_blocked(void);

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
