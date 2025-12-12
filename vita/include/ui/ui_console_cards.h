/**
 * @file ui_console_cards.h
 * @brief Console card rendering and state management
 *
 * Handles the display of console cards in the main UI:
 * - Grid/vertical layout of console cards
 * - Individual card rendering with status, logos, and names
 * - Card selection and focus animations
 * - Card cache to prevent flickering during discovery updates
 * - Host-to-card mapping logic
 */

#pragma once

#include <stdbool.h>
#include "ui_types.h"

// ============================================================================
// Initialization
// ============================================================================

/**
 * ui_cards_init() - Initialize console card system
 *
 * Sets up the card cache and animation state.
 * Call once during UI initialization.
 */
void ui_cards_init(void);

// ============================================================================
// Rendering
// ============================================================================

/**
 * ui_cards_render_grid() - Render the console card grid
 *
 * Renders all console cards in a vertical layout, centered in the content area.
 * Handles card caching, focus animation, and cooldown states.
 * Call once per frame when the main screen is active.
 */
void ui_cards_render_grid(void);

/**
 * ui_cards_render_single() - Render a single console card
 * @console: Console card info to render
 * @x: X position (top-left corner)
 * @y: Y position (top-left corner)
 * @selected: Whether this card is currently selected
 * @cooldown_for_card: Whether this card is in streaming cooldown
 * @scale: Scale factor (0.95 to 1.0 for focus animation)
 *
 * Renders a single console card with logo, name, status indicator, and state text.
 * Handles both PS4 and PS5 consoles, paired/unpaired states, and visual feedback.
 */
void ui_cards_render_single(ConsoleCardInfo* console, int x, int y, bool selected,
                             bool cooldown_for_card, float scale);

// ============================================================================
// Selection & State
// ============================================================================

/**
 * ui_cards_get_selected_index() - Get the currently selected card index
 *
 * Returns: Index of the selected console card (0-based)
 */
int ui_cards_get_selected_index(void);

/**
 * ui_cards_set_selected_index() - Set the selected card index
 * @index: New selected card index
 *
 * Updates the selected card index and triggers focus animation.
 */
void ui_cards_set_selected_index(int index);

/**
 * ui_cards_get_count() - Get the number of valid console cards
 *
 * Returns: Number of cards in the current cache
 */
int ui_cards_get_count(void);

// ============================================================================
// Cache Management
// ============================================================================

/**
 * ui_cards_update_cache() - Update the console card cache
 * @force_update: If true, bypass the 10-second throttle
 *
 * Refreshes the card cache from the global host list.
 * Normally throttled to every 10 seconds to prevent flickering during
 * discovery updates. Use force_update=true when a manual refresh is needed
 * (e.g., after host registration or deletion).
 */
void ui_cards_update_cache(bool force_update);

/**
 * ui_cards_map_host() - Map a host to a console card
 * @host: Source host data
 * @card: Destination card info (output)
 *
 * Converts a VitaChiakiHost into a ConsoleCardInfo for rendering.
 * Extracts name, IP, status, and state information from the host.
 */
void ui_cards_map_host(VitaChiakiHost* host, ConsoleCardInfo* card);
