/**
 * @file ui_controller_diagram.h
 * @brief PS Vita controller diagram renderer for immersive layout
 *
 * This module renders visual representations of the PS Vita controller
 * with labeled button mappings. Supports three view modes:
 * - FRONT: Shows D-pad, face buttons, shoulder buttons, analog sticks
 * - BACK: Shows rear touchpad quadrants (UL, UR, LL, LR zones)
 * - BOTH: Shows front view on top, back view at 80% scale below
 *
 * The diagram includes:
 * - Accurate Vita device outline (~600×300px)
 * - Button/touchpad zones with visual callouts
 * - Mapping labels (e.g., "L2", "R2", "Touchpad")
 * - PlayStation Blue highlights for active mappings
 * - Smooth animations (flip, pulse, color transitions)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "ui_types.h"
#include "controller.h"
#include <vita2d.h>

// Some translation units include this header before controller.h has defined the
// touch grid helpers. Provide a defensive fallback so the UI headers still
// compile even if the include order changes.
#ifndef VITAKI_FRONT_TOUCH_GRID_COUNT
#define VITAKI_FRONT_TOUCH_GRID_COUNT (VITAKI_FRONT_TOUCH_GRID_ROWS * VITAKI_FRONT_TOUCH_GRID_COLS)
#endif

// ============================================================================
// Types
// ============================================================================

/**
 * Detailed view modes for controller mapping UI
 */
typedef enum controller_detail_view_t {
    CTRL_DETAIL_SUMMARY = 0,     // Summary view with callouts (default)
    CTRL_DETAIL_FRONT_MAPPING,   // Front mapping view with interactive button selection
    CTRL_DETAIL_BACK_MAPPING,    // Back mapping view with touchpad zone selection
} ControllerDetailView;

/**
 * Button identifiers for procedural diagram rendering
 */
typedef enum vita_diagram_button_id_t {
    VITA_BTN_ID_DPAD = 0,
    VITA_BTN_ID_TRIANGLE,
    VITA_BTN_ID_CIRCLE,
    VITA_BTN_ID_CROSS,
    VITA_BTN_ID_SQUARE,
    VITA_BTN_ID_L,
    VITA_BTN_ID_R,
    VITA_BTN_ID_LSTICK,
    VITA_BTN_ID_RSTICK,
    VITA_BTN_ID_PS,
    VITA_BTN_ID_START,
    VITA_BTN_ID_SELECT,
    VITA_BTN_ID_RTOUCH_UL,
    VITA_BTN_ID_RTOUCH_UR,
    VITA_BTN_ID_RTOUCH_LL,
    VITA_BTN_ID_RTOUCH_LR,
    VITA_BTN_ID_COUNT
} VitaDiagramButtonId;

/**
 * Pre-computed button position for hit detection and highlighting
 */
typedef struct diagram_button_pos_t {
    int cx, cy;         // Center coordinates
    int radius;         // Radius for circular buttons
    int x, y, w, h;     // Rectangle bounds for non-circular buttons
    bool is_circular;   // True for circular buttons, false for rectangular
} DiagramButtonPos;

/**
 * Procedural diagram render context with all computed positions
 */
typedef struct diagram_render_ctx_t {
    int base_x, base_y;             // Top-left position of diagram
    int width, height;              // Diagram dimensions
    float scale;                    // Scale factor applied to all elements
    uint32_t outline_color;         // Primary outline color (PlayStation Blue)
    uint32_t outline_color_dim;     // Dimmed outline color
    uint32_t fill_color;            // Body fill color
    uint32_t screen_color;          // Screen area color
    uint32_t highlight_color;       // Highlight/glow color
    int line_width;                 // Outline stroke width (scaled)
    DiagramButtonPos buttons[VITA_BTN_ID_COUNT];  // Pre-computed button positions
} DiagramRenderCtx;

/**
 * Controller diagram animation state
 */
typedef struct diagram_state_t {
    ControllerViewMode mode;        // Current view mode (FRONT/BACK/BOTH)
    ControllerDetailView detail_view;  // Detail view mode (SUMMARY/FRONT_MAPPING/BACK_MAPPING)
    VitakiControllerMapId map_id;   // Current controller map ID
    int selected_button;            // Selected button for mapping (-1 = none)
    int selected_zone;              // Selected rear touchpad zone (0-3, -1 = none)
    bool front_selection[VITAKI_FRONT_TOUCH_GRID_COUNT]; // Active front-grid selection mask
    int front_selection_count;      // Number of selected front cells
    bool back_selection[VITAKI_CTRL_IN_REARTOUCH_GRID_COUNT]; // Active rear-grid selection mask
    int back_selection_count;       // Number of selected rear cells
    int callout_page;               // Current summary callout page index
    int callout_page_count;         // Total summary callout pages
    float highlight_pulse;          // Callout highlight pulse (0.0-1.0)
    float flip_animation;           // View flip animation progress (0.0-1.0)
    float color_tween;              // Preset change color tween (0.0-1.0)
    uint64_t animation_start_us;    // Animation start timestamp
    bool flip_in_progress;          // Flip animation active
    bool color_tween_active;        // Color tween active
    vita2d_texture* texture_front;
    vita2d_texture* texture_back;
} DiagramState;

// ============================================================================
// Initialization
// ============================================================================

/**
 * Initialize diagram state
 * @param state Diagram state to initialize
 */
void ui_diagram_init(DiagramState* state);

/**
 * Initialize procedural render context with all computed positions
 * @param ctx Render context to initialize
 * @param x Left edge X coordinate
 * @param y Top edge Y coordinate
 * @param w Diagram width
 * @param h Diagram height
 */
void ui_diagram_init_context(DiagramRenderCtx* ctx, int x, int y, int w, int h);

// ============================================================================
// Rendering
// ============================================================================

/**
 * Render the Vita controller diagram (delegates to procedural rendering)
 * @param state Diagram state
 * @param x Left edge X coordinate
 * @param y Top edge Y coordinate
 * @param w Diagram width (typically 500px)
 * @param h Diagram height (typically 228px for 2.2:1 aspect)
 */
void ui_diagram_render(DiagramState* state, const VitakiCtrlMapInfo* map, int x, int y, int w, int h);

/**
 * Draw front view of Vita controller using procedural rendering
 * @param ctx Initialized render context
 */
void ui_diagram_draw_front(DiagramRenderCtx* ctx);

/**
 * Draw back view of Vita controller with rear touchpad zones
 * @param ctx Initialized render context
 */
void ui_diagram_draw_back(DiagramRenderCtx* ctx);

/**
 * Draw pulsing highlight on a specific button
 * @param ctx Initialized render context
 * @param btn_id Button ID to highlight
 * @param pulse Pulse factor (0.0-1.0)
 */
void ui_diagram_draw_highlight(DiagramRenderCtx* ctx, int btn_id, float pulse);

/**
 * Draw pulsing highlight on a rear touchpad zone
 * @param ctx Initialized render context
 * @param zone_index Zone index (0=UL, 1=UR, 2=LL, 3=LR)
 * @param pulse Pulse factor (0.0-1.0)
 */
void ui_diagram_draw_zone_highlight(DiagramRenderCtx* ctx, int zone_index, float pulse);

/**
 * Draw highlight on a front touch zone
 * @param ctx Initialized render context
 * @param input Front touch input identifier
 * @param pulse Pulse factor (0.0-1.0)
 */
void ui_diagram_draw_front_zone_highlight(DiagramRenderCtx* ctx, VitakiCtrlIn input, float pulse);

/**
 * Draw highlight on a rear touch slot (quadrant or grip)
 * @param ctx Initialized render context
 * @param input Rear touch input identifier
 * @param pulse Pulse factor (0.0-1.0)
 */
void ui_diagram_draw_back_slot_highlight(DiagramRenderCtx* ctx, VitakiCtrlIn input, float pulse);

/**
 * Fetch absolute rectangle for a front touch input
 */
bool ui_diagram_front_zone_rect(DiagramRenderCtx* ctx, VitakiCtrlIn input,
                                int* out_x, int* out_y, int* out_w, int* out_h);

/**
 * Fetch absolute rectangle for a rear touch input
 */
bool ui_diagram_back_zone_rect(DiagramRenderCtx* ctx, VitakiCtrlIn input,
                               int* out_x, int* out_y, int* out_w, int* out_h);

// ============================================================================
// State Updates
// ============================================================================

/**
 * Set the controller preset for the diagram
 * Triggers color tween animation
 * @param state Diagram state
 * @param map_id New controller map ID
 */
void ui_diagram_set_preset(DiagramState* state, VitakiControllerMapId map_id);

/**
 * Update diagram animations
 * Call once per frame before rendering
 * @param state Diagram state
 */
void ui_diagram_update(DiagramState* state);
