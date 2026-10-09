/**
 * @file ui_screens.h
 * @brief Screen rendering functions for VitaRPS5
 *
 * Screen implementations for the main menu hand-off, the connect flows,
 * and the overlays (waking, reconnecting, streaming, messages).
 *
 * This module contains ~2000 lines of screen rendering logic extracted from ui.c.
 */

#pragma once

#include "ui_types.h"

// ============================================================================
// Screen Initialization
// ============================================================================

/**
 * Initialize screen-specific state (settings tabs, selections, etc.)
 */
void ui_screens_init(void);

// ============================================================================
// Main Screens
// ============================================================================

/**
 * Render the main menu (Home) screen: the XMB Home (ui_home.c)
 * @return next screen to display
 */
UIScreenType ui_screen_draw_main(void);

/**
 * Run the Confirm flow for a console: PIN screen, wake or connect
 * (cooldown gate, RP_IN_USE retry reset and force_psn_holepunch handling included).
 * @param host Console to act on (NULL is ignored)
 * @return screen to show next (UI_SCREEN_TYPE_MAIN when nothing started)
 */
UIScreenType ui_screens_connect_host(VitaChiakiHost *host);

/**
 * Unregister a paired console and send the user to the PIN screen to pair it again.
 * @param host Console to re-pair (NULL or unpaired is ignored)
 * @return UI_SCREEN_TYPE_REGISTER_HOST, or UI_SCREEN_TYPE_MAIN when nothing was done
 */
UIScreenType ui_screens_repair_host(VitaChiakiHost *host);

// ============================================================================
// Overlay Screens
// ============================================================================

/**
 * Render the waking screen overlay (console wake-up + fast connect)
 * Shows spinner and connection stage messages
 * @return next screen to display
 */
UIScreenType ui_screen_draw_waking(void);

/**
 * Render the reconnecting screen overlay (after packet loss)
 * Shows reconnection progress with spinner
 * @return next screen to display
 */
UIScreenType ui_screen_draw_reconnecting(void);

/**
 * Render the stream overlay (during active streaming)
 * Shows latency stats, network indicators, and stream info
 * @return true to continue streaming, false to exit
 */
bool ui_screen_draw_stream(void);

/**
 * Render the messages screen (log viewer)
 * Scrollable message log with timestamps
 * @return true to stay on messages screen, false to exit
 */
bool ui_screen_draw_messages(void);
