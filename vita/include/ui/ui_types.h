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
    UI_SCREEN_TYPE_REGISTER,
    UI_SCREEN_TYPE_REGISTER_HOST,
    UI_SCREEN_TYPE_STREAM,
    UI_SCREEN_TYPE_WAKING,           // Waking up console screen
    UI_SCREEN_TYPE_RECONNECTING,     // Reconnecting after packet loss
    UI_SCREEN_TYPE_SETTINGS,
    UI_SCREEN_TYPE_MESSAGES,
    UI_SCREEN_TYPE_PROFILE,          // Phase 2: Profile & Registration screen
    UI_SCREEN_TYPE_CONTROLLER,       // Phase 2: Controller Configuration screen
} UIScreenType;

/**
 * Types of actions that can be performed on hosts
 */
typedef enum ui_host_action_t {
    UI_HOST_ACTION_NONE = 0,
    UI_HOST_ACTION_WAKEUP,       // Only for at-rest hosts
    UI_HOST_ACTION_STREAM,       // Only for online hosts
    UI_HOST_ACTION_DELETE,       // Only for manually added hosts
    UI_HOST_ACTION_EDIT,         // Only for registered/manually added hosts
    UI_HOST_ACTION_REGISTER,     // Only for discovered hosts
} UIHostAction;

/**
 * Focus areas for D-pad navigation
 * @deprecated Use FocusZone from ui_focus.h instead (Phase 4 cleanup)
 * Kept only for potential external compatibility
 */
typedef enum ui_focus_area_t {
    FOCUS_NAV_BAR = 0,           // Wave navigation sidebar (use FOCUS_ZONE_NAV_BAR)
    FOCUS_CONSOLE_CARDS = 1      // Console cards area (use FOCUS_ZONE_MAIN_CONTENT)
} FocusArea;

/**
 * Navigation sidebar collapse states
 */
typedef enum nav_sidebar_state_t {
    NAV_STATE_EXPANDED = 0,      // Full sidebar visible (130px), waves animating
    NAV_STATE_COLLAPSING,        // Animation: 130px -> 0px -> pill reveal
    NAV_STATE_COLLAPSED,         // Pill visible at top-left
    NAV_STATE_EXPANDING          // Animation: pill -> 0px -> 130px sidebar
} NavSidebarState;

// ============================================================================
// Connection Types
// ============================================================================

/**
 * Connection overlay stages
 *
 * Note: This is also defined in ui.h for backwards compatibility.
 * During refactoring, if ui.h is included (via context.h), we skip this definition.
 */
#ifndef UI_CONNECTION_STAGE_DEFINED
#define UI_CONNECTION_STAGE_DEFINED
typedef enum ui_connection_stage_t {
    UI_CONNECTION_STAGE_NONE = 0,
    UI_CONNECTION_STAGE_WAKING,
    UI_CONNECTION_STAGE_CONNECTING,
    UI_CONNECTION_STAGE_STARTING_STREAM,
} UIConnectionStage;
#endif

// ============================================================================
// Animation State Structures
// ============================================================================

/**
 * Wave layer animation state
 */
typedef struct wave_layer_state_t {
    float phase;        // Current phase (radians, accumulates)
    float speed;        // radians per second
} WaveLayerState;

/**
 * Navigation collapse animation state
 */
typedef struct nav_collapse_state_t {
    NavSidebarState state;              // Current state
    uint64_t anim_start_us;             // Animation start timestamp
    float anim_progress;                // 0.0 to 1.0 animation progress
    float stored_wave_bottom_phase;     // For resume after collapse
    float stored_wave_top_phase;        // For resume after collapse
    float current_width;                // 0.0 to 130.0 animated sidebar width
    float pill_width;                   // 36 to 120 animated pill width
    float pill_opacity;                 // 0.0 to 1.0 pill visibility
    bool toast_shown_this_session;      // Only show toast once per app launch
    bool toast_active;                  // Currently displaying toast
    uint64_t toast_start_us;            // Toast display start time
} NavCollapseState;

/**
 * Toggle switch animation state
 */
typedef struct toggle_animation_state_t {
    int animating_index;        // Which toggle is animating (-1 = none)
    bool target_state;          // Target state (true = ON, false = OFF)
    uint64_t start_time_us;     // Animation start time
} ToggleAnimationState;

/**
 * Card focus animation state
 */
typedef struct card_focus_anim_state_t {
    int focused_card_index;             // Which card is currently focused (-1 = none)
    float current_scale;                // Current scale (0.95 to 1.0)
    uint64_t focus_start_us;            // When focus started
    int previous_focused_card_index;    // Previous focused card for scale-down
    uint64_t unfocus_start_us;          // When unfocus animation started
} CardFocusAnimState;

/**
 * Hints popup state
 */
typedef struct hints_popup_state_t {
    bool active;
    uint64_t start_time_us;
    const char* current_hint;
} HintsPopupState;

// ============================================================================
// Console Card Types
// ============================================================================

/**
 * Console card information
 */
typedef struct console_card_info_t {
    char name[32];              // "PS5 - 024"
    char ip_address[16];        // "192.168.1.100"
    int status;                 // 0=Available, 1=Unavailable, 2=Connecting
    int state;                  // 0=Unknown, 1=Ready, 2=Standby
    bool is_registered;         // Has valid credentials
    bool is_discovered;         // From network discovery
    VitaChiakiHost* host;       // Original vitaki host reference
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
// Particle System Types
// ============================================================================

/**
 * Background particle structure
 */
typedef struct particle_t {
    float x, y;
    float vx, vy;
    float scale;
    float rotation;
    float rotation_speed;
    int symbol_type;            // 0=triangle, 1=circle, 2=x, 3=square
    uint32_t color;
    bool active;
    int layer;                  // 0=background (0.7x speed), 1=foreground (1.0x speed)
    float sway_phase;           // for horizontal sway animation
    float sway_speed;           // radians per second
} Particle;

// ============================================================================
// PIN Entry Types
// ============================================================================

/**
 * PIN entry state for VitaRPS5-style registration
 */
typedef struct pin_entry_state_t {
    uint32_t pin_digits[8];     // Each digit 0-9, or 10 for empty
    int current_digit;          // Which digit cursor is on (0-7)
    bool pin_complete;          // All 8 digits entered
    uint32_t complete_pin;      // Final 8-digit number
} PinEntryState;

// ============================================================================
// Connection Overlay Types
// ============================================================================

/**
 * Connection overlay state (covers waking + fast connect flows)
 */
typedef struct connection_overlay_state_t {
    bool active;
    UIConnectionStage stage;
    uint64_t stage_updated_us;
} ConnectionOverlayState;

// ============================================================================
// Text Cache Types
// ============================================================================

/**
 * Text width cache entry
 */
typedef struct text_width_cache_entry_t {
    const char* text;
    int font_size;
    int width;
    bool valid;
} TextWidthCacheEntry;

// ============================================================================
// Controller Layout Types
// ============================================================================

/**
 * Controller view modes for immersive layout
 */
typedef enum controller_view_mode_t {
    CTRL_VIEW_FRONT = 0,  // Front view (D-pad, face buttons, sticks)
    CTRL_VIEW_BACK,       // Back view (rear touchpad quadrants)
    CTRL_VIEW_BOTH        // Both views shown (front above, back below)
} ControllerViewMode;

// ============================================================================
// Widget ID Types (for legacy compatibility)
// ============================================================================

/**
 * Identifiers of various widgets on the screen
 *
 * Note: This is also defined in ui.h for backwards compatibility.
 * During refactoring, if ui.h is included (via context.h), we skip this definition.
 */
#ifndef UI_MAIN_WIDGET_ID_DEFINED
#define UI_MAIN_WIDGET_ID_DEFINED
typedef enum ui_main_widget_id_t {
    UI_MAIN_WIDGET_ADD_HOST_BTN,
    UI_MAIN_WIDGET_REGISTER_BTN,
    UI_MAIN_WIDGET_DISCOVERY_BTN,
    UI_MAIN_WIDGET_MESSAGES_BTN,
    UI_MAIN_WIDGET_SETTINGS_BTN,
    UI_MAIN_WIDGET_HOST_TILE = 1 << 3,
    UI_MAIN_WIDGET_TEXT_INPUT = 1 << 6,
} MainWidgetId;
#endif
