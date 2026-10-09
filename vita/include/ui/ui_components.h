/**
 * @file ui_components.h
 * @brief Debug menu for VitaRPS5 (only reachable when VITARPS5_DEBUG_MENU is enabled)
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

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
