/**
 * @file ui_constants.h
 * @brief Centralized UI constants for VitaRPS5
 *
 * All layout dimensions, colors, animation timings, and magic numbers
 * are defined here to maintain consistency across UI modules.
 *
 * Color format: ABGR (vita2d native format)
 * Use RGBA8(r,g,b,a) macro for conversion from standard RGBA
 */

#pragma once

// ============================================================================
// Screen Dimensions
// ============================================================================
#define VITA_WIDTH 960
#define VITA_HEIGHT 544

// Touch panel dimensions (native resolution)
#define VITA_TOUCH_PANEL_WIDTH 1920
#define VITA_TOUCH_PANEL_HEIGHT 1088

// ============================================================================
// Legacy Colors (kept for compatibility)
// ============================================================================
#define COLOR_WHITE RGBA8(255, 255, 255, 255)
#define COLOR_GRAY50 RGBA8(129, 129, 129, 255)
#define COLOR_BLACK RGBA8(0, 0, 0, 255)
#define COLOR_ACTIVE RGBA8(255, 170, 238, 255)
#define COLOR_TILE_BG RGBA8(51, 51, 51, 255)
#define COLOR_BANNER RGBA8(22, 45, 80, 255)

// ============================================================================
// Modern VitaRPS5 Colors (ABGR format for vita2d)
// ============================================================================
#define UI_COLOR_PRIMARY_BLUE 0xFFFF9034        // PlayStation Blue #3490FF
#define UI_COLOR_BACKGROUND 0xFF1A1614          // Animated charcoal gradient base
#define UI_COLOR_CARD_BG 0xFF37322D             // Dark charcoal (45,50,55)
#define UI_COLOR_TEXT_PRIMARY 0xFFFAFAFA        // Off-white (reduced eye strain)
#define UI_COLOR_TEXT_SECONDARY 0xFFB4B4B4      // Light Gray
#define UI_COLOR_TEXT_TERTIARY 0xFFA0A0A0       // Medium Gray
#define UI_COLOR_STATUS_AVAILABLE 0xFF50AF4C    // Success Green #4CAF50
#define UI_COLOR_STATUS_CONNECTING 0xFF0098FF   // Warning Orange #FF9800
#define UI_COLOR_STATUS_UNAVAILABLE 0xFF3643F4  // Error Red #F44336
#define UI_COLOR_ACCENT_PURPLE 0xFFB0279C       // Accent Purple #9C27B0
#define UI_COLOR_SHADOW 0x3C000000              // Semi-transparent black for shadows

// ============================================================================
// Typography
// ============================================================================
#define FONT_SIZE_HEADER 28     // Screen titles, primary headers (increased for clarity)
#define FONT_SIZE_SUBHEADER 18  // Section titles, tab labels
#define FONT_SIZE_BODY 16       // Paragraph/body text
#define FONT_SIZE_SMALL 14      // Secondary text, hints, status badges

// Context-scoped sizes (used on specific hot-path surfaces):
#define FONT_SIZE_CARD_TITLE 20   // Console card name text (home screen card name-bar)
#define FONT_SIZE_HOME_HEADER 24  // Home-screen section prompt ("Which do you want to connect?")
#define FONT_SIZE_PIN_DIGIT 40    // PIN-entry digit glyphs (oversized for legibility)

// ============================================================================
// Page Layout
// ============================================================================
#define CONTENT_START_Y 80  // Unified page content start Y

// ============================================================================
// Console Cards (updated per UI spec)
// ============================================================================
#define CONSOLE_CARD_WIDTH 200    // Reverted to original 200px
#define CONSOLE_CARD_HEIGHT 205   // +5px bottom margin
#define CONSOLE_CARD_SPACING 100  // Horizontal spacing
#define CONSOLE_CARD_START_Y 150
#define CONSOLE_CARD_FOCUS_SCALE_MIN 0.95f
#define CONSOLE_CARD_FOCUS_SCALE_MAX 1.0f
#define CONSOLE_CARD_FOCUS_DURATION_MS 180
#define CONSOLE_CARD_GLOW_COLOR 0xFFFF9034  // #3490FF in ABGR

// Horizontal card carousel
#define CARDS_VISIBLE_MAX 4      // Max cards visible at once
#define CARD_H_GAP 20            // Horizontal gap between cards
#define CARD_SCROLL_ANIM_MS 200  // Scroll animation duration (ms)

// Card typography
#define CARD_LOGO_MAX_WIDTH 120         // PS5 logo max width
#define CARD_LOGO_TOP_PADDING 20        // 20px from top of card
#define CARD_NAME_BAR_BOTTOM_OFFSET 80  // Distance from bottom
#define CARD_TEXT_BASELINE_OFFSET 7     // Vertical baseline adjustment

// ============================================================================
// Card Cache
// ============================================================================
#define CARD_CACHE_UPDATE_INTERVAL_US (10 * 1000000)  // 10 seconds

// ============================================================================
// Text Width Cache
// ============================================================================
#define TEXT_WIDTH_CACHE_SIZE 16

// ============================================================================
// Tooltip
// ============================================================================
#define MAX_TOOLTIP_CHARS 200

// ============================================================================
// Legacy Layout (will be phased out)
// ============================================================================
#define HEADER_BAR_X 136
#define HEADER_BAR_Y 43
#define HEADER_BAR_H 26
#define HEADER_BAR_W 774
#define HOST_SLOTS_X (HEADER_BAR_X - 86)
#define HOST_SLOTS_Y (HEADER_BAR_Y + HEADER_BAR_H + 43)
#define HOST_SLOT_W 400
#define HOST_SLOT_H 190

// ============================================================================
// Texture Paths
// ============================================================================
#define TEXTURE_PATH "app0:/assets/"
#define IMG_PS4_PATH TEXTURE_PATH "ps4.png"

// ============================================================================
// Debug Menu
// ============================================================================
#ifndef VITARPS5_DEBUG_MENU
#define VITARPS5_DEBUG_MENU 0
#endif

// ============================================================================
// Graphics Primitives (ui_graphics.c)
// ============================================================================
#define UI_SHADOW_OFFSET_PX 4  // Drop shadow offset in pixels
#define UI_CIRCLE_OUTLINE_SEGMENTS \
  16  // Segments for circle outlines (reduced from 48 for PS Vita GPU performance)
#define UI_OFFSCREEN_MARGIN 100  // Margin for offscreen culling

// ============================================================================
// Controller Layout (old screen constants)
// ============================================================================
#define CTRL_DIAGRAM_WIDTH 500  // Controller diagram width (fits in 830px content area)
#define CTRL_DIAGRAM_HEIGHT \
  228  // Controller diagram height (2.2:1 aspect ratio for Vita landscape)
#define CTRL_DIAGRAM_Y (CONTENT_START_Y + 50)  // Below title
#define CTRL_LEGEND_WIDTH 250                  // Legend panel width
#define CTRL_LEGEND_Y CTRL_DIAGRAM_Y           // Aligned with diagram
#define CTRL_PRESET_COUNT 3                    // Number of controller presets (Custom 1, 2, 3)

// Controller diagram colors (PlayStation Blue tint for PNG outlines)
#define CTRL_OUTLINE_COLOR RGBA8(0, 212, 170, 255)            // Teal/cyan #00D4AA
#define CTRL_OUTLINE_COLOR_DIM RGBA8(0, 140, 112, 255)        // Dimmer teal
#define CTRL_OUTLINE_COLOR_HIGHLIGHT RGBA8(0, 212, 170, 180)  // Semi-transparent
#define CTRL_PNG_TINT_COLOR \
  0xFFFF9034  // PlayStation Blue #3490FF in ABGR format (RGB 52, 144, 255)

// ============================================================================
// Procedural Controller Diagram - Ratio Constants
// All positions are ratios (0.0-1.0) of the diagram bounding box
// Reference dimensions: 500x228 (2.2:1 aspect ratio)
// ============================================================================

#define VITA_DIAGRAM_ASPECT_RATIO 2.193f

// Outline stroke widths (scale with diagram)
#define VITA_OUTLINE_WIDTH_RATIO 0.004f
#define VITA_OUTLINE_THIN_RATIO 0.002f

// Body
#define VITA_BODY_X_RATIO 0.000f
#define VITA_BODY_Y_RATIO 0.132f
#define VITA_BODY_W_RATIO 1.000f
#define VITA_BODY_H_RATIO 0.737f
#define VITA_BODY_RADIUS_RATIO 0.040f

// Screen (ratios relative to diagram dimensions)
// Note: W_RATIO adjusted from 0.596f to 0.598f to compensate for integer
// truncation in RATIO_W macro causing 1-2px right edge drift
#define VITA_SCREEN_X_RATIO 0.205f
#define VITA_SCREEN_Y_RATIO 0.085f
#define VITA_SCREEN_W_RATIO 0.598f
#define VITA_SCREEN_H_RATIO 0.740f

// D-pad
#define VITA_DPAD_CX_RATIO 0.090f
#define VITA_DPAD_CY_RATIO 0.500f
#define VITA_DPAD_ARM_LENGTH_RATIO 0.050f
#define VITA_DPAD_ARM_WIDTH_RATIO 0.032f

// Face buttons (diamond pattern)
#define VITA_FACE_CX_RATIO 0.910f
#define VITA_FACE_CY_RATIO 0.500f
#define VITA_BTN_TRIANGLE_CX_RATIO 0.910f
#define VITA_BTN_TRIANGLE_CY_RATIO 0.360f
#define VITA_BTN_CIRCLE_CX_RATIO 0.956f
#define VITA_BTN_CIRCLE_CY_RATIO 0.500f
#define VITA_BTN_CROSS_CX_RATIO 0.910f
#define VITA_BTN_CROSS_CY_RATIO 0.640f
#define VITA_BTN_SQUARE_CX_RATIO 0.864f
#define VITA_BTN_SQUARE_CY_RATIO 0.500f
#define VITA_FACE_BTN_RADIUS_RATIO 0.024f

// Analog sticks
#define VITA_LSTICK_CX_RATIO 0.200f
#define VITA_LSTICK_CY_RATIO 0.720f
#define VITA_RSTICK_CX_RATIO 0.800f
#define VITA_RSTICK_CY_RATIO 0.720f
#define VITA_STICK_OUTER_R_RATIO 0.050f
#define VITA_STICK_INNER_R_RATIO 0.030f
#define VITA_STICK_DOT_R_RATIO 0.008f

// Shoulder buttons (reduced to ~8% width and height for better proportions)
#define VITA_L_BTN_X_RATIO 0.040f
#define VITA_L_BTN_Y_RATIO 0.000f
#define VITA_L_BTN_W_RATIO 0.080f
#define VITA_L_BTN_H_RATIO 0.080f
#define VITA_L_BTN_RADIUS_RATIO 0.016f
#define VITA_R_BTN_X_RATIO 0.880f
#define VITA_R_BTN_Y_RATIO 0.000f

// System buttons
#define VITA_PS_BTN_CX_RATIO 0.500f
#define VITA_PS_BTN_CY_RATIO 0.920f
#define VITA_PS_BTN_R_RATIO 0.022f
#define VITA_START_CX_RATIO 0.600f
#define VITA_START_CY_RATIO 0.860f
#define VITA_SELECT_CX_RATIO 0.400f
#define VITA_SELECT_CY_RATIO 0.860f
#define VITA_SYS_BTN_R_RATIO 0.014f

// Rear touchpad
#define VITA_RTOUCH_X_RATIO 0.192f
#define VITA_RTOUCH_Y_RATIO 0.149f
#define VITA_RTOUCH_W_RATIO 0.617f
#define VITA_RTOUCH_H_RATIO 0.567f

// Camera
#define VITA_CAMERA_CX_RATIO 0.920f
#define VITA_CAMERA_CY_RATIO 0.180f
#define VITA_CAMERA_R_RATIO 0.020f
