/**
 * @file ui_focus.c
 * @brief Centralized focus manager implementation
 */

#include "context.h"
#include "ui/ui_focus.h"
#include "ui/ui_input.h"
#include "ui/ui_navigation.h"
#include <psp2/ctrl.h>

// Focus stack - supports base state + modal overlays
static FocusState g_focus_stack[UI_FOCUS_MAX_STACK_DEPTH];
static int g_stack_depth = 0;

// Convenience macro for current focus
#define CURRENT_FOCUS (g_focus_stack[g_stack_depth])

void ui_focus_init(void) {
    g_stack_depth = 0;
    g_focus_stack[0].zone = FOCUS_ZONE_MAIN_CONTENT;
    g_focus_stack[0].index = 0;
}

// ============================================================================
// Zone Queries
// ============================================================================

FocusZone ui_focus_get_zone(void) {
    return CURRENT_FOCUS.zone;
}

int ui_focus_get_index(void) {
    return CURRENT_FOCUS.index;
}

bool ui_focus_is_nav_bar(void) {
    return CURRENT_FOCUS.zone == FOCUS_ZONE_NAV_BAR;
}

bool ui_focus_is_content(void) {
    return CURRENT_FOCUS.zone != FOCUS_ZONE_NAV_BAR &&
           CURRENT_FOCUS.zone != FOCUS_ZONE_MODAL;
}

// ============================================================================
// Zone Transitions
// ============================================================================

void ui_focus_set_zone(FocusZone zone) {
    CURRENT_FOCUS.zone = zone;
}

void ui_focus_set_index(int index) {
    CURRENT_FOCUS.index = index;
}

void ui_focus_move_to_nav_bar(void) {
    CURRENT_FOCUS.zone = FOCUS_ZONE_NAV_BAR;
}

void ui_focus_move_to_content(UIScreenType screen) {
    CURRENT_FOCUS.zone = ui_focus_zone_for_screen(screen);
}

// ============================================================================
// Modal Focus Stack
// ============================================================================

void ui_focus_push_modal(void) {
    if (g_stack_depth >= UI_FOCUS_MAX_STACK_DEPTH - 1) {
        // Stack overflow - log error and return without pushing
        LOGE("Focus stack overflow: cannot push modal (depth=%d, max=%d)",
             g_stack_depth, UI_FOCUS_MAX_STACK_DEPTH - 1);
        return;
    }
    g_stack_depth++;
    g_focus_stack[g_stack_depth].zone = FOCUS_ZONE_MODAL;
    g_focus_stack[g_stack_depth].index = 0;
}

void ui_focus_pop_modal(void) {
    if (g_stack_depth <= 0) {
        // Stack underflow - already at base, log error and return without popping
        LOGE("Focus stack underflow: cannot pop modal (depth=%d)", g_stack_depth);
        return;
    }
    g_stack_depth--;
}

bool ui_focus_has_modal(void) {
    return g_stack_depth > 0 && CURRENT_FOCUS.zone == FOCUS_ZONE_MODAL;
}

int ui_focus_get_stack_depth(void) {
    return g_stack_depth;
}

// ============================================================================
// Input Handling
// ============================================================================

FocusZone ui_focus_zone_for_screen(UIScreenType screen) {
    switch (screen) {
        case UI_SCREEN_TYPE_MAIN:
            return FOCUS_ZONE_MAIN_CONTENT;
        case UI_SCREEN_TYPE_SETTINGS:
            return FOCUS_ZONE_SETTINGS_ITEMS;
        case UI_SCREEN_TYPE_PROFILE:
            return FOCUS_ZONE_PROFILE_CARDS;
        case UI_SCREEN_TYPE_CONTROLLER:
            return FOCUS_ZONE_CONTROLLER_CONTENT;
        default:
            return FOCUS_ZONE_MAIN_CONTENT;
    }
}

bool ui_focus_handle_zone_crossing(UIScreenType current_screen) {
    // Modal traps all input - no zone crossing allowed
    if (ui_focus_has_modal()) {
        return false;
    }

    // Note: LEFT navigation (content -> nav bar) was removed to avoid
    // interfering with content-specific LEFT/RIGHT navigation.
    // Nav bar is now accessible only via touch on the pill.

    // RIGHT: Move to content (from nav bar)
    if (ui_input_btn_pressed(SCE_CTRL_RIGHT) && ui_focus_is_nav_bar()) {
        ui_focus_move_to_content(current_screen);
        ui_nav_request_collapse(true);
        return true;  // Input consumed
    }

    return false;  // Input not consumed, let screen handle it
}
