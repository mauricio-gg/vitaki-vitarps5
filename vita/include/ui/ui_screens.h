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
// Settings Screen Item Indexes
// ============================================================================

typedef enum ui_settings_streaming_item_t {
  UI_SETTINGS_ITEM_QUALITY_PRESET = 0,
  UI_SETTINGS_ITEM_LATENCY_MODE = 1,
  UI_SETTINGS_ITEM_QUALITY_FALLBACK_POLICY = 2,
  UI_SETTINGS_ITEM_FPS_TARGET = 3,
  UI_SETTINGS_ITEM_FORCE_30_FPS = 4,
  UI_SETTINGS_ITEM_AUTO_DISCOVERY = 5,
  UI_SETTINGS_ITEM_SHOW_LATENCY = 6,
  UI_SETTINGS_ITEM_SHOW_NETWORK_ALERTS = 7,
  UI_SETTINGS_ITEM_SHOW_STREAM_EXIT_HINT = 8,
  UI_SETTINGS_ITEM_CLAMP_SOFT_RESTART_BITRATE = 9,
  UI_SETTINGS_ITEM_FILL_SCREEN = 10,
  UI_SETTINGS_ITEM_SHOW_NAV_LABELS = 11,
  UI_SETTINGS_ITEM_CIRCLE_BUTTON_CONFIRM = 12,
} UISettingsStreamingItem;

#define UI_SETTINGS_STREAMING_ITEM_COUNT 13

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
