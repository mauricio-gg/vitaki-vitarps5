/**
 * @file ui_console_cards.h
 * @brief Console list data for the Home screen
 *
 * - Card cache (paired consoles only, sorted by name; filtered) that prevents flicker
 *   during discovery updates
 * - Host-to-card mapping logic
 * - The selected console and the console filter (name or IP address)
 */

#pragma once

#include <stdbool.h>
#include "ui_console_status.h"
#include "ui_types.h"

/** Size in bytes of a filter text buffer, terminator included (at most 31 bytes of text). */
#define UI_FILTER_TEXT_MAX 32

// ============================================================================
// Initialization
// ============================================================================

/**
 * ui_cards_init() - Initialize console card system
 *
 * Sets up the card cache and filter state.
 * Call once during UI initialization.
 */
void ui_cards_init(void);

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
 * Clamps the index to the cache and records it as the selected console.
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
 * ui_cards_mark_dirty() - Flag the card cache as stale
 *
 * Called by host-removal code paths outside the UI module (host_storage.c's
 * update_context_hosts(), discovery.c's remove_lost_discovered_hosts(),
 * psn_remote.c's remove_existing_psn_hosts()) after a host is freed/removed from
 * context.hosts[]. Makes the next ui_cards_update_cache() call rebuild immediately
 * instead of waiting out the normal CARD_CACHE_UPDATE_INTERVAL_US throttle, so a
 * removed host's card disappears within one frame rather than lingering up to 10s.
 * Safe to call from any thread: sets a single volatile bool.
 */
void ui_cards_mark_dirty(void);

/**
 * ui_cards_map_host() - Map a host to a console card
 * @host: Source host data
 * @card: Destination card info (output)
 *
 * Converts a VitaChiakiHost into a ConsoleCardInfo for the Home list.
 * Extracts name, IP, status, and state information from the host.
 */
void ui_cards_map_host(VitaChiakiHost *host, ConsoleCardInfo *card);

/**
 * ui_cards_get_total_count() - Consoles known before the filter is applied
 *
 * Returns: Number of paired consoles the list would show with no filter
 */
int ui_cards_get_total_count(void);

/**
 * ui_cards_get_card() - Get a cached card by position
 * @index: Position in the cache (sorted order)
 * Returns: Pointer to the card, or NULL if @index is out of range
 */
ConsoleCardInfo *ui_cards_get_card(int index);

/**
 * ui_cards_classify() - Decide what status a cached console shows (see ui_console_classify()).
 * @card:     The console.
 * @token_ok: The PSN token is valid, so a PSN route counts as reachable.
 * @cooldown: The post-stream cooldown is active for this console.
 * @message:  What the console's live status message counts as (ui_cards_message()).
 */
UiConsoleState ui_cards_classify(const ConsoleCardInfo *card, bool token_ok, bool cooldown,
                                 UiConsoleMessage message);

/**
 * ui_cards_message() - What the status message of a cached console counts as right now:
 * none (no message or expired), Error or Retrying (see ui_console_message_class()).
 */
UiConsoleMessage ui_cards_message(const ConsoleCardInfo *card);

// ============================================================================
// Filter
// ============================================================================

/**
 * ui_cards_open_filter() - Start shortcut: clear an active filter, otherwise open the keyboard
 */
void ui_cards_open_filter(void);

/**
 * ui_cards_edit_filter() - Open the system keyboard ("Filter Consoles") with the current text
 * prefilled. Done with empty text clears the filter; Cancel keeps it.
 */
void ui_cards_edit_filter(void);

/**
 * ui_cards_clear_filter() - Drop the filter and show every console again
 */
void ui_cards_clear_filter(void);

/**
 * ui_cards_poll_filter_ime() - Poll IME dialog state (call each frame)
 */
void ui_cards_poll_filter_ime(void);

/**
 * ui_cards_is_filter_active() - Check if filter is active
 */
bool ui_cards_is_filter_active(void);

/**
 * ui_cards_get_filter_text() - The active filter text ("" when no filter is set)
 */
const char *ui_cards_get_filter_text(void);
