/**
 * @file ui_focus.h
 * @brief Centralized focus manager for VitaRPS5 UI
 *
 * This module owns all focus state and handles zone-crossing navigation.
 * Screen handlers should only process intra-zone (UP/DOWN) input.
 */

#pragma once

#include <stdbool.h>
#include "ui_types.h"

// Maximum modal stack depth
#define UI_FOCUS_MAX_STACK_DEPTH 4

// Focus zones - each represents a distinct input handling region
typedef enum {
    FOCUS_ZONE_NAV_BAR,           // Wave navigation sidebar icons
    FOCUS_ZONE_MAIN_CONTENT,      // Main menu console cards
    FOCUS_ZONE_SETTINGS_ITEMS,    // Settings screen item list
    FOCUS_ZONE_PROFILE_CARDS,     // Profile screen info/connection cards
    FOCUS_ZONE_CONTROLLER_CONTENT, // Controller screen content (tabs + items)
    FOCUS_ZONE_MODAL              // Modal overlay (registration, popups, etc.)
} FocusZone;

// Focus state for a single level in the stack
typedef struct {
    FocusZone zone;
    int index;                    // Selected item within zone
} FocusState;

// ============================================================================
// Initialization
// ============================================================================

void ui_focus_init(void);

// ============================================================================
// Zone Queries
// ============================================================================

FocusZone ui_focus_get_zone(void);
int ui_focus_get_index(void);
bool ui_focus_is_nav_bar(void);
bool ui_focus_is_content(void);

// ============================================================================
// Zone Transitions
// ============================================================================

void ui_focus_set_zone(FocusZone zone);
void ui_focus_set_index(int index);
void ui_focus_move_to_nav_bar(void);
void ui_focus_move_to_content(UIScreenType screen);

// ============================================================================
// Modal Focus Stack
// ============================================================================

void ui_focus_push_modal(void);
void ui_focus_pop_modal(void);
bool ui_focus_has_modal(void);
int ui_focus_get_stack_depth(void);

// ============================================================================
// Input Handling
// ============================================================================

/**
 * Handle zone-crossing input (RIGHT to exit nav bar to content).
 * Note: LEFT navigation to open nav bar has been removed to avoid interfering
 * with content-specific navigation. Nav bar is now accessible only via touch.
 * Call this FIRST in each frame, before screen-specific handling.
 *
 * @param current_screen The currently displayed screen type
 * @return true if input was consumed (zone crossing occurred), false otherwise
 */
bool ui_focus_handle_zone_crossing(UIScreenType current_screen);

/**
 * Get the default content zone for a given screen
 */
FocusZone ui_focus_zone_for_screen(UIScreenType screen);

// ============================================================================
// Legacy Compatibility (temporary, for gradual migration)
// ============================================================================

// These map to the old FocusArea enum for backward compatibility
#define FOCUS_COMPAT_NAV_BAR     FOCUS_ZONE_NAV_BAR
#define FOCUS_COMPAT_CONSOLE_CARDS FOCUS_ZONE_MAIN_CONTENT
