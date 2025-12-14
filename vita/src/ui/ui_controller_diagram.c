/**
 * @file ui_controller_diagram.c
 * @brief PS Vita controller diagram - procedural rendering
 *
 * Renders high-fidelity Vita controller diagrams using vita2d primitives.
 * No PNG assets required - all graphics are drawn procedurally using ratio-based
 * coordinates from ui_constants.h for pixel-perfect scaling.
 *
 * Three view modes:
 * - Summary: Large diagram with inline callouts showing mappings (△ → □)
 * - Front Mapping: Interactive front view for remapping buttons
 * - Back Mapping: Interactive rear touchpad view for zone mapping
 */

#include "ui/ui_controller_diagram.h"
#include "ui/ui_constants.h"
#include "ui/ui_graphics.h"
#include "ui/ui_internal.h"
#include "controller.h"
#include "context.h"

#include <vita2d.h>
#include <psp2/rtc.h>
#include <psp2/kernel/clib.h>
#include <math.h>

// ============================================================================
// Internal Constants
// ============================================================================

// Animation durations
#define FLIP_DURATION_MS 220
#define COLOR_TWEEN_DURATION_MS 300
#define PULSE_PERIOD_MS 1000

// Callout rendering (for Summary view)
#define CALLOUT_LINE_LENGTH 40
#define CALLOUT_PILL_HEIGHT 26
#define CALLOUT_PILL_PADDING 10
#define CALLOUT_ARROW_LENGTH 12

// ============================================================================
// Helper Macros
// ============================================================================

#define RATIO_X(ctx, r) ((ctx)->base_x + (int)((ctx)->width * (r)))
#define RATIO_Y(ctx, r) ((ctx)->base_y + (int)((ctx)->height * (r)))
#define RATIO_W(ctx, r) ((int)((ctx)->width * (r)))
#define RATIO_H(ctx, r) ((int)((ctx)->height * (r)))
#define RATIO_SIZE(ctx, r) RATIO_W(ctx, r)  // Backward compatibility, defaults to width

// ============================================================================
// External References
// ============================================================================

extern vita2d_font* font;

// ============================================================================
// Internal Helpers
// ============================================================================

/**
 * Get current time in microseconds
 */
static uint64_t get_time_us(void) {
    SceRtcTick tick;
    sceRtcGetCurrentTick(&tick);
    return tick.tick;
}

/**
 * Linear interpolation
 */
static inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

/**
 * Ease-in-out cubic interpolation
 */
static inline float ease_in_out_cubic(float t) {
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

/**
 * Draw callout line with arrow pointing to button (for Summary view)
 */
static void draw_callout_arrow(int x1, int y1, int x2, int y2, uint32_t color) {
    // Main line
    vita2d_draw_line(x1, y1, x2, y2, color);

    // Arrow head (simple V shape at destination)
    float angle = atan2f((float)(y2 - y1), (float)(x2 - x1));
    float arrow_size = 6.0f;

    float left_x = x2 - arrow_size * cosf(angle - (float)M_PI / 6.0f);
    float left_y = y2 - arrow_size * sinf(angle - (float)M_PI / 6.0f);
    float right_x = x2 - arrow_size * cosf(angle + (float)M_PI / 6.0f);
    float right_y = y2 - arrow_size * sinf(angle + (float)M_PI / 6.0f);

    vita2d_draw_line(x2, y2, (int)left_x, (int)left_y, color);
    vita2d_draw_line(x2, y2, (int)right_x, (int)right_y, color);
}

/**
 * Draw callout pill with text (e.g., "△ → □")
 */
static void draw_callout_pill(int x, int y, const char* text, uint32_t bg_color, uint32_t text_color) {
    int text_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, text);
    int pill_w = text_w + CALLOUT_PILL_PADDING * 2;

    ui_draw_rounded_rect(x, y, pill_w, CALLOUT_PILL_HEIGHT, CALLOUT_PILL_HEIGHT / 2, bg_color);
    vita2d_font_draw_text(font, x + CALLOUT_PILL_PADDING, y + CALLOUT_PILL_HEIGHT - 7,
                          text_color, FONT_SIZE_SMALL, text);
}

/**
 * Get button name for mapping display
 */
static const char* get_button_name(VitakiCtrlOut button) {
    switch (button) {
        case VITAKI_CTRL_OUT_TRIANGLE: return "△";
        case VITAKI_CTRL_OUT_CIRCLE: return "○";
        case VITAKI_CTRL_OUT_CROSS: return "✕";
        case VITAKI_CTRL_OUT_SQUARE: return "□";
        case VITAKI_CTRL_OUT_L1: return "L1";
        case VITAKI_CTRL_OUT_R1: return "R1";
        case VITAKI_CTRL_OUT_L2: return "L2";
        case VITAKI_CTRL_OUT_R2: return "R2";
        case VITAKI_CTRL_OUT_L3: return "L3";
        case VITAKI_CTRL_OUT_R3: return "R3";
        case VITAKI_CTRL_OUT_TOUCHPAD: return "Touchpad";
        default: return "None";
    }
}

// ============================================================================
// Procedural Drawing Functions - Front View
// ============================================================================

/**
 * Draw a stadium/pill shape fill (rectangle with semicircular ends)
 *
 * Stadium shape: (===) where the left and right ends are perfect semicircles
 * The semicircle radius = height / 2, creating the authentic PS Vita body shape
 *
 * @param x Left edge X coordinate
 * @param y Top edge Y coordinate
 * @param w Total width including semicircular ends
 * @param h Height (semicircle radius = h/2)
 * @param color Fill color
 */
static void draw_stadium_fill(int x, int y, int w, int h, uint32_t color) {
    // Bounds validation: reject degenerate shapes
    if (w <= 0 || h <= 0) return;

    int radius = h / 2;

    // Clamp radius to valid range
    if (radius < 1) radius = 1;
    if (radius > w / 2) radius = w / 2;

    // Center rectangle (between semicircles)
    int rect_x = x + radius;
    int rect_w = w - 2 * radius;
    if (rect_w > 0) {
        vita2d_draw_rectangle(rect_x, y, rect_w, h, color);
    }

    // Left and right semicircles (filled)
    int cy = y + radius;
    vita2d_draw_fill_circle(x + radius, cy, radius, color);
    vita2d_draw_fill_circle(x + w - radius, cy, radius, color);
}

/**
 * Draw a stadium/pill shape outline with semicircular arc ends
 *
 * Uses 24 line segments per semicircle for smooth curves.
 * Matches the authentic PS Vita controller outline.
 *
 * @param x Left edge X coordinate
 * @param y Top edge Y coordinate
 * @param w Total width including semicircular ends
 * @param h Height (semicircle radius = h/2)
 * @param color Outline color
 */
static void draw_stadium_outline(int x, int y, int w, int h, uint32_t color) {
    // Bounds validation: reject degenerate shapes
    if (w <= 0 || h <= 0) return;

    int radius = h / 2;

    // Clamp radius to valid range
    if (radius < 1) radius = 1;
    if (radius > w / 2) radius = w / 2;

    int left_cx = x + radius;
    int right_cx = x + w - radius;
    int cy = y + radius;

    // Top horizontal line (between semicircles)
    vita2d_draw_line(left_cx, y, right_cx, y, color);

    // Bottom horizontal line (between semicircles)
    vita2d_draw_line(left_cx, y + h - 1, right_cx, y + h - 1, color);

    // Left semicircle arc (8 segments, from top to bottom)
    // Goes from PI/2 (top) to 3*PI/2 (bottom) on the left side
    // Reduced from 24 for PS Vita GPU performance (prevents crash from excessive draw calls)
    int arc_segments = 8;
    float start = (float)M_PI / 2.0f;
    float step = (float)M_PI / (float)arc_segments;

    for (int i = 0; i < arc_segments; i++) {
        float a1 = start + i * step;
        float a2 = start + (i + 1) * step;
        int x1 = left_cx - (int)(cosf(a1) * radius);
        int y1 = cy + (int)(sinf(a1) * radius);
        int x2 = left_cx - (int)(cosf(a2) * radius);
        int y2 = cy + (int)(sinf(a2) * radius);
        vita2d_draw_line(x1, y1, x2, y2, color);
    }

    // Right semicircle arc (8 segments, from top to bottom)
    // Goes from -PI/2 (top) to PI/2 (bottom) on the right side
    start = -(float)M_PI / 2.0f;
    for (int i = 0; i < arc_segments; i++) {
        float a1 = start + i * step;
        float a2 = start + (i + 1) * step;
        int x1 = right_cx + (int)(cosf(a1) * radius);
        int y1 = cy + (int)(sinf(a1) * radius);
        int x2 = right_cx + (int)(cosf(a2) * radius);
        int y2 = cy + (int)(sinf(a2) * radius);
        vita2d_draw_line(x1, y1, x2, y2, color);
    }
}

/**
 * Draw main body fill using stadium/pill shape (PS Vita authentic shape)
 */
static void draw_front_body(DiagramRenderCtx* ctx) {
    int x = RATIO_X(ctx, VITA_BODY_X_RATIO);
    int y = RATIO_Y(ctx, VITA_BODY_Y_RATIO);
    int w = RATIO_W(ctx, VITA_BODY_W_RATIO);
    int h = RATIO_H(ctx, VITA_BODY_H_RATIO);

    // Fill using stadium shape (radius = h/2 for authentic PS Vita look)
    draw_stadium_fill(x, y, w, h, ctx->fill_color);
}

/**
 * Draw screen area (dark rectangle in center)
 */
static void draw_front_screen(DiagramRenderCtx* ctx) {
    int x = RATIO_X(ctx, VITA_SCREEN_X_RATIO);
    int y = RATIO_Y(ctx, VITA_SCREEN_Y_RATIO);
    int w = RATIO_W(ctx, VITA_SCREEN_W_RATIO);
    int h = RATIO_H(ctx, VITA_SCREEN_H_RATIO);

    vita2d_draw_rectangle(x, y, w, h, ctx->screen_color);
}

/**
 * Draw D-pad (cross shape using two rectangles)
 */
static void draw_front_dpad(DiagramRenderCtx* ctx) {
    int cx = RATIO_X(ctx, VITA_DPAD_CX_RATIO);
    int cy = RATIO_Y(ctx, VITA_DPAD_CY_RATIO);
    int arm_len = RATIO_SIZE(ctx, VITA_DPAD_ARM_LENGTH_RATIO);
    int arm_width = RATIO_SIZE(ctx, VITA_DPAD_ARM_WIDTH_RATIO);

    // Horizontal arm
    vita2d_draw_rectangle(cx - arm_len, cy - arm_width / 2, arm_len * 2, arm_width, ctx->outline_color_dim);

    // Vertical arm
    vita2d_draw_rectangle(cx - arm_width / 2, cy - arm_len, arm_width, arm_len * 2, ctx->outline_color_dim);
}

/**
 * Draw face buttons (4 circular buttons in diamond pattern)
 */
static void draw_front_face_buttons(DiagramRenderCtx* ctx) {
    int radius = RATIO_SIZE(ctx, VITA_FACE_BTN_RADIUS_RATIO);

    // Triangle (top)
    int tri_x = RATIO_X(ctx, VITA_BTN_TRIANGLE_CX_RATIO);
    int tri_y = RATIO_Y(ctx, VITA_BTN_TRIANGLE_CY_RATIO);
    ui_draw_circle_outline(tri_x, tri_y, radius, ctx->outline_color);

    // Circle (right)
    int cir_x = RATIO_X(ctx, VITA_BTN_CIRCLE_CX_RATIO);
    int cir_y = RATIO_Y(ctx, VITA_BTN_CIRCLE_CY_RATIO);
    ui_draw_circle_outline(cir_x, cir_y, radius, ctx->outline_color);

    // Cross (bottom)
    int cross_x = RATIO_X(ctx, VITA_BTN_CROSS_CX_RATIO);
    int cross_y = RATIO_Y(ctx, VITA_BTN_CROSS_CY_RATIO);
    ui_draw_circle_outline(cross_x, cross_y, radius, ctx->outline_color);

    // Square (left)
    int sq_x = RATIO_X(ctx, VITA_BTN_SQUARE_CX_RATIO);
    int sq_y = RATIO_Y(ctx, VITA_BTN_SQUARE_CY_RATIO);
    ui_draw_circle_outline(sq_x, sq_y, radius, ctx->outline_color);
}

/**
 * Draw analog sticks (concentric circles with center dot)
 */
static void draw_front_sticks(DiagramRenderCtx* ctx) {
    int outer_r = RATIO_SIZE(ctx, VITA_STICK_OUTER_R_RATIO);
    int inner_r = RATIO_SIZE(ctx, VITA_STICK_INNER_R_RATIO);
    int dot_r = RATIO_SIZE(ctx, VITA_STICK_DOT_R_RATIO);

    // Left stick
    int lstick_x = RATIO_X(ctx, VITA_LSTICK_CX_RATIO);
    int lstick_y = RATIO_Y(ctx, VITA_LSTICK_CY_RATIO);
    ui_draw_circle_outline(lstick_x, lstick_y, outer_r, ctx->outline_color);
    ui_draw_circle_outline(lstick_x, lstick_y, inner_r, ctx->outline_color_dim);
    vita2d_draw_fill_circle(lstick_x, lstick_y, dot_r, ctx->outline_color);

    // Right stick
    int rstick_x = RATIO_X(ctx, VITA_RSTICK_CX_RATIO);
    int rstick_y = RATIO_Y(ctx, VITA_RSTICK_CY_RATIO);
    ui_draw_circle_outline(rstick_x, rstick_y, outer_r, ctx->outline_color);
    ui_draw_circle_outline(rstick_x, rstick_y, inner_r, ctx->outline_color_dim);
    vita2d_draw_fill_circle(rstick_x, rstick_y, dot_r, ctx->outline_color);
}

/**
 * Draw shoulder buttons (L and R as small rectangle outlines)
 */
static void draw_front_shoulders(DiagramRenderCtx* ctx) {
    int w = RATIO_W(ctx, VITA_L_BTN_W_RATIO);
    int h = RATIO_H(ctx, VITA_L_BTN_H_RATIO);

    // L button - draw as outline only (4 lines forming a rectangle)
    int l_x = RATIO_X(ctx, VITA_L_BTN_X_RATIO);
    int l_y = RATIO_Y(ctx, VITA_L_BTN_Y_RATIO);
    vita2d_draw_line(l_x, l_y, l_x + w, l_y, ctx->outline_color_dim);              // Top
    vita2d_draw_line(l_x, l_y + h, l_x + w, l_y + h, ctx->outline_color_dim);      // Bottom
    vita2d_draw_line(l_x, l_y, l_x, l_y + h, ctx->outline_color_dim);              // Left
    vita2d_draw_line(l_x + w, l_y, l_x + w, l_y + h, ctx->outline_color_dim);      // Right

    // R button - draw as outline only (4 lines forming a rectangle)
    int r_x = RATIO_X(ctx, VITA_R_BTN_X_RATIO);
    int r_y = RATIO_Y(ctx, VITA_R_BTN_Y_RATIO);
    vita2d_draw_line(r_x, r_y, r_x + w, r_y, ctx->outline_color_dim);              // Top
    vita2d_draw_line(r_x, r_y + h, r_x + w, r_y + h, ctx->outline_color_dim);      // Bottom
    vita2d_draw_line(r_x, r_y, r_x, r_y + h, ctx->outline_color_dim);              // Left
    vita2d_draw_line(r_x + w, r_y, r_x + w, r_y + h, ctx->outline_color_dim);      // Right
}

/**
 * Draw system buttons (PS, Start, Select as small circles)
 */
static void draw_front_system_buttons(DiagramRenderCtx* ctx) {
    int ps_r = RATIO_SIZE(ctx, VITA_PS_BTN_R_RATIO);
    int sys_r = RATIO_SIZE(ctx, VITA_SYS_BTN_R_RATIO);

    // PS button (larger, center bottom)
    int ps_x = RATIO_X(ctx, VITA_PS_BTN_CX_RATIO);
    int ps_y = RATIO_Y(ctx, VITA_PS_BTN_CY_RATIO);
    ui_draw_circle_outline(ps_x, ps_y, ps_r, ctx->outline_color);

    // Start button (right of center)
    int start_x = RATIO_X(ctx, VITA_START_CX_RATIO);
    int start_y = RATIO_Y(ctx, VITA_START_CY_RATIO);
    ui_draw_circle_outline(start_x, start_y, sys_r, ctx->outline_color_dim);

    // Select button (left of center)
    int select_x = RATIO_X(ctx, VITA_SELECT_CX_RATIO);
    int select_y = RATIO_Y(ctx, VITA_SELECT_CY_RATIO);
    ui_draw_circle_outline(select_x, select_y, sys_r, ctx->outline_color_dim);
}

/**
 * Draw body outline using stadium shape with semicircular arcs
 */
static void draw_front_body_outline(DiagramRenderCtx* ctx) {
    int x = RATIO_X(ctx, VITA_BODY_X_RATIO);
    int y = RATIO_Y(ctx, VITA_BODY_Y_RATIO);
    int w = RATIO_W(ctx, VITA_BODY_W_RATIO);
    int h = RATIO_H(ctx, VITA_BODY_H_RATIO);

    // Draw stadium outline with smooth semicircular arcs
    draw_stadium_outline(x, y, w, h, ctx->outline_color);
}

// ============================================================================
// Procedural Drawing Functions - Back View
// ============================================================================

/**
 * Draw rear touchpad with zone dividers
 */
static void draw_back_touchpad(DiagramRenderCtx* ctx) {
    int pad_x = RATIO_X(ctx, VITA_RTOUCH_X_RATIO);
    int pad_y = RATIO_Y(ctx, VITA_RTOUCH_Y_RATIO);
    int pad_w = RATIO_W(ctx, VITA_RTOUCH_W_RATIO);
    int pad_h = RATIO_H(ctx, VITA_RTOUCH_H_RATIO);

    // Draw touchpad background
    vita2d_draw_rectangle(pad_x, pad_y, pad_w, pad_h, ctx->screen_color);

    // Draw zone dividers (cross pattern)
    int mid_x = pad_x + pad_w / 2;
    int mid_y = pad_y + pad_h / 2;

    // Vertical divider
    vita2d_draw_line(mid_x, pad_y, mid_x, pad_y + pad_h, ctx->outline_color_dim);

    // Horizontal divider
    vita2d_draw_line(pad_x, mid_y, pad_x + pad_w, mid_y, ctx->outline_color_dim);

    // Draw touchpad outline
    vita2d_draw_line(pad_x, pad_y, pad_x + pad_w, pad_y, ctx->outline_color);
    vita2d_draw_line(pad_x, pad_y + pad_h, pad_x + pad_w, pad_y + pad_h, ctx->outline_color);
    vita2d_draw_line(pad_x, pad_y, pad_x, pad_y + pad_h, ctx->outline_color);
    vita2d_draw_line(pad_x + pad_w, pad_y, pad_x + pad_w, pad_y + pad_h, ctx->outline_color);
}

/**
 * Draw camera decoration (small circle in upper right)
 */
static void draw_back_camera(DiagramRenderCtx* ctx) {
    int cam_x = RATIO_X(ctx, VITA_CAMERA_CX_RATIO);
    int cam_y = RATIO_Y(ctx, VITA_CAMERA_CY_RATIO);
    int cam_r = RATIO_SIZE(ctx, VITA_CAMERA_R_RATIO);

    ui_draw_circle_outline(cam_x, cam_y, cam_r, ctx->outline_color_dim);
}

// ============================================================================
// Highlight Functions
// ============================================================================

/**
 * Draw pulsing highlight on a specific button
 */
void ui_diagram_draw_highlight(DiagramRenderCtx* ctx, int btn_id, float pulse) {
    if (btn_id < 0 || btn_id >= VITA_BTN_ID_COUNT) {
        return;
    }

    DiagramButtonPos* btn = &ctx->buttons[btn_id];
    uint8_t alpha = (uint8_t)(200 + 55 * pulse);
    uint32_t glow_color = (UI_COLOR_PRIMARY_BLUE & 0xFFFFFF00) | alpha;

    if (btn->is_circular) {
        // Draw glow circles
        ui_draw_circle_outline(btn->cx, btn->cy, btn->radius + 4, glow_color);
        ui_draw_circle_outline(btn->cx, btn->cy, btn->radius + 6, (UI_COLOR_PRIMARY_BLUE & 0xFFFFFF00) | 100);
    } else {
        // Draw glow rectangle
        ui_draw_rectangle_outline(btn->x - 2, btn->y - 2, btn->w + 4, btn->h + 4, glow_color);
        ui_draw_rectangle_outline(btn->x - 4, btn->y - 4, btn->w + 8, btn->h + 8, (UI_COLOR_PRIMARY_BLUE & 0xFFFFFF00) | 100);
    }
}

/**
 * Draw pulsing highlight on a rear touchpad zone
 */
void ui_diagram_draw_zone_highlight(DiagramRenderCtx* ctx, int zone_index, float pulse) {
    if (zone_index < 0 || zone_index > 3) {
        return;
    }

    int pad_x = RATIO_X(ctx, VITA_RTOUCH_X_RATIO);
    int pad_y = RATIO_Y(ctx, VITA_RTOUCH_Y_RATIO);
    int pad_w = RATIO_W(ctx, VITA_RTOUCH_W_RATIO);
    int pad_h = RATIO_H(ctx, VITA_RTOUCH_H_RATIO);

    int zone_w = pad_w / 2;
    int zone_h = pad_h / 2;
    int zone_x = pad_x + (zone_index % 2) * zone_w;
    int zone_y = pad_y + (zone_index / 2) * zone_h;

    uint8_t alpha = (uint8_t)(150 + 105 * pulse);
    uint32_t glow_color = (UI_COLOR_PRIMARY_BLUE & 0xFFFFFF00) | alpha;

    // Draw highlight rectangle
    ui_draw_rectangle_outline(zone_x + 2, zone_y + 2, zone_w - 4, zone_h - 4, glow_color);
    ui_draw_rectangle_outline(zone_x + 4, zone_y + 4, zone_w - 8, zone_h - 8, (UI_COLOR_PRIMARY_BLUE & 0xFFFFFF00) | 100);
}

// ============================================================================
// Summary View Callouts
// ============================================================================

/**
 * Draw mapping callouts for Summary view
 * Shows inline labels like "△ → □" with arrows pointing to controls
 */
static void draw_summary_callouts(DiagramState* state, DiagramRenderCtx* ctx) {
    // Get current map ID to determine what to show
    VitakiControllerMapId map_id = state->map_id;

    // Pulse color for callout lines
    float pulse_alpha = 0.8f + 0.2f * sinf(state->highlight_pulse * 2.0f * (float)M_PI);
    uint32_t line_color = UI_COLOR_PRIMARY_BLUE;
    uint32_t pill_bg = RGBA8(45, 50, 55, 230);
    uint32_t pill_text = UI_COLOR_TEXT_PRIMARY;

    // Example callouts based on common mappings
    // L2/R2 callouts (rear touch upper corners by default)
    if (map_id == VITAKI_CONTROLLER_MAP_0 || map_id == VITAKI_CONTROLLER_MAP_100) {
        int callout_x = ctx->base_x - 60;
        int callout_y = ctx->base_y + 40;
        draw_callout_arrow(callout_x + 80, callout_y + 12, ctx->base_x + 20, ctx->base_y + 30, line_color);
        draw_callout_pill(callout_x, callout_y, "L2 → Rear UL", pill_bg, pill_text);

        callout_x = ctx->base_x + ctx->width - 20;
        draw_callout_arrow(callout_x, callout_y + 12, ctx->base_x + ctx->width - 60, ctx->base_y + 30, line_color);
        draw_callout_pill(callout_x, callout_y, "R2 → Rear UR", pill_bg, pill_text);
    }

    // Touchpad callout (center)
    if (map_id != VITAKI_CONTROLLER_MAP_4 && map_id != VITAKI_CONTROLLER_MAP_5) {
        int callout_x = ctx->base_x + ctx->width / 2 - 40;
        int callout_y = ctx->base_y + ctx->height / 2;
        vita2d_font_draw_text(font, callout_x, callout_y, CTRL_OUTLINE_COLOR_DIM,
                              FONT_SIZE_SMALL, "Touchpad → Front");
    }
}

// ============================================================================
// Context Initialization
// ============================================================================

/**
 * Initialize procedural render context with all computed positions
 */
void ui_diagram_init_context(DiagramRenderCtx* ctx, int x, int y, int w, int h) {
    // Set base position and dimensions
    ctx->base_x = x;
    ctx->base_y = y;
    ctx->width = w;
    ctx->height = h;
    ctx->scale = 1.0f;

    // Set colors
    ctx->outline_color = UI_COLOR_PRIMARY_BLUE;          // PlayStation Blue
    ctx->outline_color_dim = RGBA8(0, 100, 180, 255);    // Dimmer blue
    ctx->fill_color = RGBA8(35, 38, 45, 255);            // Dark gray body
    ctx->screen_color = RGBA8(20, 22, 28, 255);          // Darker screen
    ctx->highlight_color = UI_COLOR_PRIMARY_BLUE;        // PlayStation Blue

    // Compute line width based on scale
    ctx->line_width = RATIO_SIZE(ctx, VITA_OUTLINE_WIDTH_RATIO);
    if (ctx->line_width < 1) ctx->line_width = 1;

    // Pre-compute all button positions for hit detection and highlighting
    // This is done once per render for efficiency

    // D-pad (rectangular)
    ctx->buttons[VITA_BTN_ID_DPAD].cx = RATIO_X(ctx, VITA_DPAD_CX_RATIO);
    ctx->buttons[VITA_BTN_ID_DPAD].cy = RATIO_Y(ctx, VITA_DPAD_CY_RATIO);
    ctx->buttons[VITA_BTN_ID_DPAD].radius = RATIO_SIZE(ctx, VITA_DPAD_ARM_LENGTH_RATIO);
    ctx->buttons[VITA_BTN_ID_DPAD].is_circular = false;
    ctx->buttons[VITA_BTN_ID_DPAD].x = ctx->buttons[VITA_BTN_ID_DPAD].cx - ctx->buttons[VITA_BTN_ID_DPAD].radius;
    ctx->buttons[VITA_BTN_ID_DPAD].y = ctx->buttons[VITA_BTN_ID_DPAD].cy - ctx->buttons[VITA_BTN_ID_DPAD].radius;
    ctx->buttons[VITA_BTN_ID_DPAD].w = ctx->buttons[VITA_BTN_ID_DPAD].radius * 2;
    ctx->buttons[VITA_BTN_ID_DPAD].h = ctx->buttons[VITA_BTN_ID_DPAD].radius * 2;

    // Face buttons (circular)
    int face_r = RATIO_SIZE(ctx, VITA_FACE_BTN_RADIUS_RATIO);

    ctx->buttons[VITA_BTN_ID_TRIANGLE].cx = RATIO_X(ctx, VITA_BTN_TRIANGLE_CX_RATIO);
    ctx->buttons[VITA_BTN_ID_TRIANGLE].cy = RATIO_Y(ctx, VITA_BTN_TRIANGLE_CY_RATIO);
    ctx->buttons[VITA_BTN_ID_TRIANGLE].radius = face_r;
    ctx->buttons[VITA_BTN_ID_TRIANGLE].is_circular = true;

    ctx->buttons[VITA_BTN_ID_CIRCLE].cx = RATIO_X(ctx, VITA_BTN_CIRCLE_CX_RATIO);
    ctx->buttons[VITA_BTN_ID_CIRCLE].cy = RATIO_Y(ctx, VITA_BTN_CIRCLE_CY_RATIO);
    ctx->buttons[VITA_BTN_ID_CIRCLE].radius = face_r;
    ctx->buttons[VITA_BTN_ID_CIRCLE].is_circular = true;

    ctx->buttons[VITA_BTN_ID_CROSS].cx = RATIO_X(ctx, VITA_BTN_CROSS_CX_RATIO);
    ctx->buttons[VITA_BTN_ID_CROSS].cy = RATIO_Y(ctx, VITA_BTN_CROSS_CY_RATIO);
    ctx->buttons[VITA_BTN_ID_CROSS].radius = face_r;
    ctx->buttons[VITA_BTN_ID_CROSS].is_circular = true;

    ctx->buttons[VITA_BTN_ID_SQUARE].cx = RATIO_X(ctx, VITA_BTN_SQUARE_CX_RATIO);
    ctx->buttons[VITA_BTN_ID_SQUARE].cy = RATIO_Y(ctx, VITA_BTN_SQUARE_CY_RATIO);
    ctx->buttons[VITA_BTN_ID_SQUARE].radius = face_r;
    ctx->buttons[VITA_BTN_ID_SQUARE].is_circular = true;

    // Shoulder buttons (rectangular)
    int shoulder_w = RATIO_W(ctx, VITA_L_BTN_W_RATIO);
    int shoulder_h = RATIO_H(ctx, VITA_L_BTN_H_RATIO);

    ctx->buttons[VITA_BTN_ID_L].x = RATIO_X(ctx, VITA_L_BTN_X_RATIO);
    ctx->buttons[VITA_BTN_ID_L].y = RATIO_Y(ctx, VITA_L_BTN_Y_RATIO);
    ctx->buttons[VITA_BTN_ID_L].w = shoulder_w;
    ctx->buttons[VITA_BTN_ID_L].h = shoulder_h;
    ctx->buttons[VITA_BTN_ID_L].cx = ctx->buttons[VITA_BTN_ID_L].x + shoulder_w / 2;
    ctx->buttons[VITA_BTN_ID_L].cy = ctx->buttons[VITA_BTN_ID_L].y + shoulder_h / 2;
    ctx->buttons[VITA_BTN_ID_L].is_circular = false;

    ctx->buttons[VITA_BTN_ID_R].x = RATIO_X(ctx, VITA_R_BTN_X_RATIO);
    ctx->buttons[VITA_BTN_ID_R].y = RATIO_Y(ctx, VITA_R_BTN_Y_RATIO);
    ctx->buttons[VITA_BTN_ID_R].w = shoulder_w;
    ctx->buttons[VITA_BTN_ID_R].h = shoulder_h;
    ctx->buttons[VITA_BTN_ID_R].cx = ctx->buttons[VITA_BTN_ID_R].x + shoulder_w / 2;
    ctx->buttons[VITA_BTN_ID_R].cy = ctx->buttons[VITA_BTN_ID_R].y + shoulder_h / 2;
    ctx->buttons[VITA_BTN_ID_R].is_circular = false;

    // Analog sticks (circular)
    int stick_r = RATIO_SIZE(ctx, VITA_STICK_OUTER_R_RATIO);

    ctx->buttons[VITA_BTN_ID_LSTICK].cx = RATIO_X(ctx, VITA_LSTICK_CX_RATIO);
    ctx->buttons[VITA_BTN_ID_LSTICK].cy = RATIO_Y(ctx, VITA_LSTICK_CY_RATIO);
    ctx->buttons[VITA_BTN_ID_LSTICK].radius = stick_r;
    ctx->buttons[VITA_BTN_ID_LSTICK].is_circular = true;

    ctx->buttons[VITA_BTN_ID_RSTICK].cx = RATIO_X(ctx, VITA_RSTICK_CX_RATIO);
    ctx->buttons[VITA_BTN_ID_RSTICK].cy = RATIO_Y(ctx, VITA_RSTICK_CY_RATIO);
    ctx->buttons[VITA_BTN_ID_RSTICK].radius = stick_r;
    ctx->buttons[VITA_BTN_ID_RSTICK].is_circular = true;

    // System buttons (circular)
    int ps_r = RATIO_SIZE(ctx, VITA_PS_BTN_R_RATIO);
    int sys_r = RATIO_SIZE(ctx, VITA_SYS_BTN_R_RATIO);

    ctx->buttons[VITA_BTN_ID_PS].cx = RATIO_X(ctx, VITA_PS_BTN_CX_RATIO);
    ctx->buttons[VITA_BTN_ID_PS].cy = RATIO_Y(ctx, VITA_PS_BTN_CY_RATIO);
    ctx->buttons[VITA_BTN_ID_PS].radius = ps_r;
    ctx->buttons[VITA_BTN_ID_PS].is_circular = true;

    ctx->buttons[VITA_BTN_ID_START].cx = RATIO_X(ctx, VITA_START_CX_RATIO);
    ctx->buttons[VITA_BTN_ID_START].cy = RATIO_Y(ctx, VITA_START_CY_RATIO);
    ctx->buttons[VITA_BTN_ID_START].radius = sys_r;
    ctx->buttons[VITA_BTN_ID_START].is_circular = true;

    ctx->buttons[VITA_BTN_ID_SELECT].cx = RATIO_X(ctx, VITA_SELECT_CX_RATIO);
    ctx->buttons[VITA_BTN_ID_SELECT].cy = RATIO_Y(ctx, VITA_SELECT_CY_RATIO);
    ctx->buttons[VITA_BTN_ID_SELECT].radius = sys_r;
    ctx->buttons[VITA_BTN_ID_SELECT].is_circular = true;

    // Rear touchpad zones (rectangular) - computed from touchpad dimensions
    int pad_x = RATIO_X(ctx, VITA_RTOUCH_X_RATIO);
    int pad_y = RATIO_Y(ctx, VITA_RTOUCH_Y_RATIO);
    int pad_w = RATIO_W(ctx, VITA_RTOUCH_W_RATIO);
    int pad_h = RATIO_H(ctx, VITA_RTOUCH_H_RATIO);
    int zone_w = pad_w / 2;
    int zone_h = pad_h / 2;

    // Upper Left
    ctx->buttons[VITA_BTN_ID_RTOUCH_UL].x = pad_x;
    ctx->buttons[VITA_BTN_ID_RTOUCH_UL].y = pad_y;
    ctx->buttons[VITA_BTN_ID_RTOUCH_UL].w = zone_w;
    ctx->buttons[VITA_BTN_ID_RTOUCH_UL].h = zone_h;
    ctx->buttons[VITA_BTN_ID_RTOUCH_UL].cx = RATIO_X(ctx, VITA_RZONE_UL_CX_RATIO);
    ctx->buttons[VITA_BTN_ID_RTOUCH_UL].cy = RATIO_Y(ctx, VITA_RZONE_UL_CY_RATIO);
    ctx->buttons[VITA_BTN_ID_RTOUCH_UL].is_circular = false;

    // Upper Right
    ctx->buttons[VITA_BTN_ID_RTOUCH_UR].x = pad_x + zone_w;
    ctx->buttons[VITA_BTN_ID_RTOUCH_UR].y = pad_y;
    ctx->buttons[VITA_BTN_ID_RTOUCH_UR].w = zone_w;
    ctx->buttons[VITA_BTN_ID_RTOUCH_UR].h = zone_h;
    ctx->buttons[VITA_BTN_ID_RTOUCH_UR].cx = RATIO_X(ctx, VITA_RZONE_UR_CX_RATIO);
    ctx->buttons[VITA_BTN_ID_RTOUCH_UR].cy = RATIO_Y(ctx, VITA_RZONE_UR_CY_RATIO);
    ctx->buttons[VITA_BTN_ID_RTOUCH_UR].is_circular = false;

    // Lower Left
    ctx->buttons[VITA_BTN_ID_RTOUCH_LL].x = pad_x;
    ctx->buttons[VITA_BTN_ID_RTOUCH_LL].y = pad_y + zone_h;
    ctx->buttons[VITA_BTN_ID_RTOUCH_LL].w = zone_w;
    ctx->buttons[VITA_BTN_ID_RTOUCH_LL].h = zone_h;
    ctx->buttons[VITA_BTN_ID_RTOUCH_LL].cx = RATIO_X(ctx, VITA_RZONE_LL_CX_RATIO);
    ctx->buttons[VITA_BTN_ID_RTOUCH_LL].cy = RATIO_Y(ctx, VITA_RZONE_LL_CY_RATIO);
    ctx->buttons[VITA_BTN_ID_RTOUCH_LL].is_circular = false;

    // Lower Right
    ctx->buttons[VITA_BTN_ID_RTOUCH_LR].x = pad_x + zone_w;
    ctx->buttons[VITA_BTN_ID_RTOUCH_LR].y = pad_y + zone_h;
    ctx->buttons[VITA_BTN_ID_RTOUCH_LR].w = zone_w;
    ctx->buttons[VITA_BTN_ID_RTOUCH_LR].h = zone_h;
    ctx->buttons[VITA_BTN_ID_RTOUCH_LR].cx = RATIO_X(ctx, VITA_RZONE_LR_CX_RATIO);
    ctx->buttons[VITA_BTN_ID_RTOUCH_LR].cy = RATIO_Y(ctx, VITA_RZONE_LR_CY_RATIO);
    ctx->buttons[VITA_BTN_ID_RTOUCH_LR].is_circular = false;
}

// ============================================================================
// Public Rendering Functions
// ============================================================================

/**
 * Draw front view of Vita controller using procedural rendering
 * Layer order: body fill → screen → shoulders → outline → dpad → face → sticks → system
 */
void ui_diagram_draw_front(DiagramRenderCtx* ctx) {
    // Layer 1: Body fill
    draw_front_body(ctx);

    // Layer 2: Screen area
    draw_front_screen(ctx);

    // Layer 3: Shoulder buttons
    draw_front_shoulders(ctx);

    // Layer 4: Body outline (on top of fills, under controls)
    draw_front_body_outline(ctx);

    // Layer 5: D-pad
    draw_front_dpad(ctx);

    // Layer 6: Face buttons
    draw_front_face_buttons(ctx);

    // Layer 7: Analog sticks
    draw_front_sticks(ctx);

    // Layer 8: System buttons (PS, Start, Select)
    draw_front_system_buttons(ctx);
}

/**
 * Draw back view of Vita controller with rear touchpad zones
 * Layer order: body fill → touchpad bg → zone dividers → outline → touchpad outline → camera
 */
void ui_diagram_draw_back(DiagramRenderCtx* ctx) {
    // Layer 1: Body fill
    draw_front_body(ctx);

    // Layer 2-3: Touchpad with zones
    draw_back_touchpad(ctx);

    // Layer 4: Body outline
    draw_front_body_outline(ctx);

    // Layer 5: Camera decoration
    draw_back_camera(ctx);
}

/**
 * Main diagram render function (delegates to procedural rendering)
 */
void ui_diagram_render(DiagramState* state, int x, int y, int w, int h) {
    // Initialize render context
    DiagramRenderCtx ctx = {0};
    ui_diagram_init_context(&ctx, x, y, w, h);

    // Background card
    ui_draw_card_with_shadow(x, y, w, h, 8, UI_COLOR_CARD_BG);

    // Apply flip animation scale if active
    if (state->flip_in_progress) {
        float t = state->flip_animation;
        float anim_scale = (t < 0.5f) ? lerp(1.0f, 0.95f, t * 2.0f) : lerp(0.95f, 1.0f, (t - 0.5f) * 2.0f);
        ctx.scale *= anim_scale;
        // Recalculate dimensions with animation scale
        ctx.width = (int)(w * ctx.scale);
        ctx.height = (int)(h * ctx.scale);
        ctx.base_x = x + (w - ctx.width) / 2;
        ctx.base_y = y + (h - ctx.height) / 2;
    }

    // Draw appropriate view
    if (state->mode == CTRL_VIEW_BACK) {
        ui_diagram_draw_back(&ctx);
    } else {
        ui_diagram_draw_front(&ctx);
    }

    // Draw overlays based on detail view
    if (state->detail_view == CTRL_DETAIL_SUMMARY) {
        draw_summary_callouts(state, &ctx);
    } else if (state->detail_view == CTRL_DETAIL_FRONT_MAPPING) {
        if (state->selected_button >= 0) {
            ui_diagram_draw_highlight(&ctx, state->selected_button, sinf(state->highlight_pulse * 2.0f * (float)M_PI));
        }
    } else if (state->detail_view == CTRL_DETAIL_BACK_MAPPING) {
        if (state->selected_zone >= 0) {
            ui_diagram_draw_zone_highlight(&ctx, state->selected_zone, sinf(state->highlight_pulse * 2.0f * (float)M_PI));
        }
    }

    // Draw BOTH view mode (front + back stacked) - not commonly used, kept for compatibility
    if (state->mode == CTRL_VIEW_BOTH) {
        float small_scale = 0.6f;
        int small_w = (int)(w * small_scale);
        int small_h = (int)(h * small_scale);

        // Reuse single context for both views to minimize stack usage
        DiagramRenderCtx both_ctx = {0};

        // Draw front view
        ui_diagram_init_context(&both_ctx, x + (w - small_w) / 2, y + 20, small_w, small_h);
        ui_diagram_draw_front(&both_ctx);

        // Reinitialize same context for back view
        ui_diagram_init_context(&both_ctx, x + (w - small_w) / 2, y + 20 + small_h + 10, small_w, small_h);
        ui_diagram_draw_back(&both_ctx);
    }
}

// ============================================================================
// State Management
// ============================================================================

void ui_diagram_init(DiagramState* state) {
    memset(state, 0, sizeof(DiagramState));
    state->mode = CTRL_VIEW_FRONT;
    state->detail_view = CTRL_DETAIL_SUMMARY;
    state->map_id = VITAKI_CONTROLLER_MAP_0;
    state->selected_button = -1;
    state->selected_zone = -1;
    state->highlight_pulse = 0.0f;
    state->flip_animation = 0.0f;
    state->color_tween = 0.0f;
    state->animation_start_us = 0;
    state->flip_in_progress = false;
    state->color_tween_active = false;
}

void ui_diagram_set_preset(DiagramState* state, VitakiControllerMapId map_id) {
    if (state->map_id != map_id) {
        state->map_id = map_id;

        // Trigger color tween animation
        state->color_tween = 0.0f;
        state->color_tween_active = true;
        state->animation_start_us = get_time_us();
    }
}

void ui_diagram_update(DiagramState* state) {
    uint64_t now_us = get_time_us();

    // Update highlight pulse (always active)
    uint64_t elapsed_ms = (now_us / 1000) % PULSE_PERIOD_MS;
    state->highlight_pulse = (float)elapsed_ms / (float)PULSE_PERIOD_MS;

    // Update flip animation
    if (state->flip_in_progress) {
        uint64_t anim_elapsed_us = now_us - state->animation_start_us;
        float t = (float)anim_elapsed_us / (FLIP_DURATION_MS * 1000.0f);

        if (t >= 1.0f) {
            state->flip_in_progress = false;
            state->flip_animation = 0.0f;
        } else {
            state->flip_animation = ease_in_out_cubic(t);
        }
    }

    // Update color tween
    if (state->color_tween_active) {
        uint64_t anim_elapsed_us = now_us - state->animation_start_us;
        float t = (float)anim_elapsed_us / (COLOR_TWEEN_DURATION_MS * 1000.0f);

        if (t >= 1.0f) {
            state->color_tween_active = false;
            state->color_tween = 0.0f;
        } else {
            state->color_tween = t;
        }
    }
}
