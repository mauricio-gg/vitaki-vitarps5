/**
 * @file ui_graphics.h
 * @brief Low-level drawing primitives for VitaRPS5 UI
 *
 * This module provides the foundational drawing functions used throughout
 * the UI system. All functions use vita2d for rendering and follow the
 * ABGR color format convention.
 *
 * Functions are optimized for PS Vita constraints (960x544, limited CPU/GPU).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Primitive Shape Drawing
// ============================================================================

/**
 * Draw a rounded rectangle
 *
 * Efficiently draws a rectangle with rounded corners using O(radius) draw
 * calls instead of O(radius^2). Uses a cross-shaped body with row-by-row
 * corner filling for optimal performance.
 *
 * @param x Left edge X coordinate
 * @param y Top edge Y coordinate
 * @param width Rectangle width in pixels
 * @param height Rectangle height in pixels
 * @param radius Corner radius in pixels (clamped to min(width, height)/2)
 * @param color ABGR color value
 *
 * @note If radius <= 0, falls back to standard rectangle
 * @note Radius is automatically clamped if too large for dimensions
 */
void ui_draw_rounded_rect(int x, int y, int width, int height, int radius, uint32_t color);

/**
 * Draw a card with drop shadow
 *
 * Renders a rounded rectangle with a subtle drop shadow offset by 4 pixels.
 * Shadow uses UI_COLOR_SHADOW (semi-transparent black).
 *
 * @param x Left edge X coordinate (before shadow offset)
 * @param y Top edge Y coordinate (before shadow offset)
 * @param width Card width in pixels
 * @param height Card height in pixels
 * @param radius Corner radius in pixels
 * @param color ABGR color for the card body
 *
 * @note Shadow is drawn first, then card is drawn on top
 * @note Total visual size is (width+4, height+4) due to shadow offset
 */
void ui_draw_card_with_shadow(int x, int y, int width, int height, int radius, uint32_t color);

/**
 * Draw a filled circle
 *
 * Rasterizes a filled circle using pixel-by-pixel drawing. Includes bounds
 * checking and color validation to prevent rendering issues.
 *
 * @param cx Center X coordinate
 * @param cy Center Y coordinate
 * @param radius Circle radius in pixels
 * @param color ABGR color value
 *
 * @note Performs bounds checking: cx [-100,1060], cy [-100,644], radius [1,1000]
 * @note Fixes problematic color values (0xFFFFFFFF -> 0xFFFEFEFE)
 * @note Ensures alpha channel is set if missing
 * @note Clips pixels outside screen bounds (0-960, 0-544)
 */
void ui_draw_circle(int cx, int cy, int radius, uint32_t color);

/**
 * Draw a circle outline
 *
 * Draws a circle outline using 48 line segments for smooth appearance.
 * Uses sine/cosine lookup for segment generation.
 *
 * @param cx Center X coordinate
 * @param cy Center Y coordinate
 * @param radius Circle radius in pixels
 * @param color ABGR color value
 *
 * @note Uses 48 segments for smooth visual quality
 * @note No fill - outline only
 */
void ui_draw_circle_outline(int cx, int cy, int radius, uint32_t color);

/**
 * Draw a rectangle outline
 *
 * Draws a 1-pixel outline around the specified rectangle area.
 * Useful for borders on controller diagrams, touchpad areas, etc.
 *
 * @param x Left edge X coordinate
 * @param y Top edge Y coordinate
 * @param width Rectangle width in pixels
 * @param height Rectangle height in pixels
 * @param color ABGR color value
 *
 * @note Draws four 1-pixel lines for top, bottom, left, right edges
 * @note No fill - outline only
 */
void ui_draw_rectangle_outline(int x, int y, int width, int height, uint32_t color);
void ui_draw_vertical_gradient_rect(int x, int y, int width, int height,
                                    uint32_t top_color, uint32_t bottom_color,
                                    int radius);

/**
 * Draw a rotating spinner arc
 *
 * Renders a 3/4 circle arc (270 degrees) used for loading indicators.
 * Arc rotates continuously based on rotation_deg parameter.
 *
 * @param cx Center X coordinate
 * @param cy Center Y coordinate
 * @param radius Outer radius of the arc
 * @param thickness Arc thickness in pixels
 * @param rotation_deg Current rotation angle in degrees
 * @param color ABGR color value
 *
 * @note Uses 32 segments for smooth arc rendering
 * @note Draws both inner and outer arcs with connecting lines for fill
 * @note Update rotation_deg each frame for animation effect
 */
void ui_draw_spinner(int cx, int cy, int radius, int thickness, float rotation_deg, uint32_t color);

// ============================================================================
// Overlay & Effect Drawing
// ============================================================================

/**
 * Render semi-transparent focus overlay
 *
 * Draws a full-screen semi-transparent black overlay (80 alpha) used when
 * the navigation sidebar is expanded to dim the content area.
 *
 * @note Only renders when navigation is in NAV_STATE_EXPANDED
 * @note Uses 960x544 full screen dimensions
 * @note Alpha value: RGBA8(0, 0, 0, 80)
 */
void ui_draw_content_focus_overlay(void);

/**
 * Render network loss indicator badge
 *
 * Displays a small badge in the bottom-right corner showing "Network Unstable"
 * with a red dot. Badge fades out based on alert duration.
 *
 * @note Only renders when:
 *   - Not currently streaming (context.stream.is_streaming == false)
 *   - Network indicator enabled (context.config.show_network_indicator == true)
 *   - Alert timer active (context.stream.loss_alert_until_us set)
 * @note Fades out proportionally to remaining alert time
 * @note Badge includes: red dot (6px radius) + "Network Unstable" text (FONT_SIZE_SMALL)
 * @note Positioned 18px from bottom-right corner
 */
void ui_draw_loss_indicator(void);

#ifdef __cplusplus
}
#endif
