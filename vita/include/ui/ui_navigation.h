/**
 * @file ui_navigation.h
 * @brief Navigation system for VitaRPS5 UI
 *
 * This module handles the wave sidebar navigation with collapse/expand animation,
 * navigation pill rendering, touch input, and procedural icon drawing.
 *
 * Phase 5 extraction: ~1200 lines from ui.c
 */

#pragma once

#include <stdbool.h>
#include "ui_types.h"

// ============================================================================
// Initialization
// ============================================================================

/**
 * Initialize the navigation system
 * Must be called once during UI initialization
 */
void ui_nav_init(void);

// ============================================================================
// Rendering
// ============================================================================

/**
 * Render the complete wave navigation sidebar
 * Handles expanded state, collapsed state, and animations
 */
void ui_nav_render(void);

/**
 * Render the navigation pill (collapsed state indicator)
 */
void ui_nav_render_pill(void);

/**
 * Render the toast message shown when navigation collapses
 */
void ui_nav_render_toast(void);

/**
 * Render semi-transparent overlay on content when nav is focused
 */
void ui_nav_render_content_overlay(void);

// ============================================================================
// State Queries
// ============================================================================

/**
 * Check if navigation sidebar is expanded
 * @return true if in NAV_STATE_EXPANDED
 */
bool ui_nav_is_expanded(void);

/**
 * Check if navigation sidebar is collapsed
 * @return true if in NAV_STATE_COLLAPSED
 */
bool ui_nav_is_collapsed(void);

/**
 * Check if navigation is currently animating
 * @return true if in NAV_STATE_COLLAPSING or NAV_STATE_EXPANDING
 */
bool ui_nav_is_animating(void);

/**
 * Get current navigation width (for animated layout)
 * @return Width in pixels (0.0 to WAVE_NAV_WIDTH)
 */
float ui_nav_get_current_width(void);

/**
 * Get current navigation state
 * @return Current NavSidebarState
 */
NavSidebarState ui_nav_get_state(void);

// ============================================================================
// State Control
// ============================================================================

/**
 * Request navigation sidebar to collapse
 */
void ui_nav_request_collapse(void);

/**
 * Request navigation sidebar to expand
 */
void ui_nav_request_expand(void);

/**
 * Toggle navigation between expanded and collapsed states
 */
void ui_nav_toggle(void);

/**
 * Reset navigation to collapsed state immediately (no animation)
 */
void ui_nav_reset_collapsed(void);

// ============================================================================
// Selection & Focus
// ============================================================================

/**
 * Get currently selected navigation icon index
 * @return Icon index (0=Play, 1=Settings, 2=Controller, 3=Profile)
 */
int ui_nav_get_selected_icon(void);

/**
 * Set selected navigation icon
 * @param index Icon index to select (0-3)
 */
void ui_nav_set_selected_icon(int index);

/**
 * Map icon index to corresponding screen type
 * @param index Icon index (0-3)
 * @return Corresponding UIScreenType
 */
UIScreenType ui_nav_screen_for_icon(int index);

/**
 * Map screen type to corresponding icon index
 * @param screen Screen type
 * @return Icon index (0=Play, 1=Settings, 2=Controller, 3=Profile)
 */
int ui_nav_icon_for_screen(UIScreenType screen);

// Legacy focus getters/setters removed in Phase 4
// Use ui_focus_get_zone()/ui_focus_set_zone() from ui_focus.h instead

// ============================================================================
// Input Handling
// ============================================================================

/**
 * Handle touch input on navigation icons
 * @param x Touch X coordinate (screen space)
 * @param y Touch Y coordinate (screen space)
 * @param out_screen Output parameter for selected screen type (can be NULL)
 * @return true if touch hit a navigation icon
 */
bool ui_nav_handle_touch(float x, float y, UIScreenType *out_screen);

/**
 * Handle touch input on collapsed navigation pill
 * @param x Touch X coordinate (screen space)
 * @param y Touch Y coordinate (screen space)
 * @return true if touch hit the pill
 */
bool ui_nav_handle_pill_touch(float x, float y);

/**
 * Handle global navigation shortcuts (Triangle button, D-pad navigation)
 * @param current_screen Current screen type (for icon sync on expand)
 * @param out_screen Output parameter for selected screen type (can be NULL)
 * @param allow_dpad True to enable D-pad navigation handling
 * @return true if navigation action was taken (screen change)
 */
bool ui_nav_handle_shortcuts(UIScreenType current_screen, UIScreenType *out_screen, bool allow_dpad);

// ============================================================================
// Update Functions
// ============================================================================

/**
 * Update wave animation state
 * Call once per frame when navigation is visible
 */
void ui_nav_update_wave_animation(void);

/**
 * Update collapse/expand animation
 * Called automatically by ui_nav_render()
 */
void ui_nav_update_collapse_animation(void);

/**
 * Update toast message state
 * Called automatically by ui_nav_render()
 */
void ui_nav_update_toast(void);

// ============================================================================
// Procedural Icon Drawing (Fallback if textures fail to load)
// ============================================================================

/**
 * Draw play/stream icon (triangle pointing right)
 * @param cx Center X coordinate
 * @param cy Center Y coordinate
 * @param size Icon size in pixels
 */
void ui_nav_draw_play_icon(int cx, int cy, int size);

/**
 * Draw settings icon (gear)
 * @param cx Center X coordinate
 * @param cy Center Y coordinate
 * @param size Icon size in pixels
 */
void ui_nav_draw_settings_icon(int cx, int cy, int size);

/**
 * Draw controller icon (gamepad)
 * @param cx Center X coordinate
 * @param cy Center Y coordinate
 * @param size Icon size in pixels
 */
void ui_nav_draw_controller_icon(int cx, int cy, int size);

/**
 * Draw profile icon (user silhouette)
 * @param cx Center X coordinate
 * @param cy Center Y coordinate
 * @param size Icon size in pixels
 */
void ui_nav_draw_profile_icon(int cx, int cy, int size);

/**
 * Draw hamburger menu icon (three horizontal lines)
 * @param x Left edge X coordinate
 * @param cy Center Y coordinate
 * @param size Icon size in pixels
 * @param color Icon color (ABGR format)
 */
void ui_nav_draw_hamburger_icon(int x, int cy, int size, uint32_t color);
