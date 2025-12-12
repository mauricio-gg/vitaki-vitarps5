/**
 * @file ui_components.h
 * @brief Reusable UI widgets and dialogs for VitaRPS5
 *
 * This module provides high-level UI components including:
 * - Toggle switches, dropdowns, tabs, status indicators
 * - Error popup dialog
 * - Hints popup system
 * - Debug menu (when VITARPS5_DEBUG_MENU enabled)
 *
 * All components follow the VitaRPS5 design system with PlayStation-inspired
 * styling and smooth animations.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

// ============================================================================
// Widget Drawing Functions
// ============================================================================

/**
 * Draw an animated toggle switch control
 * @param x X position of toggle
 * @param y Y position of toggle
 * @param width Total width of toggle track
 * @param height Height of toggle track
 * @param anim_value Animation progress (0.0 = OFF, 1.0 = ON)
 * @param selected true if this toggle is currently selected/focused
 */
void ui_draw_toggle_switch(int x, int y, int width, int height, float anim_value, bool selected);

/**
 * Draw a dropdown control with label and current value
 * @param x X position of dropdown
 * @param y Y position of dropdown
 * @param width Width of dropdown
 * @param height Height of dropdown
 * @param label Label text displayed on left side
 * @param value Current value text displayed on right side
 * @param expanded true if dropdown menu is currently expanded
 * @param selected true if this dropdown is currently selected/focused
 */
void ui_draw_dropdown(int x, int y, int width, int height, const char* label,
                      const char* value, bool expanded, bool selected);

/**
 * Draw a tabbed navigation bar with color-coded sections
 * @param x X position of tab bar
 * @param y Y position of tab bar
 * @param width Total width of tab bar
 * @param height Height of tab bar
 * @param tabs Array of tab label strings
 * @param colors Array of colors for each tab background
 * @param num_tabs Number of tabs in the array
 * @param selected Index of currently selected tab
 */
void ui_draw_tab_bar(int x, int y, int width, int height,
                     const char* tabs[], uint32_t colors[], int num_tabs, int selected);

/**
 * Status types for status indicator dots
 */
typedef enum {
    UI_STATUS_ACTIVE = 0,    // Green - system active/available
    UI_STATUS_STANDBY = 1,   // Yellow/Orange - system in standby
    UI_STATUS_ERROR = 2      // Red - error or unavailable
} UIStatusType;

/**
 * Draw a colored status indicator dot
 * @param x X position (center of dot)
 * @param y Y position (center of dot)
 * @param radius Radius of the dot
 * @param status Status type determining the color
 */
void ui_draw_status_dot(int x, int y, int radius, UIStatusType status);

/**
 * Draw a styled section header with title and accent line
 * @param x X position of header
 * @param y Y position of header
 * @param width Width of header
 * @param title Header title text
 */
void ui_draw_section_header(int x, int y, int width, const char* title);

/**
 * Draw a single PIN entry digit box
 * @param x X position of digit box
 * @param y Y position of digit box
 * @param digit The digit value (0-9, or >9 for empty)
 * @param is_current true if this is the currently selected digit (shows cursor)
 * @param has_value true if a digit has been entered
 */
void ui_draw_pin_digit(int x, int y, uint32_t digit, bool is_current, bool has_value);

// ============================================================================
// Toggle Switch Animation
// ============================================================================

/**
 * Start toggle switch animation
 * @param toggle_index Unique identifier for the toggle being animated
 * @param target_state Target state (true = animating to ON, false = animating to OFF)
 */
void ui_toggle_start_animation(int toggle_index, bool target_state);

/**
 * Get current animation value for a toggle switch
 * @param toggle_index Unique identifier for the toggle
 * @param current_state Current logical state of the toggle
 * @return Animation value from 0.0 (OFF) to 1.0 (ON)
 */
float ui_toggle_get_animation_value(int toggle_index, bool current_state);

// ============================================================================
// Error Popup Dialog
// ============================================================================

/**
 * Show error popup with specified message
 * @param message Error message to display (copied internally)
 */
void ui_error_show(const char* message);

/**
 * Hide the error popup
 */
void ui_error_hide(void);

/**
 * Render the error popup (call during draw loop)
 */
void ui_error_render(void);

/**
 * Handle input for error popup (call during input loop)
 */
void ui_error_handle_input(void);

/**
 * Check if error popup is currently active
 * @return true if popup is visible
 */
bool ui_error_is_active(void);

// ============================================================================
// Hints Popup System
// ============================================================================

/**
 * Trigger hints popup with specified hint text
 * @param hint_text Text to display in the hints popup
 */
void ui_hints_trigger(const char* hint_text);

/**
 * Render the hints popup (call during draw loop)
 */
void ui_hints_render(void);

/**
 * Render hints indicator in top-right corner
 */
void ui_hints_render_indicator(void);

// ============================================================================
// Debug Menu (VITARPS5_DEBUG_MENU must be enabled)
// ============================================================================

/**
 * Open the debug menu
 */
void ui_debug_open(void);

/**
 * Close the debug menu
 */
void ui_debug_close(void);

/**
 * Render the debug menu (call during draw loop)
 */
void ui_debug_render(void);

/**
 * Handle input for debug menu (call during input loop)
 */
void ui_debug_handle_input(void);

/**
 * Check if debug menu is currently active
 * @return true if debug menu is visible
 */
bool ui_debug_is_active(void);
