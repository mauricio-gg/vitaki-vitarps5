/**
 * @file ui_graphics.c
 * @brief Low-level drawing primitives implementation for VitaRPS5
 *
 * This module implements the foundational drawing functions used throughout
 * the UI system. All functions are optimized for PS Vita hardware constraints.
 *
 * Performance notes:
 * - draw_rounded_rectangle uses O(radius) instead of O(radius^2) algorithm
 * - draw_circle includes bounds checking to prevent expensive off-screen draws
 * - All functions use vita2d for hardware-accelerated rendering where possible
 */

// Include context.h BEFORE ui_internal.h to avoid circular dependency issues
// context.h -> ui.h has duplicate definitions with ui_types.h (included by ui_internal.h)
// Including context.h first ensures ui.h types take precedence
#include "context.h"

#include "ui/ui_graphics.h"
#include "ui/ui_internal.h"

#include <math.h>
#include <sys/param.h>  // For MIN macro

// ============================================================================
// Primitive Shape Drawing
// ============================================================================

/**
 * Draw a rounded rectangle
 *
 * Efficiently renders a rectangle with rounded corners using:
 * 1. Two center cross rectangles (horizontal and vertical)
 * 2. Row-by-row corner filling using circle equation
 *
 * This approach uses O(radius) draw calls instead of O(radius^2) pixel loops.
 */
void ui_draw_rounded_rect(int x, int y, int width, int height, int radius, uint32_t color) {
  // Fast path: no rounding needed
  if (radius <= 0) {
    vita2d_draw_rectangle(x, y, width, height, color);
    return;
  }

  // Clamp radius to prevent oversized corners
  int max_radius = MIN(width, height) / 2;
  if (radius > max_radius)
    radius = max_radius;

  // Main rectangle body (center cross)
  int body_w = width - 2 * radius;
  int body_h = height - 2 * radius;
  if (body_w > 0)
    vita2d_draw_rectangle(x + radius, y, body_w, height, color);
  if (body_h > 0)
    vita2d_draw_rectangle(x, y + radius, width, body_h, color);

  // Draw curved corners row-by-row (O(radius) draw calls instead of O(radius^2))
  const int radius_sq = radius * radius;
  for (int dy = 0; dy < radius; ++dy) {
    int dist = radius - dy;
    int inside = radius_sq - dist * dist;
    if (inside <= 0)
      continue;
    int dx = (int)ceilf(sqrtf((float)inside));
    if (dx <= 0)
      continue;

    int top_y = y + dy;
    int bottom_y = y + height - dy - 1;
    int left_start = x + radius - dx;
    int right_start = x + width - radius;

    vita2d_draw_rectangle(left_start, top_y, dx, 1, color);
    vita2d_draw_rectangle(right_start, top_y, dx, 1, color);
    vita2d_draw_rectangle(left_start, bottom_y, dx, 1, color);
    vita2d_draw_rectangle(right_start, bottom_y, dx, 1, color);
  }
}

/**
 * Draw a card with drop shadow
 *
 * Renders shadow first (4px offset), then card on top for proper layering.
 */
void ui_draw_card_with_shadow(int x, int y, int width, int height, int radius, uint32_t color) {
  // Render shadow first (offset by UI_SHADOW_OFFSET_PX pixels)
  uint32_t shadow_color = UI_COLOR_SHADOW;
  ui_draw_rounded_rect(x + UI_SHADOW_OFFSET_PX, y + UI_SHADOW_OFFSET_PX, width, height, radius, shadow_color);

  // Render the actual card on top
  ui_draw_rounded_rect(x, y, width, height, radius, color);
}

/**
 * Draw a filled circle
 *
 * Uses pixel-by-pixel rasterization with circle equation: x^2 + y^2 <= r^2
 * Includes extensive bounds checking and color validation.
 */
void ui_draw_circle(int cx, int cy, int radius, uint32_t color) {
  // Bounds checking - prevent expensive off-screen rendering
  if (cx < -UI_OFFSCREEN_MARGIN || cx > VITA_WIDTH + UI_OFFSCREEN_MARGIN ||
      cy < -UI_OFFSCREEN_MARGIN || cy > VITA_HEIGHT + UI_OFFSCREEN_MARGIN ||
      radius <= 0 || radius > 1000) {
    return;
  }

  // Fix problematic color values (vita2d rendering issue workaround)
  if (color == 0xFFFFFFFF) {
    color = RGBA8(254, 254, 254, 255);
  }

  // Ensure alpha channel is set
  if ((color & 0xFF000000) == 0) {
    color |= 0xFF000000;
  }

  // Rasterize circle using bounding box
  for (int y = -radius; y <= radius; y++) {
    for (int x = -radius; x <= radius; x++) {
      if (x * x + y * y <= radius * radius) {
        int draw_x = cx + x;
        int draw_y = cy + y;

        // Clip to screen bounds
        if (draw_x < 0 || draw_x >= 960 || draw_y < 0 || draw_y >= 544) {
          continue;
        }

        vita2d_draw_rectangle(draw_x, draw_y, 1, 1, color);
      }
    }
  }
}

/**
 * Draw a circle outline
 *
 * Uses 48 line segments for smooth circular appearance.
 */
void ui_draw_circle_outline(int cx, int cy, int radius, uint32_t color) {
  float step = (2.0f * M_PI) / (float)UI_CIRCLE_OUTLINE_SEGMENTS;
  for (int i = 0; i < UI_CIRCLE_OUTLINE_SEGMENTS; i++) {
    float angle1 = i * step;
    float angle2 = (i + 1) * step;
    float x1 = cx + cosf(angle1) * radius;
    float y1 = cy + sinf(angle1) * radius;
    float x2 = cx + cosf(angle2) * radius;
    float y2 = cy + sinf(angle2) * radius;
    vita2d_draw_line(x1, y1, x2, y2, color);
  }
}

/**
 * Draw a rotating spinner arc
 *
 * Renders a 3/4 circle (270 degrees) with thickness for loading indicators.
 * Increment rotation_deg each frame for animation effect.
 */
void ui_draw_spinner(int cx, int cy, int radius, int thickness, float rotation_deg, uint32_t color) {
  // Draw a circular arc that rotates continuously
  // We'll draw 3/4 of a circle (270 degrees) that rotates around
  float arc_length = 270.0f;  // 3/4 circle in degrees
  int arc_segments = UI_SPINNER_SEGMENTS * 3 / 4;  // 3/4 of total segments

  for (int i = 0; i < arc_segments; i++) {
    float angle1 = rotation_deg + (i * arc_length / arc_segments);
    float angle2 = rotation_deg + ((i + 1) * arc_length / arc_segments);

    // Convert to radians
    float rad1 = angle1 * (float)M_PI / 180.0f;
    float rad2 = angle2 * (float)M_PI / 180.0f;

    // Draw outer arc segment
    int x1_outer = cx + (int)(cos(rad1) * radius);
    int y1_outer = cy + (int)(sin(rad1) * radius);
    int x2_outer = cx + (int)(cos(rad2) * radius);
    int y2_outer = cy + (int)(sin(rad2) * radius);

    // Draw inner arc segment (for thickness)
    int x1_inner = cx + (int)(cos(rad1) * (radius - thickness));
    int y1_inner = cy + (int)(sin(rad1) * (radius - thickness));
    int x2_inner = cx + (int)(cos(rad2) * (radius - thickness));
    int y2_inner = cy + (int)(sin(rad2) * (radius - thickness));

    // Draw lines to create filled arc segment
    vita2d_draw_line(x1_outer, y1_outer, x2_outer, y2_outer, color);
    vita2d_draw_line(x1_inner, y1_inner, x2_inner, y2_inner, color);

    // Fill between inner and outer arcs
    vita2d_draw_line(x1_outer, y1_outer, x1_inner, y1_inner, color);
  }
}

// ============================================================================
// Overlay & Effect Drawing
// ============================================================================

/**
 * Render semi-transparent focus overlay
 *
 * Dims content area when navigation sidebar is expanded.
 */
void ui_draw_content_focus_overlay(void) {
  if (nav_collapse.state != NAV_STATE_EXPANDED) {
    return;
  }
  vita2d_draw_rectangle(0, 0, VITA_WIDTH, VITA_HEIGHT,
                        RGBA8(0, 0, 0, 80));
}

/**
 * Render network loss indicator badge
 *
 * Shows "Network Unstable" badge with red dot in bottom-right corner.
 * Fades out proportionally to remaining alert time.
 */
void ui_draw_loss_indicator(void) {
  // Don't show if currently streaming
  if (context.stream.is_streaming)
    return;

  // Don't show if indicator disabled in config
  if (!context.config.show_network_indicator)
    return;

  // Get current time
  uint64_t now_us = sceKernelGetProcessTimeWide();

  // Check if alert is active
  if (!context.stream.loss_alert_until_us ||
      now_us >= context.stream.loss_alert_until_us)
    return;

  // Calculate fade-out alpha based on remaining time
  uint64_t duration = context.stream.loss_alert_duration_us ?
      context.stream.loss_alert_duration_us : VIDEO_LOSS_ALERT_DEFAULT_US;
  if (!duration)
    duration = VIDEO_LOSS_ALERT_DEFAULT_US;
  uint64_t remaining = context.stream.loss_alert_until_us - now_us;
  float alpha_ratio = (float)remaining / (float)duration;
  if (alpha_ratio < 0.0f)
    alpha_ratio = 0.0f;
  uint8_t alpha = (uint8_t)(alpha_ratio * 255.0f);

  // Badge dimensions using constants
  const int dot_radius = 6;
  const char *headline = "Network Unstable";
  int text_width = vita2d_font_text_width(font, FONT_SIZE_SMALL, headline);
  int box_w = UI_LOSS_INDICATOR_PADDING_X * 2 + dot_radius * 2 + 10 + text_width;
  int box_h = UI_LOSS_INDICATOR_PADDING_Y * 2 + FONT_SIZE_SMALL + 4;
  int box_x = VITA_WIDTH - box_w - UI_LOSS_INDICATOR_MARGIN;
  int box_y = VITA_HEIGHT - box_h - UI_LOSS_INDICATOR_MARGIN;

  // Draw background pill with fade
  uint8_t bg_alpha = (uint8_t)(alpha_ratio * 200.0f);
  if (bg_alpha < 40)
    bg_alpha = 40;
  ui_draw_rounded_rect(box_x, box_y, box_w, box_h, box_h / 2,
                         RGBA8(0, 0, 0, bg_alpha));

  // Draw red status dot
  int dot_x = box_x + UI_LOSS_INDICATOR_PADDING_X;
  int dot_y = box_y + box_h / 2;
  vita2d_draw_fill_circle(dot_x, dot_y, dot_radius,
                          RGBA8(0xF4, 0x43, 0x36, alpha));

  // Draw text label
  int text_x = dot_x + dot_radius + 10;
  int text_y = box_y + box_h / 2 + (FONT_SIZE_SMALL / 2) - 2;
  vita2d_font_draw_text(font, text_x, text_y,
                        RGBA8(0xFF, 0xFF, 0xFF, alpha),
                        FONT_SIZE_SMALL,
                        headline);
}
