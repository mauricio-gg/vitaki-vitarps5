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
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

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

#define CONTROLLER_FRONT_TEXTURE_PATH TEXTURE_PATH "controller_front.png"
#define CONTROLLER_BACK_TEXTURE_PATH TEXTURE_PATH "controller_back.png"
#define FRONT_TEXTURE_ALPHA_THRESHOLD 64
#define BACK_TEXTURE_ALPHA_THRESHOLD 0
#define FRONT_TEXTURE_TINT RGBA8(255, 255, 255, 255)
#define BACK_TEXTURE_TINT RGBA8(255, 255, 255, 255)

#define CTRL_ARRAY_SIZE(arr) (int)(sizeof(arr) / sizeof((arr)[0]))

typedef struct ratio_point_t {
    float x;
    float y;
} RatioPoint;

#define FRONT_GRID_CELL_COUNT (VITAKI_FRONT_TOUCH_GRID_ROWS * VITAKI_FRONT_TOUCH_GRID_COLS)
#define BACK_GRID_CELL_COUNT  (VITAKI_REAR_TOUCH_GRID_ROWS * VITAKI_REAR_TOUCH_GRID_COLS)

typedef struct touch_region_info_t {
    bool active;
    VitakiCtrlOut output;
    int min_x;
    int min_y;
    int max_x;
    int max_y;
    int center_sum_x;
    int center_sum_y;
    int center_x;
    int center_y;
    int cell_count;
} TouchRegionInfo;

typedef struct diagram_callout_def_t {
    VitakiCtrlIn input;
    ControllerViewMode view;
    float anchor_rx;
    float anchor_ry;
    float label_rx;
    float label_ry;
    const char* label;
} DiagramCalloutDef;

typedef struct diagram_callout_page_t {
    int start;
    int count;
    const char* title;
} DiagramCalloutPage;

static const DiagramCalloutDef g_callouts[] = {
    { VITAKI_CTRL_IN_L1, CTRL_VIEW_FRONT, 0.10f, 0.12f, -0.13f, 0.08f, "L1" },
    { VITAKI_CTRL_IN_R1, CTRL_VIEW_FRONT, 0.90f, 0.12f, 1.02f, 0.08f, "R1" }
};

static const DiagramCalloutPage g_callout_pages[] = {
    { 0, 2, "Buttons" },
    { 2, 0, "Back Touch" }
};

#define CTRL_CALLOUT_PAGE_COUNT CTRL_ARRAY_SIZE(g_callout_pages)

static const char* g_touch_grid_labels[VITAKI_REAR_TOUCH_GRID_ROWS][VITAKI_REAR_TOUCH_GRID_COLS] = {
    { "A1", "B1", "C1", "D1", "E1", "F1" },
    { "A2", "B2", "C2", "D2", "E2", "F2" },
    { "A3", "B3", "C3", "D3", "E3", "F3" }
};

// ============================================================================
// Helper Macros
// ============================================================================

#define RATIO_X(ctx, r) ((ctx)->base_x + (int)((ctx)->width * (r)))
#define RATIO_Y(ctx, r) ((ctx)->base_y + (int)((ctx)->height * (r)))
#define RATIO_W(ctx, r) ((int)((ctx)->width * (r)))
#define RATIO_H(ctx, r) ((int)((ctx)->height * (r)))
#define RATIO_SIZE(ctx, r) RATIO_W(ctx, r)  // Backward compatibility, defaults to width

static const uint32_t k_mapping_fill_colors[] = {
    RGBA8(84, 132, 255, 120),   // Blue
    RGBA8(255, 159, 67, 120),   // Orange
    RGBA8(84, 222, 164, 120),   // Mint
    RGBA8(255, 99, 178, 120),   // Pink
    RGBA8(155, 132, 255, 120),  // Violet
    RGBA8(255, 205, 86, 120)    // Yellow
};

static const VitakiCtrlOut k_priority_outputs[] = {
    VITAKI_CTRL_OUT_OPTIONS,
    VITAKI_CTRL_OUT_SHARE,
    VITAKI_CTRL_OUT_TOUCHPAD,
    VITAKI_CTRL_OUT_L2,
    VITAKI_CTRL_OUT_R2,
    VITAKI_CTRL_OUT_L3,
    VITAKI_CTRL_OUT_R3,
    VITAKI_CTRL_OUT_PS
};

static inline uint32_t color_for_output(VitakiCtrlOut output) {
    if (output <= VITAKI_CTRL_OUT_NONE)
        return RGBA8(80, 130, 255, 90);

    size_t palette_count = sizeof(k_mapping_fill_colors) / sizeof(k_mapping_fill_colors[0]);
    for (size_t i = 0; i < CTRL_ARRAY_SIZE(k_priority_outputs); i++) {
        if (k_priority_outputs[i] == output) {
            return k_mapping_fill_colors[i % palette_count];
        }
    }

    uint32_t hash = (uint32_t)output * 2654435761u;
    return k_mapping_fill_colors[hash % palette_count];
}

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

static void draw_ratio_polyline(DiagramRenderCtx* ctx, const RatioPoint* pts, int count, uint32_t color, bool closed) {
    if (!pts || count < 2)
        return;
    int prev_x = RATIO_X(ctx, pts[0].x);
    int prev_y = RATIO_Y(ctx, pts[0].y);
    for (int i = 1; i < count; i++) {
        int x = RATIO_X(ctx, pts[i].x);
        int y = RATIO_Y(ctx, pts[i].y);
        vita2d_draw_line(prev_x, prev_y, x, y, color);
        prev_x = x;
        prev_y = y;
    }
    if (closed) {
        int x = RATIO_X(ctx, pts[0].x);
        int y = RATIO_Y(ctx, pts[0].y);
        vita2d_draw_line(prev_x, prev_y, x, y, color);
    }
}

static void draw_ratio_speckles(DiagramRenderCtx* ctx, const RatioPoint* pts, int count, uint32_t color, int size) {
    for (int i = 0; i < count; i++) {
        int x = RATIO_X(ctx, pts[i].x);
        int y = RATIO_Y(ctx, pts[i].y);
        vita2d_draw_rectangle(x - size / 2, y - size / 2, size, size, color);
    }
}

static void draw_ratio_circle_outline(DiagramRenderCtx* ctx, float rx, float ry, float rr, uint32_t color) {
    int cx = RATIO_X(ctx, rx);
    int cy = RATIO_Y(ctx, ry);
    int r = RATIO_SIZE(ctx, rr);
    ui_draw_circle_outline(cx, cy, r, color);
}

static void draw_dashed_rect_outline(int x, int y, int w, int h, uint32_t color, int dash_len, int gap_len) {
    if (w <= 0 || h <= 0 || dash_len <= 0)
        return;
    if (gap_len < 0)
        gap_len = 0;
    int step = dash_len + gap_len;
    if (step <= 0)
        step = dash_len;

    for (int offset = 0; offset < w; offset += step) {
        int seg = dash_len;
        if (offset + seg > w)
            seg = w - offset;
        if (seg <= 0)
            break;
        vita2d_draw_rectangle(x + offset, y, seg, 1, color);
        vita2d_draw_rectangle(x + offset, y + h - 1, seg, 1, color);
    }

    for (int offset = 0; offset < h; offset += step) {
        int seg = dash_len;
        if (offset + seg > h)
            seg = h - offset;
        if (seg <= 0)
            break;
        vita2d_draw_rectangle(x, y + offset, 1, seg, color);
        vita2d_draw_rectangle(x + w - 1, y + offset, 1, seg, color);
    }
}

static void draw_symbol_square(int x, int y, int size, uint32_t color) {
    vita2d_draw_rectangle(x - size, y - size, size * 2, size * 2, color);
    ui_draw_rectangle_outline(x - size - 1, y - size - 1, size * 2 + 2, size * 2 + 2, RGBA8(150, 170, 200, 180));
}

static void draw_symbol_triangle(int x, int y, int size, uint32_t color) {
    vita2d_draw_line(x, y - size, x - size, y + size, color);
    vita2d_draw_line(x - size, y + size, x + size, y + size, color);
    vita2d_draw_line(x + size, y + size, x, y - size, color);
}

static void draw_symbol_cross(int x, int y, int size, uint32_t color) {
    vita2d_draw_line(x - size, y - size, x + size, y + size, color);
    vita2d_draw_line(x - size, y + size, x + size, y - size, color);
}

static void sanitize_outline_texture(vita2d_texture* texture, uint8_t threshold) {
    if (!texture)
        return;

    uint8_t* data = vita2d_texture_get_datap(texture);
    if (!data)
        return;

    const unsigned int stride = vita2d_texture_get_stride(texture);
    const unsigned int width = vita2d_texture_get_width(texture);
    const unsigned int height = vita2d_texture_get_height(texture);

    for (unsigned int y = 0; y < height; y++) {
        uint32_t* row = (uint32_t*)(data + y * stride);
        for (unsigned int x = 0; x < width; x++) {
            uint32_t pixel = row[x];
            uint8_t alpha = (pixel >> 24) & 0xFF;
            if (alpha <= threshold) {
                row[x] = 0;
                continue;
            }
            uint8_t adjusted_alpha = (uint8_t)(((alpha - threshold) * 255) / (255 - threshold));
            row[x] = ((uint32_t)adjusted_alpha << 24) | 0x00FFFFFF;
        }
    }
}

static void draw_front_texture(DiagramRenderCtx* ctx, vita2d_texture* texture) {
    if (!texture)
        return;

    float tex_w = (float)vita2d_texture_get_width(texture);
    float tex_h = (float)vita2d_texture_get_height(texture);

    float scale = fminf((float)ctx->width / tex_w, (float)ctx->height / tex_h);
    float draw_w = tex_w * scale;
    float draw_h = tex_h * scale;
    float draw_x = (float)ctx->base_x + ((float)ctx->width - draw_w) / 2.0f;
    float draw_y = (float)ctx->base_y + ((float)ctx->height - draw_h) / 2.0f;

    vita2d_draw_texture_tint_scale(texture, draw_x, draw_y, scale, scale, FRONT_TEXTURE_TINT);
}

static void draw_back_texture(DiagramRenderCtx* ctx, vita2d_texture* texture) {
    if (!texture)
        return;

    float tex_w = (float)vita2d_texture_get_width(texture);
    float tex_h = (float)vita2d_texture_get_height(texture);

    float scale = fminf((float)ctx->width / tex_w, (float)ctx->height / tex_h);
    float draw_w = tex_w * scale;
    float draw_h = tex_h * scale;
    float draw_x = (float)ctx->base_x + ((float)ctx->width - draw_w) / 2.0f;
    float draw_y = (float)ctx->base_y + ((float)ctx->height - draw_h) / 2.0f;

    vita2d_draw_texture_tint_scale(texture, draw_x, draw_y, scale, scale, BACK_TEXTURE_TINT);
}

/**
 * Draw callout connector line (no arrowhead, keeps "A ---- B" style)
 */
static void draw_callout_arrow(int x1, int y1, int x2, int y2, uint32_t color) {
    vita2d_draw_line(x1, y1, x2, y2, color);
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

bool ui_diagram_front_zone_rect(DiagramRenderCtx* ctx, VitakiCtrlIn input,
                                int* out_x, int* out_y, int* out_w, int* out_h) {
    int screen_x = RATIO_X(ctx, VITA_SCREEN_X_RATIO);
    int screen_y = RATIO_Y(ctx, VITA_SCREEN_Y_RATIO);
    int screen_w = RATIO_W(ctx, VITA_SCREEN_W_RATIO);
    int screen_h = RATIO_H(ctx, VITA_SCREEN_H_RATIO);

    int zone_x = screen_x;
    int zone_y = screen_y;
    int zone_w = screen_w;
    int zone_h = screen_h;

    if (vitaki_ctrl_in_is_front_grid(input)) {
        int col = vitaki_ctrl_in_front_grid_col(input);
        int row = vitaki_ctrl_in_front_grid_row(input);

        int base_w = screen_w / VITAKI_FRONT_TOUCH_GRID_COLS;
        int extra_w = screen_w % VITAKI_FRONT_TOUCH_GRID_COLS;
        int base_h = screen_h / VITAKI_FRONT_TOUCH_GRID_ROWS;
        int extra_h = screen_h % VITAKI_FRONT_TOUCH_GRID_ROWS;

        int offset_x = col * base_w + ((col < extra_w) ? col : extra_w);
        int offset_y = row * base_h + ((row < extra_h) ? row : extra_h);
        int cell_w = base_w + (col < extra_w ? 1 : 0);
        int cell_h = base_h + (row < extra_h ? 1 : 0);

        zone_x = screen_x + offset_x;
        zone_y = screen_y + offset_y;
        zone_w = cell_w;
        zone_h = cell_h;
        if (zone_w < 1) zone_w = 1;
        if (zone_h < 1) zone_h = 1;

        if (out_x) *out_x = zone_x;
        if (out_y) *out_y = zone_y;
        if (out_w) *out_w = zone_w;
        if (out_h) *out_h = zone_h;
        return true;
    }

    switch (input) {
        case VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC:
            zone_w /= 2;
            zone_h /= 2;
            break;
        case VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC:
            zone_x += screen_w / 2;
            zone_w /= 2;
            zone_h /= 2;
            break;
        case VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC:
            zone_w /= 2;
            zone_y += screen_h / 2;
            zone_h /= 2;
            break;
        case VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC:
            zone_x += screen_w / 2;
            zone_w /= 2;
            zone_y += screen_h / 2;
            zone_h /= 2;
            break;
        case VITAKI_CTRL_IN_FRONTTOUCH_CENTER:
            zone_x += screen_w / 5;
            zone_w = (screen_w * 3) / 5;
            zone_y += screen_h / 5;
            zone_h = (screen_h * 3) / 5;
            break;
        case VITAKI_CTRL_IN_FRONTTOUCH_ANY:
            break;
        default:
            return false;
    }

    if (out_x) *out_x = zone_x;
    if (out_y) *out_y = zone_y;
    if (out_w) *out_w = zone_w;
    if (out_h) *out_h = zone_h;
    return true;
}

bool ui_diagram_back_zone_rect(DiagramRenderCtx* ctx, VitakiCtrlIn input,
                               int* out_x, int* out_y, int* out_w, int* out_h) {
    int pad_x = RATIO_X(ctx, VITA_RTOUCH_X_RATIO);
    int pad_y = RATIO_Y(ctx, VITA_RTOUCH_Y_RATIO);
    int pad_w = RATIO_W(ctx, VITA_RTOUCH_W_RATIO);
    int pad_h = RATIO_H(ctx, VITA_RTOUCH_H_RATIO);

    int zone_x = pad_x;
    int zone_y = pad_y;
    int zone_w = pad_w;
    int zone_h = pad_h;

    if (vitaki_ctrl_in_is_rear_grid(input)) {
        zone_w /= VITAKI_REAR_TOUCH_GRID_COLS;
        zone_h /= VITAKI_REAR_TOUCH_GRID_ROWS;
        int col = vitaki_ctrl_in_rear_grid_col(input);
        int row = vitaki_ctrl_in_rear_grid_row(input);
        zone_x += col * zone_w;
        zone_y += row * zone_h;
        if (out_x) *out_x = zone_x;
        if (out_y) *out_y = zone_y;
        if (out_w) *out_w = zone_w;
        if (out_h) *out_h = zone_h;
        return true;
    }

    switch (input) {
        case VITAKI_CTRL_IN_REARTOUCH_UL:
            zone_w /= 2;
            zone_h /= 2;
            break;
        case VITAKI_CTRL_IN_REARTOUCH_UR:
            zone_x += pad_w / 2;
            zone_w /= 2;
            zone_h /= 2;
            break;
        case VITAKI_CTRL_IN_REARTOUCH_LL:
            zone_w /= 2;
            zone_y += pad_h / 2;
            zone_h /= 2;
            break;
        case VITAKI_CTRL_IN_REARTOUCH_LR:
            zone_x += pad_w / 2;
            zone_w /= 2;
            zone_y += pad_h / 2;
            zone_h /= 2;
            break;
        case VITAKI_CTRL_IN_REARTOUCH_LEFT:
            zone_w /= 2;
            break;
        case VITAKI_CTRL_IN_REARTOUCH_RIGHT:
            zone_x += pad_w / 2;
            zone_w /= 2;
            break;
        case VITAKI_CTRL_IN_REARTOUCH_LEFT_L1:
            zone_x = pad_x - RATIO_W(ctx, 0.08f);
            zone_w = RATIO_W(ctx, 0.12f);
            zone_y = pad_y + pad_h / 6;
            zone_h = (pad_h * 2) / 3;
            break;
        case VITAKI_CTRL_IN_REARTOUCH_RIGHT_R1:
            zone_x = pad_x + pad_w - RATIO_W(ctx, 0.04f);
            zone_w = RATIO_W(ctx, 0.12f);
            zone_y = pad_y + pad_h / 6;
            zone_h = (pad_h * 2) / 3;
            break;
        case VITAKI_CTRL_IN_REARTOUCH_ANY:
            break;
        default:
            return false;
    }

    if (out_x) *out_x = zone_x;
    if (out_y) *out_y = zone_y;
    if (out_w) *out_w = zone_w;
    if (out_h) *out_h = zone_h;
    return true;
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

static bool anchor_from_button(DiagramRenderCtx* ctx, VitaDiagramButtonId btn_id,
                               int* out_x, int* out_y) {
    if (!ctx || btn_id < 0 || btn_id >= VITA_BTN_ID_COUNT)
        return false;
    const DiagramButtonPos* btn = &ctx->buttons[btn_id];
    if (out_x) *out_x = btn->cx;
    if (out_y) *out_y = btn->cy;
    return true;
}

static bool anchor_from_front_touch(DiagramRenderCtx* ctx, VitakiCtrlIn input,
                                    int* out_x, int* out_y) {
    int screen_x = RATIO_X(ctx, VITA_SCREEN_X_RATIO);
    int screen_y = RATIO_Y(ctx, VITA_SCREEN_Y_RATIO);
    int screen_w = RATIO_W(ctx, VITA_SCREEN_W_RATIO);
    int screen_h = RATIO_H(ctx, VITA_SCREEN_H_RATIO);

    int zone_x = screen_x;
    int zone_y = screen_y;
    int zone_w = screen_w;
    int zone_h = screen_h;

    switch (input) {
        case VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC:
            zone_w /= 2;
            zone_h /= 2;
            break;
        case VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC:
            zone_x += screen_w / 2;
            zone_w /= 2;
            zone_h /= 2;
            break;
        case VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC:
            zone_w /= 2;
            zone_y += screen_h / 2;
            zone_h /= 2;
            break;
        case VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC:
            zone_x += screen_w / 2;
            zone_w /= 2;
            zone_y += screen_h / 2;
            zone_h /= 2;
            break;
        case VITAKI_CTRL_IN_FRONTTOUCH_CENTER:
            zone_x += screen_w / 5;
            zone_w = (screen_w * 3) / 5;
            zone_y += screen_h / 5;
            zone_h = (screen_h * 3) / 5;
            break;
        case VITAKI_CTRL_IN_FRONTTOUCH_ANY:
            break;
        default:
            return false;
    }

    if (out_x) *out_x = zone_x + zone_w / 2;
    if (out_y) *out_y = zone_y + zone_h / 2;
    return true;
}

static bool anchor_from_back_touch(DiagramRenderCtx* ctx, VitakiCtrlIn input,
                                   int* out_x, int* out_y) {
    int pad_x = RATIO_X(ctx, VITA_RTOUCH_X_RATIO);
    int pad_y = RATIO_Y(ctx, VITA_RTOUCH_Y_RATIO);
    int pad_w = RATIO_W(ctx, VITA_RTOUCH_W_RATIO);
    int pad_h = RATIO_H(ctx, VITA_RTOUCH_H_RATIO);

    int zone_x = pad_x;
    int zone_y = pad_y;
    int zone_w = pad_w;
    int zone_h = pad_h;

    switch (input) {
        case VITAKI_CTRL_IN_REARTOUCH_UL:
            zone_w /= 2;
            zone_h /= 2;
            break;
        case VITAKI_CTRL_IN_REARTOUCH_UR:
            zone_x += pad_w / 2;
            zone_w /= 2;
            zone_h /= 2;
            break;
        case VITAKI_CTRL_IN_REARTOUCH_LL:
            zone_w /= 2;
            zone_y += pad_h / 2;
            zone_h /= 2;
            break;
        case VITAKI_CTRL_IN_REARTOUCH_LR:
            zone_x += pad_w / 2;
            zone_w /= 2;
            zone_y += pad_h / 2;
            zone_h /= 2;
            break;
        case VITAKI_CTRL_IN_REARTOUCH_LEFT_L1:
        case VITAKI_CTRL_IN_REARTOUCH_LEFT:
            zone_x = pad_x - RATIO_W(ctx, 0.08f);
            zone_w = RATIO_W(ctx, 0.12f);
            zone_y = pad_y + pad_h / 6;
            zone_h = (pad_h * 2) / 3;
            break;
        case VITAKI_CTRL_IN_REARTOUCH_RIGHT_R1:
        case VITAKI_CTRL_IN_REARTOUCH_RIGHT:
            zone_x = pad_x + pad_w - RATIO_W(ctx, 0.04f);
            zone_w = RATIO_W(ctx, 0.12f);
            zone_y = pad_y + pad_h / 6;
            zone_h = (pad_h * 2) / 3;
            break;
        case VITAKI_CTRL_IN_REARTOUCH_ANY:
            break;
        default:
            return false;
    }

    if (out_x) *out_x = zone_x + zone_w / 2;
    if (out_y) *out_y = zone_y + zone_h / 2;
    return true;
}

static bool callout_anchor_for_input(DiagramRenderCtx* ctx, VitakiCtrlIn input,
                                     int* out_x, int* out_y) {
    switch (input) {
        case VITAKI_CTRL_IN_L1:
            return anchor_from_button(ctx, VITA_BTN_ID_L, out_x, out_y);
        case VITAKI_CTRL_IN_R1:
            return anchor_from_button(ctx, VITA_BTN_ID_R, out_x, out_y);
        case VITAKI_CTRL_IN_SELECT_START:
            return anchor_from_button(ctx, VITA_BTN_ID_PS, out_x, out_y);
        case VITAKI_CTRL_IN_LEFT_SQUARE:
            return anchor_from_button(ctx, VITA_BTN_ID_LSTICK, out_x, out_y);
        case VITAKI_CTRL_IN_RIGHT_CIRCLE:
            return anchor_from_button(ctx, VITA_BTN_ID_RSTICK, out_x, out_y);
        default:
            break;
    }

    if (anchor_from_front_touch(ctx, input, out_x, out_y))
        return true;
    if (anchor_from_back_touch(ctx, input, out_x, out_y))
        return true;

    return false;
}

static void draw_anchor_label(const char* text, int x, int y, uint32_t color) {
    if (!text)
        return;
    int text_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, text);
    int text_x = x - text_w / 2;
    int text_y = y - 6;
    vita2d_font_draw_text(font, text_x, text_y, color, FONT_SIZE_SMALL, text);
}

static void draw_zone_mapping_text(int cx, int cy, const char* zone_label, const char* mapping_text) {
    if (!mapping_text) {
        mapping_text = "None";
    }
    uint32_t label_color = UI_COLOR_TEXT_TERTIARY;
    uint32_t mapping_color = UI_COLOR_TEXT_PRIMARY;
    int zone_w = zone_label ? vita2d_font_text_width(font, FONT_SIZE_SMALL, zone_label) : 0;
    int map_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, mapping_text);

    if (zone_label && zone_label[0] != '\0') {
        int zone_x = cx - zone_w / 2;
        vita2d_font_draw_text(font, zone_x, cy - 6, label_color, FONT_SIZE_SMALL, zone_label);
    }

    int map_x = cx - map_w / 2;
    vita2d_font_draw_text(font, map_x, cy + 10, mapping_color, FONT_SIZE_SMALL, mapping_text);
}

static void draw_front_touch_overlay(DiagramRenderCtx* ctx, const VitakiCtrlMapInfo* map, const bool* selection_mask) {
    if (!ctx || !map)
        return;

    int screen_x = RATIO_X(ctx, VITA_SCREEN_X_RATIO);
    int screen_y = RATIO_Y(ctx, VITA_SCREEN_Y_RATIO);
    int screen_w = RATIO_W(ctx, VITA_SCREEN_W_RATIO);
    int screen_h = RATIO_H(ctx, VITA_SCREEN_H_RATIO);

    uint32_t mask_color = RGBA8(5, 10, 18, 165);
    vita2d_draw_rectangle(screen_x, screen_y, screen_w, screen_h, mask_color);

    VitakiCtrlOut cell_outputs[FRONT_GRID_CELL_COUNT];
    memset(cell_outputs, 0, sizeof(cell_outputs));
    for (int idx = 0; idx < FRONT_GRID_CELL_COUNT; idx++) {
        VitakiCtrlIn input = (VitakiCtrlIn)(VITAKI_CTRL_IN_FRONTTOUCH_GRID_START + idx);
        cell_outputs[idx] = controller_map_get_output_for_input(map, input);
    }

    int region_ids[FRONT_GRID_CELL_COUNT];
    for (int i = 0; i < FRONT_GRID_CELL_COUNT; i++)
        region_ids[i] = -1;
    TouchRegionInfo regions[FRONT_GRID_CELL_COUNT];
    int region_count = 0;

    int queue[FRONT_GRID_CELL_COUNT];

    for (int idx = 0; idx < FRONT_GRID_CELL_COUNT; idx++) {
        if (cell_outputs[idx] == VITAKI_CTRL_OUT_NONE || region_ids[idx] >= 0)
            continue;
        TouchRegionInfo region = {0};
        region.active = true;
        region.output = cell_outputs[idx];
        region.min_x = region.min_y = INT_MAX;
        region.max_x = region.max_y = INT_MIN;
        int head = 0, tail = 0;
        queue[tail++] = idx;
        region_ids[idx] = region_count;
        while (head < tail) {
            int current = queue[head++];
            int row = current / VITAKI_FRONT_TOUCH_GRID_COLS;
            int col = current % VITAKI_FRONT_TOUCH_GRID_COLS;
            VitakiCtrlIn cell_input = (VitakiCtrlIn)(VITAKI_CTRL_IN_FRONTTOUCH_GRID_START + current);
            int zx, zy, zw, zh;
            if (!ui_diagram_front_zone_rect(ctx, cell_input, &zx, &zy, &zw, &zh))
                continue;
            int cx = zx + zw / 2;
            int cy = zy + zh / 2;
            if (zx < region.min_x) region.min_x = zx;
            if (zy < region.min_y) region.min_y = zy;
            if (zx + zw > region.max_x) region.max_x = zx + zw;
            if (zy + zh > region.max_y) region.max_y = zy + zh;
            region.center_sum_x += cx;
            region.center_sum_y += cy;
            region.cell_count++;

            int neighbors[4] = {-1, -1, -1, -1};
            if (col > 0) neighbors[0] = current - 1;
            if (col < VITAKI_FRONT_TOUCH_GRID_COLS - 1) neighbors[1] = current + 1;
            if (row > 0) neighbors[2] = current - VITAKI_FRONT_TOUCH_GRID_COLS;
            if (row < VITAKI_FRONT_TOUCH_GRID_ROWS - 1) neighbors[3] = current + VITAKI_FRONT_TOUCH_GRID_COLS;
            for (int n = 0; n < 4; n++) {
                int next = neighbors[n];
                if (next < 0)
                    continue;
                if (cell_outputs[next] != region.output)
                    continue;
                if (region_ids[next] >= 0)
                    continue;
                region_ids[next] = region_count;
                queue[tail++] = next;
            }
        }
        if (region.cell_count > 0) {
            region.center_x = region.center_sum_x / region.cell_count;
            region.center_y = region.center_sum_y / region.cell_count;
        }
        regions[region_count++] = region;
    }

    uint32_t selection_fill = RGBA8(70, 120, 255, 110);
    uint32_t selection_border = RGBA8(255, 90, 180, 230);
    uint32_t mapped_border = RGBA8(255, 65, 170, 220);
    uint32_t dashed_border = RGBA8(255, 255, 255, 190);
    const int dashed_len = 6;
    const int dashed_gap = 4;
    const int mapped_border_thickness = 2;

    for (int idx = 0; idx < FRONT_GRID_CELL_COUNT; idx++) {
        int row = idx / VITAKI_FRONT_TOUCH_GRID_COLS;
        int col = idx % VITAKI_FRONT_TOUCH_GRID_COLS;
        VitakiCtrlIn input = (VitakiCtrlIn)(VITAKI_CTRL_IN_FRONTTOUCH_GRID_START + idx);
        int zx, zy, zw, zh;
        if (!ui_diagram_front_zone_rect(ctx, input, &zx, &zy, &zw, &zh))
            continue;

        bool is_selected = selection_mask && selection_mask[idx];
        bool has_mapping = cell_outputs[idx] != VITAKI_CTRL_OUT_NONE;

        if (is_selected) {
            vita2d_draw_rectangle(zx + 1, zy + 1, zw - 2, zh - 2, selection_fill);
            ui_draw_rectangle_outline(zx, zy, zw, zh, selection_border);
            continue;
        }

        if (has_mapping) {
            uint32_t fill_color = color_for_output(cell_outputs[idx]);
            vita2d_draw_rectangle(zx, zy, zw, zh, fill_color);
            bool same_left = (col > 0) && (cell_outputs[idx - 1] == cell_outputs[idx]);
            bool same_right = (col < VITAKI_FRONT_TOUCH_GRID_COLS - 1) && (cell_outputs[idx + 1] == cell_outputs[idx]);
            bool same_top = (row > 0) && (cell_outputs[idx - VITAKI_FRONT_TOUCH_GRID_COLS] == cell_outputs[idx]);
            bool same_bottom = (row < VITAKI_FRONT_TOUCH_GRID_ROWS - 1) && (cell_outputs[idx + VITAKI_FRONT_TOUCH_GRID_COLS] == cell_outputs[idx]);

            int thick_w = mapped_border_thickness;
            if (thick_w > zw) thick_w = zw;
            int thick_h = mapped_border_thickness;
            if (thick_h > zh) thick_h = zh;

            if (!same_top) {
                vita2d_draw_rectangle(zx, zy, zw, thick_h, mapped_border);
            }
            if (!same_bottom) {
                int yb = zy + zh - thick_h;
                if (yb < zy) yb = zy;
                vita2d_draw_rectangle(zx, yb, zw, thick_h, mapped_border);
            }
            if (!same_left) {
                vita2d_draw_rectangle(zx, zy, thick_w, zh, mapped_border);
            }
            if (!same_right) {
                int xr = zx + zw - thick_w;
                if (xr < zx) xr = zx;
                vita2d_draw_rectangle(xr, zy, thick_w, zh, mapped_border);
            }
        } else {
            draw_dashed_rect_outline(zx, zy, zw, zh, dashed_border, dashed_len, dashed_gap);
        }
    }

    for (int r = 0; r < region_count; r++) {
        if (!regions[r].active || regions[r].cell_count == 0)
            continue;
        const char* label = controller_output_symbol(regions[r].output);
        draw_zone_mapping_text(regions[r].center_x, regions[r].center_y, "", label);
    }

    VitakiCtrlOut full_touch = controller_map_get_output_for_input(map, VITAKI_CTRL_IN_FRONTTOUCH_ANY);
    const char* full_text = controller_output_symbol(full_touch);
    draw_zone_mapping_text(screen_x + screen_w / 2, screen_y + FONT_SIZE_SMALL, "Full", full_text);
}

static void draw_back_touch_overlay(DiagramRenderCtx* ctx, const VitakiCtrlMapInfo* map, const bool* selection_mask) {
    if (!ctx || !map)
        return;

    int pad_x = RATIO_X(ctx, VITA_RTOUCH_X_RATIO);
    int pad_y = RATIO_Y(ctx, VITA_RTOUCH_Y_RATIO);
    int pad_w = RATIO_W(ctx, VITA_RTOUCH_W_RATIO);
    int pad_h = RATIO_H(ctx, VITA_RTOUCH_H_RATIO);
    uint32_t mask_color = RGBA8(5, 10, 18, 140);

    vita2d_draw_rectangle(pad_x, pad_y, pad_w, pad_h, mask_color);

    VitakiCtrlOut slot_outputs[BACK_GRID_CELL_COUNT];
    for (int idx = 0; idx < BACK_GRID_CELL_COUNT; idx++) {
        VitakiCtrlIn input = (VitakiCtrlIn)(VITAKI_CTRL_IN_REARTOUCH_GRID_START + idx);
        slot_outputs[idx] = controller_map_get_output_for_input(map, input);
    }

    int region_ids[BACK_GRID_CELL_COUNT];
    for (int i = 0; i < BACK_GRID_CELL_COUNT; i++)
        region_ids[i] = -1;
    TouchRegionInfo regions[BACK_GRID_CELL_COUNT];
    int region_count = 0;
    int queue[BACK_GRID_CELL_COUNT];

    for (int idx = 0; idx < BACK_GRID_CELL_COUNT; idx++) {
        if (slot_outputs[idx] == VITAKI_CTRL_OUT_NONE || region_ids[idx] >= 0)
            continue;
        TouchRegionInfo region = {0};
        region.active = true;
        region.output = slot_outputs[idx];
        region.min_x = region.min_y = INT_MAX;
        region.max_x = region.max_y = INT_MIN;
        int head = 0, tail = 0;
        queue[tail++] = idx;
        region_ids[idx] = region_count;
        while (head < tail) {
            int current = queue[head++];
            int row = current / VITAKI_REAR_TOUCH_GRID_COLS;
            int col = current % VITAKI_REAR_TOUCH_GRID_COLS;
            VitakiCtrlIn cell_input = (VitakiCtrlIn)(VITAKI_CTRL_IN_REARTOUCH_GRID_START + current);
            int zx, zy, zw, zh;
            if (!ui_diagram_back_zone_rect(ctx, cell_input, &zx, &zy, &zw, &zh))
                continue;
            int cx = zx + zw / 2;
            int cy = zy + zh / 2;
            if (zx < region.min_x) region.min_x = zx;
            if (zy < region.min_y) region.min_y = zy;
            if (zx + zw > region.max_x) region.max_x = zx + zw;
            if (zy + zh > region.max_y) region.max_y = zy + zh;
            region.center_sum_x += cx;
            region.center_sum_y += cy;
            region.cell_count++;

            int neighbors[4] = {-1, -1, -1, -1};
            if (col > 0) neighbors[0] = current - 1;
            if (col < VITAKI_REAR_TOUCH_GRID_COLS - 1) neighbors[1] = current + 1;
            if (row > 0) neighbors[2] = current - VITAKI_REAR_TOUCH_GRID_COLS;
            if (row < VITAKI_REAR_TOUCH_GRID_ROWS - 1) neighbors[3] = current + VITAKI_REAR_TOUCH_GRID_COLS;
            for (int n = 0; n < 4; n++) {
                int next = neighbors[n];
                if (next < 0)
                    continue;
                if (slot_outputs[next] != region.output)
                    continue;
                if (region_ids[next] >= 0)
                    continue;
                region_ids[next] = region_count;
                queue[tail++] = next;
            }
        }
        if (region.cell_count > 0) {
            region.center_x = region.center_sum_x / region.cell_count;
            region.center_y = region.center_sum_y / region.cell_count;
        }
        regions[region_count++] = region;
    }

    uint32_t selection_fill = RGBA8(70, 120, 255, 110);
    uint32_t selection_border = RGBA8(255, 90, 180, 230);
    uint32_t mapped_border = RGBA8(255, 65, 170, 220);
    uint32_t dashed_border = RGBA8(255, 255, 255, 190);
    const int dashed_len = 6;
    const int dashed_gap = 4;
    const int mapped_border_thickness = 2;

    for (int idx = 0; idx < BACK_GRID_CELL_COUNT; idx++) {
        int row = idx / VITAKI_REAR_TOUCH_GRID_COLS;
        int col = idx % VITAKI_REAR_TOUCH_GRID_COLS;
        VitakiCtrlIn input = (VitakiCtrlIn)(VITAKI_CTRL_IN_REARTOUCH_GRID_START + idx);
        int zx, zy, zw, zh;
        if (!ui_diagram_back_zone_rect(ctx, input, &zx, &zy, &zw, &zh))
            continue;

        bool is_selected = selection_mask && selection_mask[idx];
        bool has_mapping = slot_outputs[idx] != VITAKI_CTRL_OUT_NONE;
        if (is_selected) {
            vita2d_draw_rectangle(zx + 1, zy + 1, zw - 2, zh - 2, selection_fill);
            ui_draw_rectangle_outline(zx, zy, zw, zh, selection_border);
            continue;
        }
        if (has_mapping) {
            uint32_t fill_color = color_for_output(slot_outputs[idx]);
            vita2d_draw_rectangle(zx, zy, zw, zh, fill_color);

            bool same_left = (col > 0) && (slot_outputs[idx - 1] == slot_outputs[idx]);
            bool same_right = (col < VITAKI_REAR_TOUCH_GRID_COLS - 1) && (slot_outputs[idx + 1] == slot_outputs[idx]);
            bool same_top = (row > 0) && (slot_outputs[idx - VITAKI_REAR_TOUCH_GRID_COLS] == slot_outputs[idx]);
            bool same_bottom = (row < VITAKI_REAR_TOUCH_GRID_ROWS - 1) && (slot_outputs[idx + VITAKI_REAR_TOUCH_GRID_COLS] == slot_outputs[idx]);

            int thick_w = mapped_border_thickness;
            if (thick_w > zw) thick_w = zw;
            int thick_h = mapped_border_thickness;
            if (thick_h > zh) thick_h = zh;

            if (!same_top) {
                vita2d_draw_rectangle(zx, zy, zw, thick_h, mapped_border);
            }
            if (!same_bottom) {
                int yb = zy + zh - thick_h;
                if (yb < zy) yb = zy;
                vita2d_draw_rectangle(zx, yb, zw, thick_h, mapped_border);
            }
            if (!same_left) {
                vita2d_draw_rectangle(zx, zy, thick_w, zh, mapped_border);
            }
            if (!same_right) {
                int xr = zx + zw - thick_w;
                if (xr < zx) xr = zx;
                vita2d_draw_rectangle(xr, zy, thick_w, zh, mapped_border);
            }
        } else {
            draw_dashed_rect_outline(zx, zy, zw, zh, dashed_border, dashed_len, dashed_gap);
        }
    }

    for (int r = 0; r < region_count; r++) {
        if (!regions[r].active || regions[r].cell_count == 0)
            continue;
        const char* label = controller_output_symbol(regions[r].output);
        draw_zone_mapping_text(regions[r].center_x, regions[r].center_y, "", label);
    }

    for (int row = 0; row < VITAKI_REAR_TOUCH_GRID_ROWS; row++) {
        for (int col = 0; col < VITAKI_REAR_TOUCH_GRID_COLS; col++) {
            int idx = row * VITAKI_REAR_TOUCH_GRID_COLS + col;
            VitakiCtrlIn input = (VitakiCtrlIn)(VITAKI_CTRL_IN_REARTOUCH_GRID_START + idx);
            int zx, zy, zw, zh;
            if (!ui_diagram_back_zone_rect(ctx, input, &zx, &zy, &zw, &zh))
                continue;
            int cx = zx + zw / 2;
            int cy = zy + zh / 2;
            VitakiCtrlOut mapped = slot_outputs[idx];
            if (mapped == VITAKI_CTRL_OUT_NONE) {
                const char* label = g_touch_grid_labels[row][col];
                draw_zone_mapping_text(cx, cy, label, "None");
            }
        }
    }

    int full_x, full_y, full_w, full_h;
    if (ui_diagram_back_zone_rect(ctx, VITAKI_CTRL_IN_REARTOUCH_ANY, &full_x, &full_y, &full_w, &full_h)) {
        VitakiCtrlOut mapped = controller_map_get_output_for_input(map, VITAKI_CTRL_IN_REARTOUCH_ANY);
        draw_zone_mapping_text(full_x + full_w / 2, pad_y + FONT_SIZE_SMALL, "Full", controller_output_symbol(mapped));
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

static const RatioPoint FRONT_BODY_TOP[] = {
    { 0.044838f, 0.431193f },
    { 0.049009f, 0.357798f },
    { 0.053180f, 0.318807f },
    { 0.057351f, 0.288991f },
    { 0.061522f, 0.266055f },
    { 0.065693f, 0.245413f },
    { 0.069864f, 0.229358f },
    { 0.074035f, 0.213303f },
    { 0.078206f, 0.199541f },
    { 0.082377f, 0.188073f },
    { 0.086548f, 0.176606f },
    { 0.090719f, 0.167431f },
    { 0.094891f, 0.155963f },
    { 0.099062f, 0.149083f },
    { 0.103233f, 0.139908f },
    { 0.107404f, 0.130734f },
    { 0.111575f, 0.123853f },
    { 0.115746f, 0.114679f },
    { 0.119917f, 0.107798f },
    { 0.124088f, 0.100917f },
    { 0.128259f, 0.096330f },
    { 0.132430f, 0.089450f },
    { 0.136601f, 0.084862f },
    { 0.140772f, 0.080275f },
    { 0.144943f, 0.075688f },
    { 0.149114f, 0.073394f },
    { 0.153285f, 0.068807f },
    { 0.157456f, 0.066514f },
    { 0.161627f, 0.061927f },
    { 0.165798f, 0.059633f },
    { 0.169969f, 0.057339f },
    { 0.174140f, 0.055046f },
    { 0.178311f, 0.052752f },
    { 0.182482f, 0.052752f },
    { 0.186653f, 0.050459f },
    { 0.190824f, 0.048165f },
    { 0.194995f, 0.048165f },
    { 0.199166f, 0.048165f },
    { 0.203337f, 0.045872f },
    { 0.207508f, 0.045872f },
    { 0.211679f, 0.045872f },
    { 0.215850f, 0.045872f },
    { 0.220021f, 0.045872f },
    { 0.224192f, 0.045872f },
    { 0.228363f, 0.045872f },
    { 0.232534f, 0.045872f },
    { 0.236705f, 0.045872f },
    { 0.240876f, 0.045872f },
    { 0.245047f, 0.045872f },
    { 0.249218f, 0.045872f },
    { 0.253389f, 0.045872f },
    { 0.257560f, 0.045872f },
    { 0.261731f, 0.045872f },
    { 0.265902f, 0.045872f },
    { 0.270073f, 0.045872f },
    { 0.274244f, 0.045872f },
    { 0.278415f, 0.045872f },
    { 0.282586f, 0.045872f },
    { 0.286757f, 0.045872f },
    { 0.290928f, 0.045872f },
    { 0.295099f, 0.045872f },
    { 0.299270f, 0.045872f },
    { 0.303441f, 0.045872f },
    { 0.307612f, 0.045872f },
    { 0.311783f, 0.045872f },
    { 0.315954f, 0.045872f },
    { 0.320125f, 0.045872f },
    { 0.324296f, 0.045872f },
    { 0.328467f, 0.045872f },
    { 0.332638f, 0.045872f },
    { 0.336809f, 0.045872f },
    { 0.340980f, 0.045872f },
    { 0.345151f, 0.045872f },
    { 0.349322f, 0.045872f },
    { 0.353493f, 0.045872f },
    { 0.357664f, 0.045872f },
    { 0.361835f, 0.045872f },
    { 0.366006f, 0.045872f },
    { 0.370177f, 0.045872f },
    { 0.374348f, 0.045872f },
    { 0.378519f, 0.045872f },
    { 0.382690f, 0.045872f },
    { 0.386861f, 0.045872f },
    { 0.391032f, 0.045872f },
    { 0.395203f, 0.045872f },
    { 0.399374f, 0.045872f },
    { 0.403545f, 0.045872f },
    { 0.407716f, 0.045872f },
    { 0.411887f, 0.045872f },
    { 0.416058f, 0.045872f },
    { 0.420229f, 0.045872f },
    { 0.424400f, 0.045872f },
    { 0.428571f, 0.045872f },
    { 0.432742f, 0.045872f },
    { 0.436913f, 0.045872f },
    { 0.441084f, 0.045872f },
    { 0.445255f, 0.045872f },
    { 0.449426f, 0.045872f },
    { 0.453597f, 0.045872f },
    { 0.457769f, 0.045872f },
    { 0.461940f, 0.045872f },
    { 0.466111f, 0.045872f },
    { 0.470282f, 0.045872f },
    { 0.474453f, 0.045872f },
    { 0.478624f, 0.045872f },
    { 0.482795f, 0.045872f },
    { 0.486966f, 0.045872f },
    { 0.491137f, 0.045872f },
    { 0.495308f, 0.045872f },
    { 0.499479f, 0.045872f },
    { 0.503650f, 0.045872f },
    { 0.507821f, 0.045872f },
    { 0.511992f, 0.045872f },
    { 0.516163f, 0.045872f },
    { 0.520334f, 0.045872f },
    { 0.524505f, 0.045872f },
    { 0.528676f, 0.045872f },
    { 0.532847f, 0.045872f },
    { 0.537018f, 0.045872f },
    { 0.541189f, 0.045872f },
    { 0.545360f, 0.045872f },
    { 0.549531f, 0.045872f },
    { 0.553702f, 0.045872f },
    { 0.557873f, 0.045872f },
    { 0.562044f, 0.045872f },
    { 0.566215f, 0.045872f },
    { 0.570386f, 0.045872f },
    { 0.574557f, 0.045872f },
    { 0.578728f, 0.045872f },
    { 0.582899f, 0.045872f },
    { 0.587070f, 0.045872f },
    { 0.591241f, 0.045872f },
    { 0.595412f, 0.045872f },
    { 0.599583f, 0.045872f },
    { 0.603754f, 0.045872f },
    { 0.607925f, 0.045872f },
    { 0.612096f, 0.045872f },
    { 0.616267f, 0.045872f },
    { 0.620438f, 0.045872f },
    { 0.624609f, 0.045872f },
    { 0.628780f, 0.045872f },
    { 0.632951f, 0.045872f },
    { 0.637122f, 0.045872f },
    { 0.641293f, 0.045872f },
    { 0.645464f, 0.045872f },
    { 0.649635f, 0.045872f },
    { 0.653806f, 0.045872f },
    { 0.657977f, 0.045872f },
    { 0.662148f, 0.045872f },
    { 0.666319f, 0.045872f },
    { 0.670490f, 0.045872f },
    { 0.674661f, 0.045872f },
    { 0.678832f, 0.045872f },
    { 0.683003f, 0.045872f },
    { 0.687174f, 0.045872f },
    { 0.691345f, 0.045872f },
    { 0.695516f, 0.045872f },
    { 0.699687f, 0.045872f },
    { 0.703858f, 0.045872f },
    { 0.708029f, 0.045872f },
    { 0.712200f, 0.045872f },
    { 0.716371f, 0.045872f },
    { 0.720542f, 0.045872f },
    { 0.724713f, 0.045872f },
    { 0.728884f, 0.045872f },
    { 0.733055f, 0.045872f },
    { 0.737226f, 0.045872f },
    { 0.741397f, 0.045872f },
    { 0.745568f, 0.045872f },
    { 0.749739f, 0.045872f },
    { 0.753910f, 0.045872f },
    { 0.758081f, 0.045872f },
    { 0.762252f, 0.045872f },
    { 0.766423f, 0.045872f },
    { 0.770594f, 0.045872f },
    { 0.774765f, 0.045872f },
    { 0.778936f, 0.045872f },
    { 0.783107f, 0.045872f },
    { 0.787278f, 0.045872f },
    { 0.791449f, 0.045872f },
    { 0.795620f, 0.045872f },
    { 0.799791f, 0.048165f },
    { 0.803962f, 0.048165f },
    { 0.808133f, 0.048165f },
    { 0.812304f, 0.050459f },
    { 0.816475f, 0.050459f },
    { 0.820647f, 0.052752f },
    { 0.824818f, 0.055046f },
    { 0.828989f, 0.057339f },
    { 0.833160f, 0.059633f },
    { 0.837331f, 0.061927f },
    { 0.841502f, 0.064220f },
    { 0.845673f, 0.066514f },
    { 0.849844f, 0.071101f },
    { 0.854015f, 0.075688f },
    { 0.858186f, 0.077982f },
    { 0.862357f, 0.082569f },
    { 0.866528f, 0.089450f },
    { 0.870699f, 0.094037f },
    { 0.874870f, 0.100917f },
    { 0.879041f, 0.107798f },
    { 0.883212f, 0.114679f },
    { 0.887383f, 0.121560f },
    { 0.891554f, 0.130734f },
    { 0.895725f, 0.137615f },
    { 0.899896f, 0.146789f },
    { 0.904067f, 0.155963f },
    { 0.908238f, 0.165138f },
    { 0.912409f, 0.174312f },
    { 0.916580f, 0.185780f },
    { 0.920751f, 0.197248f },
    { 0.924922f, 0.211009f },
    { 0.929093f, 0.224771f },
    { 0.933264f, 0.240826f },
    { 0.937435f, 0.261468f },
    { 0.941606f, 0.284404f },
    { 0.945777f, 0.311927f },
    { 0.949948f, 0.346330f },
    { 0.954119f, 0.405963f },
};

static const RatioPoint FRONT_BODY_BOTTOM[] = {
    { 0.044838f, 0.545872f },
    { 0.049009f, 0.621560f },
    { 0.053180f, 0.662844f },
    { 0.057351f, 0.694954f },
    { 0.061522f, 0.717890f },
    { 0.065693f, 0.738532f },
    { 0.069864f, 0.756881f },
    { 0.074035f, 0.770642f },
    { 0.078206f, 0.784404f },
    { 0.082377f, 0.798165f },
    { 0.086548f, 0.809633f },
    { 0.090719f, 0.818807f },
    { 0.094891f, 0.827982f },
    { 0.099062f, 0.837156f },
    { 0.103233f, 0.846330f },
    { 0.107404f, 0.857798f },
    { 0.111575f, 0.866972f },
    { 0.115746f, 0.873853f },
    { 0.119917f, 0.880734f },
    { 0.124088f, 0.885321f },
    { 0.128259f, 0.892202f },
    { 0.132430f, 0.896789f },
    { 0.136601f, 0.903670f },
    { 0.140772f, 0.908257f },
    { 0.144943f, 0.912844f },
    { 0.149114f, 0.917431f },
    { 0.153285f, 0.922018f },
    { 0.157456f, 0.926606f },
    { 0.161627f, 0.928899f },
    { 0.165798f, 0.933486f },
    { 0.169969f, 0.935780f },
    { 0.174140f, 0.938073f },
    { 0.178311f, 0.940367f },
    { 0.182482f, 0.942661f },
    { 0.186653f, 0.944954f },
    { 0.190824f, 0.947248f },
    { 0.194995f, 0.947248f },
    { 0.199166f, 0.949541f },
    { 0.203337f, 0.949541f },
    { 0.207508f, 0.951835f },
    { 0.211679f, 0.951835f },
    { 0.215850f, 0.951835f },
    { 0.220021f, 0.951835f },
    { 0.224192f, 0.951835f },
    { 0.228363f, 0.951835f },
    { 0.232534f, 0.951835f },
    { 0.236705f, 0.951835f },
    { 0.240876f, 0.951835f },
    { 0.245047f, 0.951835f },
    { 0.249218f, 0.951835f },
    { 0.253389f, 0.951835f },
    { 0.257560f, 0.951835f },
    { 0.261731f, 0.951835f },
    { 0.265902f, 0.951835f },
    { 0.270073f, 0.951835f },
    { 0.274244f, 0.951835f },
    { 0.278415f, 0.951835f },
    { 0.282586f, 0.951835f },
    { 0.286757f, 0.951835f },
    { 0.290928f, 0.951835f },
    { 0.295099f, 0.951835f },
    { 0.299270f, 0.951835f },
    { 0.303441f, 0.951835f },
    { 0.307612f, 0.951835f },
    { 0.311783f, 0.951835f },
    { 0.315954f, 0.951835f },
    { 0.320125f, 0.951835f },
    { 0.324296f, 0.951835f },
    { 0.328467f, 0.951835f },
    { 0.332638f, 0.951835f },
    { 0.336809f, 0.951835f },
    { 0.340980f, 0.951835f },
    { 0.345151f, 0.951835f },
    { 0.349322f, 0.951835f },
    { 0.353493f, 0.951835f },
    { 0.357664f, 0.951835f },
    { 0.361835f, 0.951835f },
    { 0.366006f, 0.951835f },
    { 0.370177f, 0.951835f },
    { 0.374348f, 0.951835f },
    { 0.378519f, 0.951835f },
    { 0.382690f, 0.951835f },
    { 0.386861f, 0.951835f },
    { 0.391032f, 0.951835f },
    { 0.395203f, 0.951835f },
    { 0.399374f, 0.951835f },
    { 0.403545f, 0.951835f },
    { 0.407716f, 0.951835f },
    { 0.411887f, 0.951835f },
    { 0.416058f, 0.951835f },
    { 0.420229f, 0.951835f },
    { 0.424400f, 0.951835f },
    { 0.428571f, 0.951835f },
    { 0.432742f, 0.951835f },
    { 0.436913f, 0.951835f },
    { 0.441084f, 0.951835f },
    { 0.445255f, 0.951835f },
    { 0.449426f, 0.951835f },
    { 0.453597f, 0.951835f },
    { 0.457769f, 0.951835f },
    { 0.461940f, 0.951835f },
    { 0.466111f, 0.951835f },
    { 0.470282f, 0.951835f },
    { 0.474453f, 0.951835f },
    { 0.478624f, 0.951835f },
    { 0.482795f, 0.951835f },
    { 0.486966f, 0.951835f },
    { 0.491137f, 0.951835f },
    { 0.495308f, 0.951835f },
    { 0.499479f, 0.951835f },
    { 0.503650f, 0.951835f },
    { 0.507821f, 0.951835f },
    { 0.511992f, 0.951835f },
    { 0.516163f, 0.951835f },
    { 0.520334f, 0.951835f },
    { 0.524505f, 0.951835f },
    { 0.528676f, 0.951835f },
    { 0.532847f, 0.951835f },
    { 0.537018f, 0.951835f },
    { 0.541189f, 0.951835f },
    { 0.545360f, 0.951835f },
    { 0.549531f, 0.951835f },
    { 0.553702f, 0.951835f },
    { 0.557873f, 0.951835f },
    { 0.562044f, 0.951835f },
    { 0.566215f, 0.951835f },
    { 0.570386f, 0.951835f },
    { 0.574557f, 0.951835f },
    { 0.578728f, 0.951835f },
    { 0.582899f, 0.951835f },
    { 0.587070f, 0.951835f },
    { 0.591241f, 0.951835f },
    { 0.595412f, 0.951835f },
    { 0.599583f, 0.951835f },
    { 0.603754f, 0.951835f },
    { 0.607925f, 0.951835f },
    { 0.612096f, 0.951835f },
    { 0.616267f, 0.951835f },
    { 0.620438f, 0.951835f },
    { 0.624609f, 0.951835f },
    { 0.628780f, 0.951835f },
    { 0.632951f, 0.951835f },
    { 0.637122f, 0.951835f },
    { 0.641293f, 0.951835f },
    { 0.645464f, 0.951835f },
    { 0.649635f, 0.951835f },
    { 0.653806f, 0.951835f },
    { 0.657977f, 0.951835f },
    { 0.662148f, 0.951835f },
    { 0.666319f, 0.951835f },
    { 0.670490f, 0.951835f },
    { 0.674661f, 0.951835f },
    { 0.678832f, 0.951835f },
    { 0.683003f, 0.951835f },
    { 0.687174f, 0.951835f },
    { 0.691345f, 0.951835f },
    { 0.695516f, 0.951835f },
    { 0.699687f, 0.951835f },
    { 0.703858f, 0.951835f },
    { 0.708029f, 0.951835f },
    { 0.712200f, 0.951835f },
    { 0.716371f, 0.951835f },
    { 0.720542f, 0.951835f },
    { 0.724713f, 0.951835f },
    { 0.728884f, 0.951835f },
    { 0.733055f, 0.951835f },
    { 0.737226f, 0.951835f },
    { 0.741397f, 0.951835f },
    { 0.745568f, 0.951835f },
    { 0.749739f, 0.951835f },
    { 0.753910f, 0.951835f },
    { 0.758081f, 0.951835f },
    { 0.762252f, 0.951835f },
    { 0.766423f, 0.951835f },
    { 0.770594f, 0.951835f },
    { 0.774765f, 0.951835f },
    { 0.778936f, 0.951835f },
    { 0.783107f, 0.951835f },
    { 0.787278f, 0.951835f },
    { 0.791449f, 0.951835f },
    { 0.795620f, 0.951835f },
    { 0.799791f, 0.949541f },
    { 0.803962f, 0.949541f },
    { 0.808133f, 0.947248f },
    { 0.812304f, 0.944954f },
    { 0.816475f, 0.944954f },
    { 0.820647f, 0.942661f },
    { 0.824818f, 0.940367f },
    { 0.828989f, 0.935780f },
    { 0.833160f, 0.933486f },
    { 0.837331f, 0.931193f },
    { 0.841502f, 0.926606f },
    { 0.845673f, 0.924312f },
    { 0.849844f, 0.919725f },
    { 0.854015f, 0.915138f },
    { 0.858186f, 0.910550f },
    { 0.862357f, 0.903670f },
    { 0.866528f, 0.899083f },
    { 0.870699f, 0.894495f },
    { 0.874870f, 0.887615f },
    { 0.879041f, 0.883028f },
    { 0.883212f, 0.876147f },
    { 0.887383f, 0.869266f },
    { 0.891554f, 0.860092f },
    { 0.895725f, 0.850917f },
    { 0.899896f, 0.839450f },
    { 0.904067f, 0.830275f },
    { 0.908238f, 0.821101f },
    { 0.912409f, 0.811927f },
    { 0.916580f, 0.800459f },
    { 0.920751f, 0.788991f },
    { 0.924922f, 0.775229f },
    { 0.929093f, 0.761468f },
    { 0.933264f, 0.743119f },
    { 0.937435f, 0.724771f },
    { 0.941606f, 0.701835f },
    { 0.945777f, 0.672018f },
    { 0.949948f, 0.633028f },
    { 0.954119f, 0.573394f },
};

static const RatioPoint FRONT_TOP_VENTS[] = {
    {0.37f, 0.12f}, {0.39f, 0.12f}, {0.41f, 0.12f}, {0.43f, 0.12f}, {0.45f, 0.12f},
    {0.47f, 0.12f}, {0.49f, 0.12f}, {0.51f, 0.12f}, {0.53f, 0.12f}, {0.55f, 0.12f},
    {0.57f, 0.12f}, {0.59f, 0.12f}, {0.61f, 0.12f}
};

static const RatioPoint FRONT_SPEAKERS[] = {
    {0.18f, 0.50f}, {0.20f, 0.50f}, {0.22f, 0.50f}, {0.24f, 0.50f},
    {0.76f, 0.50f}, {0.78f, 0.50f}, {0.80f, 0.50f}, {0.82f, 0.50f}
};

static const RatioPoint BACK_BODY_OUTLINE[] = {
    {0.030f, 0.28f},
    {0.055f, 0.14f},
    {0.110f, 0.08f},
    {0.220f, 0.04f},
    {0.780f, 0.04f},
    {0.890f, 0.08f},
    {0.945f, 0.14f},
    {0.970f, 0.28f},
    {0.970f, 0.72f},
    {0.945f, 0.86f},
    {0.890f, 0.92f},
    {0.780f, 0.96f},
    {0.220f, 0.96f},
    {0.110f, 0.92f},
    {0.055f, 0.86f},
    {0.030f, 0.72f}
};

static const RatioPoint BACK_CAMERA_STRIP[] = {
    {0.32f, 0.08f}, {0.68f, 0.08f}
};

#define FRONT_BODY_POINT_COUNT (int)(sizeof(FRONT_BODY_TOP) / sizeof(RatioPoint))

static void draw_front_body(DiagramRenderCtx* ctx) {
    for (int i = 0; i < FRONT_BODY_POINT_COUNT - 1; i++) {
        int x1 = RATIO_X(ctx, FRONT_BODY_TOP[i].x);
        int x2 = RATIO_X(ctx, FRONT_BODY_TOP[i + 1].x);
        int top1 = RATIO_Y(ctx, FRONT_BODY_TOP[i].y);
        int top2 = RATIO_Y(ctx, FRONT_BODY_TOP[i + 1].y);
        int bot1 = RATIO_Y(ctx, FRONT_BODY_BOTTOM[i].y);
        int bot2 = RATIO_Y(ctx, FRONT_BODY_BOTTOM[i + 1].y);
        if (x2 < x1) {
            int tmp = x1;
            x1 = x2;
            x2 = tmp;
        }
        if (x2 == x1)
            continue;
        for (int x = x1; x <= x2; x++) {
            float t = (float)(x - x1) / (float)(x2 - x1);
            int top_y = (int)lerp((float)top1, (float)top2, t);
            int bot_y = (int)lerp((float)bot1, (float)bot2, t);
            vita2d_draw_line(x, top_y, x, bot_y, ctx->fill_color);
        }
    }
}

static void draw_front_outline(DiagramRenderCtx* ctx) {
    uint32_t glow = (UI_COLOR_PRIMARY_BLUE & 0xFFFFFF00) | 180;
    for (int i = 0; i < FRONT_BODY_POINT_COUNT - 1; i++) {
        int x1 = RATIO_X(ctx, FRONT_BODY_TOP[i].x);
        int y1 = RATIO_Y(ctx, FRONT_BODY_TOP[i].y);
        int x2 = RATIO_X(ctx, FRONT_BODY_TOP[i + 1].x);
        int y2 = RATIO_Y(ctx, FRONT_BODY_TOP[i + 1].y);
        vita2d_draw_line(x1, y1, x2, y2, glow);
    }
    for (int i = 0; i < FRONT_BODY_POINT_COUNT - 1; i++) {
        int x1 = RATIO_X(ctx, FRONT_BODY_BOTTOM[i].x);
        int y1 = RATIO_Y(ctx, FRONT_BODY_BOTTOM[i].y);
        int x2 = RATIO_X(ctx, FRONT_BODY_BOTTOM[i + 1].x);
        int y2 = RATIO_Y(ctx, FRONT_BODY_BOTTOM[i + 1].y);
        vita2d_draw_line(x1, y1, x2, y2, glow);
    }
    // Connect sides
    vita2d_draw_line(RATIO_X(ctx, FRONT_BODY_TOP[0].x),
                     RATIO_Y(ctx, FRONT_BODY_TOP[0].y),
                     RATIO_X(ctx, FRONT_BODY_BOTTOM[0].x),
                     RATIO_Y(ctx, FRONT_BODY_BOTTOM[0].y), glow);
    vita2d_draw_line(RATIO_X(ctx, FRONT_BODY_TOP[FRONT_BODY_POINT_COUNT - 1].x),
                     RATIO_Y(ctx, FRONT_BODY_TOP[FRONT_BODY_POINT_COUNT - 1].y),
                     RATIO_X(ctx, FRONT_BODY_BOTTOM[FRONT_BODY_POINT_COUNT - 1].x),
                     RATIO_Y(ctx, FRONT_BODY_BOTTOM[FRONT_BODY_POINT_COUNT - 1].y), glow);
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

static void draw_front_touch_grid(DiagramRenderCtx* ctx) {
    int x = RATIO_X(ctx, VITA_SCREEN_X_RATIO);
    int y = RATIO_Y(ctx, VITA_SCREEN_Y_RATIO);
    int w = RATIO_W(ctx, VITA_SCREEN_W_RATIO);
    int h = RATIO_H(ctx, VITA_SCREEN_H_RATIO);
    uint32_t grid_color = RGBA8(80, 90, 105, 140);

    vita2d_draw_line(x + w / 2, y, x + w / 2, y + h, grid_color);
    vita2d_draw_line(x, y + h / 2, x + w, y + h / 2, grid_color);
    vita2d_draw_rectangle(x + w / 5, y + h / 5, (w * 3) / 5, (h * 3) / 5, RGBA8(52, 144, 255, 24));
}

static void draw_front_camera_and_speaker(DiagramRenderCtx* ctx) {
    int body_y = RATIO_Y(ctx, VITA_BODY_Y_RATIO);
    int cam_r = RATIO_SIZE(ctx, 0.012f);
    int cam_x = ctx->base_x + ctx->width / 2;
    int cam_y = body_y - cam_r - 6;
    ui_draw_circle_outline(cam_x, cam_y, cam_r, RGBA8(90, 95, 105, 255));
    vita2d_draw_fill_circle(cam_x, cam_y, cam_r / 2, RGBA8(60, 65, 75, 255));

    int speaker_w = RATIO_W(ctx, 0.22f);
    int speaker_h = RATIO_H(ctx, 0.035f);
    int speaker_x = ctx->base_x + (ctx->width - speaker_w) / 2;
    int speaker_y = cam_y - speaker_h - 6;
    ui_draw_rounded_rect(speaker_x, speaker_y, speaker_w, speaker_h, speaker_h / 2, RGBA8(45, 50, 60, 255));

    int notch_w = speaker_w / 10;
    for (int i = 0; i < 9; i++) {
        int notch_x = speaker_x + 4 + i * notch_w;
        vita2d_draw_rectangle(notch_x, speaker_y + speaker_h / 2 - 2, notch_w / 2, 3, RGBA8(20, 22, 28, 255));
    }
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

    ui_draw_rectangle_outline(cx - arm_len - 2, cy - arm_width / 2 - 2, arm_len * 2 + 4, arm_width + 4, RGBA8(90, 120, 170, 200));
    ui_draw_rectangle_outline(cx - arm_width / 2 - 2, cy - arm_len - 2, arm_width + 4, arm_len * 2 + 4, RGBA8(90, 120, 170, 200));
    ui_draw_circle(cx, cy, arm_width / 3, RGBA8(25, 30, 42, 255));
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
    vita2d_draw_line(tri_x, tri_y - radius / 2, tri_x - radius / 2, tri_y + radius / 2, ctx->outline_color);
    vita2d_draw_line(tri_x - radius / 2, tri_y + radius / 2, tri_x + radius / 2, tri_y + radius / 2, ctx->outline_color);
    vita2d_draw_line(tri_x + radius / 2, tri_y + radius / 2, tri_x, tri_y - radius / 2, ctx->outline_color);

    // Circle (right)
    int cir_x = RATIO_X(ctx, VITA_BTN_CIRCLE_CX_RATIO);
    int cir_y = RATIO_Y(ctx, VITA_BTN_CIRCLE_CY_RATIO);
    ui_draw_circle_outline(cir_x, cir_y, radius, ctx->outline_color);
    ui_draw_circle_outline(cir_x, cir_y, radius - 4, ctx->outline_color_dim);

    // Cross (bottom)
    int cross_x = RATIO_X(ctx, VITA_BTN_CROSS_CX_RATIO);
    int cross_y = RATIO_Y(ctx, VITA_BTN_CROSS_CY_RATIO);
    ui_draw_circle_outline(cross_x, cross_y, radius, ctx->outline_color);
    vita2d_draw_line(cross_x - radius / 2, cross_y - radius / 2, cross_x + radius / 2, cross_y + radius / 2, ctx->outline_color);
    vita2d_draw_line(cross_x - radius / 2, cross_y + radius / 2, cross_x + radius / 2, cross_y - radius / 2, ctx->outline_color);

    // Square (left)
    int sq_x = RATIO_X(ctx, VITA_BTN_SQUARE_CX_RATIO);
    int sq_y = RATIO_Y(ctx, VITA_BTN_SQUARE_CY_RATIO);
    ui_draw_circle_outline(sq_x, sq_y, radius, ctx->outline_color);
    ui_draw_rectangle_outline(sq_x - radius / 2, sq_y - radius / 2, radius, radius, ctx->outline_color);
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
    ui_draw_circle_outline(lstick_x, lstick_y, dot_r + 4, RGBA8(70, 90, 130, 180));

    // Right stick
    int rstick_x = RATIO_X(ctx, VITA_RSTICK_CX_RATIO);
    int rstick_y = RATIO_Y(ctx, VITA_RSTICK_CY_RATIO);
    ui_draw_circle_outline(rstick_x, rstick_y, outer_r, ctx->outline_color);
    ui_draw_circle_outline(rstick_x, rstick_y, inner_r, ctx->outline_color_dim);
    vita2d_draw_fill_circle(rstick_x, rstick_y, dot_r, ctx->outline_color);
    ui_draw_circle_outline(rstick_x, rstick_y, dot_r + 4, RGBA8(70, 90, 130, 180));
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

static void draw_back_touchpad_pattern(DiagramRenderCtx* ctx) {
    int pad_x = RATIO_X(ctx, VITA_RTOUCH_X_RATIO);
    int pad_y = RATIO_Y(ctx, VITA_RTOUCH_Y_RATIO);
    int pad_w = RATIO_W(ctx, VITA_RTOUCH_W_RATIO);
    int pad_h = RATIO_H(ctx, VITA_RTOUCH_H_RATIO);
    uint32_t color = RGBA8(60, 90, 130, 255);

    int cols = 8;
    int rows = 5;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int px = pad_x + (c + 1) * pad_w / (cols + 1);
            int py = pad_y + (r + 1) * pad_h / (rows + 1);
            int size = 4;
            float rx = (float)(px - ctx->base_x) / (float)ctx->width;
            float ry = (float)(py - ctx->base_y) / (float)ctx->height;
            switch ((r + c) % 4) {
                case 0:
                    draw_ratio_circle_outline(ctx, rx, ry, 0.010f, color);
                    break;
                case 1:
                    draw_symbol_square(px, py, size, RGBA8(45, 55, 70, 220));
                    break;
                case 2:
                    draw_symbol_triangle(px, py, size, color);
                    break;
                default:
                    draw_symbol_cross(px, py, size, color);
                    break;
            }
        }
    }
}

static void draw_back_grips(DiagramRenderCtx* ctx) {
    int body_x = RATIO_X(ctx, VITA_BODY_X_RATIO);
    int body_y = RATIO_Y(ctx, VITA_BODY_Y_RATIO);
    int body_w = RATIO_W(ctx, VITA_BODY_W_RATIO);
    int grip_w = RATIO_W(ctx, 0.22f);
    int grip_h = RATIO_H(ctx, 0.55f);
    int grip_y = body_y + RATIO_H(ctx, 0.20f);
    uint32_t grip_color = RGBA8(32, 35, 42, 255);

    ui_draw_rounded_rect(body_x + RATIO_W(ctx, 0.04f), grip_y, grip_w, grip_h, grip_h / 2, grip_color);
    ui_draw_rounded_rect(body_x + body_w - grip_w - RATIO_W(ctx, 0.04f), grip_y, grip_w, grip_h, grip_h / 2, grip_color);
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

static void draw_back_screws(DiagramRenderCtx* ctx) {
    int left_x = ctx->base_x + RATIO_W(ctx, 0.08f);
    int right_x = ctx->base_x + ctx->width - RATIO_W(ctx, 0.08f);
    int top_y = ctx->base_y + RATIO_H(ctx, 0.18f);
    int bottom_y = ctx->base_y + ctx->height - RATIO_H(ctx, 0.18f);
    uint32_t screw_color = RGBA8(90, 95, 105, 255);

    int positions[4][2] = {
        { left_x, top_y },
        { right_x, top_y },
        { left_x, bottom_y },
        { right_x, bottom_y }
    };

    for (int i = 0; i < 4; i++) {
        int cx = positions[i][0];
        int cy = positions[i][1];
        ui_draw_circle(cx, cy, 6, RGBA8(25, 28, 34, 255));
        ui_draw_circle_outline(cx, cy, 6, screw_color);
        vita2d_draw_line(cx - 3, cy, cx + 3, cy, screw_color);
        vita2d_draw_line(cx, cy - 3, cx, cy + 3, screw_color);
    }
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

static void draw_zone_highlight_rect(int x, int y, int w, int h, float pulse) {
    uint8_t alpha = (uint8_t)(150 + 105 * pulse);
    uint32_t glow_color = (UI_COLOR_PRIMARY_BLUE & 0xFFFFFF00) | alpha;
    vita2d_draw_rectangle(x, y, w, h, RGBA8(52, 144, 255, alpha / 3));
    ui_draw_rectangle_outline(x, y, w, h, glow_color);
    ui_draw_rectangle_outline(x + 3, y + 3, w - 6, h - 6, (UI_COLOR_PRIMARY_BLUE & 0xFFFFFF00) | 100);
}

void ui_diagram_draw_front_zone_highlight(DiagramRenderCtx* ctx, VitakiCtrlIn input, float pulse) {
    int zone_x, zone_y, zone_w, zone_h;
    if (!ui_diagram_front_zone_rect(ctx, input, &zone_x, &zone_y, &zone_w, &zone_h))
        return;
    draw_zone_highlight_rect(zone_x, zone_y, zone_w, zone_h, pulse);
}

void ui_diagram_draw_back_slot_highlight(DiagramRenderCtx* ctx, VitakiCtrlIn input, float pulse) {
    int zone_x, zone_y, zone_w, zone_h;
    if (!ui_diagram_back_zone_rect(ctx, input, &zone_x, &zone_y, &zone_w, &zone_h))
        return;
    draw_zone_highlight_rect(zone_x, zone_y, zone_w, zone_h, pulse);
}

// ============================================================================
// Summary View Callouts
// ============================================================================

/**
 * Draw mapping callouts for Summary view
 * Shows inline labels like "△ → □" with arrows pointing to controls
 */
static void draw_summary_callouts(DiagramState* state, DiagramRenderCtx* ctx, const VitakiCtrlMapInfo* map) {
    if (!map || state->callout_page < 0 || state->callout_page >= CTRL_CALLOUT_PAGE_COUNT) {
        return;
    }

    const DiagramCalloutPage* page = &g_callout_pages[state->callout_page];
    float pulse_alpha = 0.75f + 0.25f * sinf(state->highlight_pulse * 2.0f * (float)M_PI);
    uint32_t line_color = (UI_COLOR_PRIMARY_BLUE & 0xFFFFFF00) | (uint32_t)(pulse_alpha * 255.0f);
    uint32_t pill_bg = RGBA8(40, 45, 52, 230);

    char page_text[48];
    snprintf(page_text, sizeof(page_text), "Page %d/%d · %s",
             state->callout_page + 1, state->callout_page_count, page->title);
    int label_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, page_text);
    int label_x = ctx->base_x + (ctx->width - label_w) / 2;
    vita2d_font_draw_text(font, label_x, ctx->base_y - 12, UI_COLOR_TEXT_TERTIARY,
                          FONT_SIZE_SMALL, page_text);

    for (int i = 0; i < page->count; i++) {
        const DiagramCalloutDef* def = &g_callouts[page->start + i];
        if (def->view != state->mode) {
            continue;
        }

        int anchor_x, anchor_y;
        if (!callout_anchor_for_input(ctx, def->input, &anchor_x, &anchor_y)) {
            anchor_x = RATIO_X(ctx, def->anchor_rx);
            anchor_y = RATIO_Y(ctx, def->anchor_ry);
        }
        int label_x_ratio = ctx->base_x + (int)(ctx->width * def->label_rx);
        int label_y_ratio = ctx->base_y + (int)(ctx->height * def->label_ry);

        VitakiCtrlOut mapped = controller_map_get_output_for_input(map, def->input);
        const char* mapped_text = controller_output_symbol(mapped);
        char pill_text[32];
        snprintf(pill_text, sizeof(pill_text), "%s", mapped_text);

        uint32_t text_color = (mapped == VITAKI_CTRL_OUT_NONE)
            ? UI_COLOR_TEXT_TERTIARY : UI_COLOR_TEXT_PRIMARY;
        draw_anchor_label(def->label, anchor_x, anchor_y, UI_COLOR_TEXT_SECONDARY);
        draw_callout_arrow(anchor_x, anchor_y,
                           label_x_ratio + (int)(CALLOUT_PILL_PADDING * 1.5f),
                           label_y_ratio + CALLOUT_PILL_HEIGHT / 2, line_color);
        draw_callout_pill(label_x_ratio, label_y_ratio, pill_text, pill_bg, text_color);
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
    // Layer 1: Body fill using traced outline
    draw_front_body(ctx);

    // Layer 2: Screen area
    draw_front_screen(ctx);
    draw_front_touch_grid(ctx);

    // Layer 3: Shoulder buttons
    draw_front_shoulders(ctx);

    // Layer 4: Body outline (on top of fills, under controls)
    draw_front_camera_and_speaker(ctx);
    draw_front_outline(ctx);
    draw_ratio_speckles(ctx, FRONT_TOP_VENTS, CTRL_ARRAY_SIZE(FRONT_TOP_VENTS), RGBA8(90, 95, 105, 230), 6);
    draw_ratio_speckles(ctx, FRONT_SPEAKERS, CTRL_ARRAY_SIZE(FRONT_SPEAKERS), RGBA8(60, 70, 82, 230), 5);

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
    draw_back_grips(ctx);

    // Layer 2-3: Touchpad with zones
    draw_back_touchpad(ctx);
    draw_back_touchpad_pattern(ctx);

    // Layer 4: Body outline
    draw_front_body_outline(ctx);
    draw_ratio_polyline(ctx, BACK_BODY_OUTLINE, CTRL_ARRAY_SIZE(BACK_BODY_OUTLINE), RGBA8(85, 120, 150, 200), true);
    draw_ratio_polyline(ctx, BACK_BODY_OUTLINE, CTRL_ARRAY_SIZE(BACK_BODY_OUTLINE), RGBA8(8, 10, 14, 255), true);

    // Layer 5: Camera decoration
    draw_back_camera(ctx);
    draw_ratio_polyline(ctx, BACK_CAMERA_STRIP, CTRL_ARRAY_SIZE(BACK_CAMERA_STRIP), RGBA8(70, 80, 92, 255), false);
    draw_back_screws(ctx);
}

/**
 * Main diagram render function (delegates to procedural rendering)
 */
void ui_diagram_render(DiagramState* state, const VitakiCtrlMapInfo* map, int x, int y, int w, int h) {
    // Initialize render context
    DiagramRenderCtx ctx = {0};
    ui_diagram_init_context(&ctx, x, y, w, h);

    bool skip_background_card = false;
    if ((state->mode == CTRL_VIEW_FRONT && state->texture_front) ||
        (state->mode == CTRL_VIEW_BACK && state->texture_back)) {
        skip_background_card = true;
    }
    if (!skip_background_card) {
        ui_draw_card_with_shadow(x, y, w, h, 8, UI_COLOR_CARD_BG);
    }

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

    bool front_texture_drawn = false;

    // Draw appropriate view
    bool back_texture_drawn = false;

    if (state->mode == CTRL_VIEW_BACK) {
        if (state->texture_back) {
            draw_back_texture(&ctx, state->texture_back);
            back_texture_drawn = true;
        }
        if (!back_texture_drawn) {
            ui_diagram_draw_back(&ctx);
        }
    } else {
        if (state->texture_front) {
            draw_front_texture(&ctx, state->texture_front);
            front_texture_drawn = true;
        }
        if (!front_texture_drawn) {
            ui_diagram_draw_front(&ctx);
        }
    }

    if (state->mode == CTRL_VIEW_FRONT &&
        (state->detail_view == CTRL_DETAIL_SUMMARY || state->detail_view == CTRL_DETAIL_FRONT_MAPPING)) {
        const bool* selection = (state->detail_view == CTRL_DETAIL_FRONT_MAPPING) ? state->front_selection : NULL;
        draw_front_touch_overlay(&ctx, map, selection);
    } else if (state->mode == CTRL_VIEW_BACK &&
               (state->detail_view == CTRL_DETAIL_SUMMARY || state->detail_view == CTRL_DETAIL_BACK_MAPPING)) {
        const bool* selection = (state->detail_view == CTRL_DETAIL_BACK_MAPPING) ? state->back_selection : NULL;
        draw_back_touch_overlay(&ctx, map, selection);
    }

    // Draw overlays based on detail view
    if (state->detail_view == CTRL_DETAIL_SUMMARY) {
        draw_summary_callouts(state, &ctx, map);
    } else if (state->detail_view == CTRL_DETAIL_FRONT_MAPPING) {
        if (state->selected_button >= 0) {
            ui_diagram_draw_front_zone_highlight(&ctx, (VitakiCtrlIn)state->selected_button,
                                                 sinf(state->highlight_pulse * 2.0f * (float)M_PI));
        }
    } else if (state->detail_view == CTRL_DETAIL_BACK_MAPPING) {
        if (state->selected_zone >= 0) {
            ui_diagram_draw_back_slot_highlight(&ctx, (VitakiCtrlIn)state->selected_zone,
                                                sinf(state->highlight_pulse * 2.0f * (float)M_PI));
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
    state->callout_page = 0;
    state->callout_page_count = CTRL_CALLOUT_PAGE_COUNT;
    state->highlight_pulse = 0.0f;
    state->flip_animation = 0.0f;
    state->color_tween = 0.0f;
    state->animation_start_us = 0;
    state->flip_in_progress = false;
    state->color_tween_active = false;
    state->texture_front = vita2d_load_PNG_file(CONTROLLER_FRONT_TEXTURE_PATH);
    sanitize_outline_texture(state->texture_front, FRONT_TEXTURE_ALPHA_THRESHOLD);
    state->texture_back = vita2d_load_PNG_file(CONTROLLER_BACK_TEXTURE_PATH);
    sanitize_outline_texture(state->texture_back, BACK_TEXTURE_ALPHA_THRESHOLD);
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
