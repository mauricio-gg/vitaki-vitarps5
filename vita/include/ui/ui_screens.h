/**
 * @file ui_screens.h
 * @brief Screen rendering functions for VitaRPS5
 *
 * Screen implementations for the main menu hand-off, the connect flows,
 * and the overlays (waking, reconnecting, streaming, messages).
 */

#pragma once

#include "ui_types.h"

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
 * Send the user to the PIN screen to pair a discovered, unpaired console.
 * @param host Console to pair (NULL or already paired is ignored)
 * @return UI_SCREEN_TYPE_REGISTER_HOST, or UI_SCREEN_TYPE_MAIN when nothing was done
 */
UIScreenType ui_screens_pair_host(VitaChiakiHost *host);

/**
 * Send the user to the PIN screen for a console found by the IP probe (a new pair, or a re-pair
 * when the host is already paired, with no confirm popup). The PIN screen takes ownership of the
 * host: see ui_pin_adopt_probed_host().
 * @param host Host from discovery_probe_take_host() (NULL is ignored)
 * @return UI_SCREEN_TYPE_REGISTER_HOST, or UI_SCREEN_TYPE_MAIN when nothing was done
 */
UIScreenType ui_screens_pair_probed_host(VitaChiakiHost *host);

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
