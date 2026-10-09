/**
 * @file ui_constants.h
 * @brief Shared UI constants for VitaRPS5: screen size, a few colors, and the
 * controller diagram ratios. Layout and type tokens live in ui_theme.h.
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
// Colors (ABGR format for vita2d)
// ============================================================================
#define UI_COLOR_PRIMARY_BLUE 0xFFFF9034  // PlayStation Blue #3490FF
#define UI_COLOR_CARD_BG 0xFF37322D       // Dark charcoal (45,50,55)
#define UI_COLOR_SHADOW 0x3C000000        // Semi-transparent black for shadows

// ============================================================================
// Card Cache
// ============================================================================
#define CARD_CACHE_UPDATE_INTERVAL_US (10 * 1000000)  // 10 seconds

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
// Controller presets
// ============================================================================
#define CTRL_PRESET_COUNT 3  // Number of controller presets (Custom 1, 2, 3)

// ============================================================================
// Procedural Controller Diagram - Ratio Constants
// All positions are ratios (0.0-1.0) of the diagram bounding box
// Reference dimensions: 500x228 (2.2:1 aspect ratio)
// ============================================================================

// Outline stroke widths (scale with diagram)
#define VITA_OUTLINE_WIDTH_RATIO 0.004f

// Body
#define VITA_BODY_X_RATIO 0.000f
#define VITA_BODY_Y_RATIO 0.132f
#define VITA_BODY_W_RATIO 1.000f
#define VITA_BODY_H_RATIO 0.737f

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
