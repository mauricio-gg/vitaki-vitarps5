/**
 * @file ui_types.h
 * @brief Type definitions for VitaRPS5 UI system
 *
 * All structs, enums, and typedefs used across UI modules are defined here.
 * This ensures consistent type usage and prevents circular dependencies.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "ui_connecting_flow.h"
#include "ui_connection_stage.h"
#include "ui_constants.h"

// Forward declaration for host reference
struct vita_chiaki_host_t;
typedef struct vita_chiaki_host_t VitaChiakiHost;

// ============================================================================
// Screen & Navigation Types
// ============================================================================

/**
 * Types of screens that can be rendered
 */
typedef enum ui_screen_type_t {
  UI_SCREEN_TYPE_MAIN = 0,
  UI_SCREEN_TYPE_REGISTER_HOST,
  UI_SCREEN_TYPE_NONE,          // Sentinel: no screen drawn yet (never a real screen)
  UI_SCREEN_TYPE_WAKING,        // Waking up console screen
  UI_SCREEN_TYPE_RECONNECTING,  // Reconnecting after packet loss
  UI_SCREEN_TYPE_SETTINGS,
  UI_SCREEN_TYPE_PROFILE,     // Phase 2: Profile & Registration screen
  UI_SCREEN_TYPE_CONTROLLER,  // Phase 2: Controller Configuration screen
} UIScreenType;

/**
 * Types of actions that can be performed on hosts
 */
typedef enum ui_host_action_t {
  UI_HOST_ACTION_NONE = 0,
  UI_HOST_ACTION_WAKEUP,    // Only for at-rest hosts
  UI_HOST_ACTION_STREAM,    // Only for online hosts
  UI_HOST_ACTION_DELETE,    // Only for manually added hosts
  UI_HOST_ACTION_EDIT,      // Only for registered/manually added hosts
  UI_HOST_ACTION_REGISTER,  // Only for discovered hosts
} UIHostAction;

// ============================================================================
// Console Card Types
// ============================================================================

/** Power state a console card reports (ConsoleCardInfo.state). */
typedef enum console_card_state_t {
  CONSOLE_CARD_STATE_UNKNOWN = 0,
  CONSOLE_CARD_STATE_READY,
  CONSOLE_CARD_STATE_STANDBY,
} ConsoleCardState;

/**
 * Console card information
 */
typedef struct console_card_info_t {
  char name[32];           // "PS5 - 024"
  char ip_address[16];     // "192.168.1.100"
  int status;              // 0=Available, 1=Unavailable, 2=Connecting
  ConsoleCardState state;  // Power state: unknown, ready or standby
  bool is_registered;      // Has valid credentials
  bool is_discovered;      // From network discovery
  bool has_internet;       // Host is reachable over the internet via PSN remote play (mirrors
                           // VitaChiakiHost.psn_remote_available). True for both a LAN host with a
                           // merged PSN_REMOTE entry AND a standalone PSN_REMOTE card with no LAN
                           // route -- callers that need "has both a LAN and an internet route" must
                           // additionally check host->source != VITA_HOST_SOURCE_PSN_REMOTE.
  VitaChiakiHost *host;    // Original vitaki host reference
} ConsoleCardInfo;

/**
 * Console card cache to prevent flickering during discovery
 */
typedef struct console_card_cache_t {
  ConsoleCardInfo cards[64];  // MAX_CONTEXT_HOSTS — keep in sync with host.h
  int num_cards;
  uint64_t last_update_time;  // Microseconds since epoch
} ConsoleCardCache;

// ============================================================================
// Connection Overlay Types
// ============================================================================

/**
 * Connection overlay state (covers waking + fast connect flows)
 */
typedef struct connection_overlay_state_t {
  bool active;
  UIConnectionStage stage;
  UiConnectingFlow flow;  // decided once when the connect begins
  uint32_t serial;        // connects begun so far
  uint64_t stage_updated_us;
} ConnectionOverlayState;

// ============================================================================
// Controller Layout Types
// ============================================================================

/**
 * Controller view modes for immersive layout
 */
typedef enum controller_view_mode_t {
  CTRL_VIEW_FRONT = 0,  // Front view (D-pad, face buttons, sticks)
  CTRL_VIEW_BACK        // Back view (rear touchpad quadrants)
} ControllerViewMode;
