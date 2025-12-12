/**
 * @file ui_screens.h
 * @brief Screen rendering functions for VitaRPS5
 *
 * All screen implementations including main menu, settings, profile,
 * controller configuration, and overlays (waking, streaming, messages, etc.)
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
 * Render the main menu screen with console cards and wave navigation
 * Handles D-pad navigation, touch input, and console connection logic
 * @return next screen to display
 */
UIScreenType ui_screen_draw_main(void);

/**
 * Render the settings screen with streaming/quality controls
 * Displays toggles and dropdowns for resolution, FPS, latency mode, etc.
 * @return next screen to display
 */
UIScreenType ui_screen_draw_settings(void);

/**
 * Render the profile screen with PSN account info and registration controls
 * Shows three-column layout: profile card, connection info, registration
 * @return next screen to display
 */
UIScreenType ui_screen_draw_profile(void);

/**
 * Render the controller configuration screen with mapping and settings
 * Two-tab layout: controller mappings and controller settings
 * @return next screen to display
 */
UIScreenType ui_screen_draw_controller(void);

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
 * Render the registration dialog (PIN entry)
 * 8-digit PIN entry with visual feedback
 * @return true if registration should continue, false if canceled
 */
bool ui_screen_draw_registration(void);

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
