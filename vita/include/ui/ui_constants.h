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
#define UI_COLOR_PRIMARY_BLUE       0xFFFF9034  // PlayStation Blue #3490FF
#define UI_COLOR_BACKGROUND         0xFF1A1614  // Animated charcoal gradient base
#define UI_COLOR_CARD_BG            0xFF37322D  // Dark charcoal (45,50,55)
#define UI_COLOR_TEXT_PRIMARY       0xFFFAFAFA  // Off-white (reduced eye strain)
#define UI_COLOR_TEXT_SECONDARY     0xFFB4B4B4  // Light Gray
#define UI_COLOR_TEXT_TERTIARY      0xFFA0A0A0  // Medium Gray
#define UI_COLOR_STATUS_AVAILABLE   0xFF50AF4C  // Success Green #4CAF50
#define UI_COLOR_STATUS_CONNECTING  0xFF0098FF  // Warning Orange #FF9800
#define UI_COLOR_STATUS_UNAVAILABLE 0xFF3643F4  // Error Red #F44336
#define UI_COLOR_ACCENT_PURPLE      0xFFB0279C  // Accent Purple #9C27B0
#define UI_COLOR_SHADOW             0x3C000000  // Semi-transparent black for shadows

// ============================================================================
// Particle Colors (ABGR with alpha - 0xCC = 80% opaque / 20% transparent)
// ============================================================================
#define PARTICLE_COLOR_RED          0xCCFF5555  // 80% opaque red
#define PARTICLE_COLOR_GREEN        0xCC55FF55  // 80% opaque green
#define PARTICLE_COLOR_BLUE         0xCC5555FF  // 80% opaque blue
#define PARTICLE_COLOR_ORANGE       0xCC55AAFF  // 80% opaque orange

// ============================================================================
// Typography (from docs/ai/UI_FINAL_SPECIFICATION.md)
// ============================================================================
#define FONT_SIZE_HEADER            24  // Screen titles, primary headers
#define FONT_SIZE_SUBHEADER         18  // Section titles, tab labels
#define FONT_SIZE_BODY              16  // Paragraph/body text
#define FONT_SIZE_SMALL             14  // Secondary text, hints, status badges

// ============================================================================
// Navigation Layout (per UI spec)
// ============================================================================
#define WAVE_NAV_WIDTH              130     // Per UI spec line 45
#define CONTENT_AREA_X              WAVE_NAV_WIDTH
#define CONTENT_AREA_WIDTH          (VITA_WIDTH - WAVE_NAV_WIDTH)  // 830px
#define CONTENT_CENTER_X            (WAVE_NAV_WIDTH + (CONTENT_AREA_WIDTH / 2))

// Wave navigation icons
#define WAVE_NAV_ICON_SIZE          32      // Per spec: 32x32px icons
#define WAVE_NAV_ICON_X             50      // Positioned left of center
#define WAVE_NAV_ICON_SPACING       80      // Spacing between icon centers
#define WAVE_NAV_ICON_START_Y       152     // Vertically centered start

// ============================================================================
// Particle Animation (Batch 3: Particle Background Enhancements)
// ============================================================================
#define PARTICLE_COUNT              8       // Optimized from 12 for performance
#define PARTICLE_LAYER_BG_SPEED     0.7f
#define PARTICLE_LAYER_FG_SPEED     1.0f
#define PARTICLE_SWAY_AMPLITUDE     2.0f
#define PARTICLE_SWAY_SPEED_MIN     0.5f
#define PARTICLE_SWAY_SPEED_MAX     1.5f

// ============================================================================
// Wave Animation (per SCOPING_UI_POLISH.md)
// ============================================================================
#define WAVE_SPEED_BOTTOM           0.7f    // radians per second for bottom wave
#define WAVE_SPEED_TOP              1.1f    // radians per second for top wave
#define WAVE_ALPHA_BOTTOM           160     // 160/255 opacity for bottom wave
#define WAVE_ALPHA_TOP              100     // 100/255 opacity for top wave

// ============================================================================
// Collapsible Navigation (per SCOPING_NAV_COLLAPSIBLE_BAR.md)
// ============================================================================
#define NAV_COLLAPSE_DURATION_MS    280     // Total animation duration
#define NAV_PHASE1_END_MS           80      // Preparation phase end
#define NAV_PHASE2_END_MS           200     // Collapse phase end
#define NAV_PILL_WIDTH              140     // Pill width when fully collapsed
#define NAV_PILL_HEIGHT             44      // Pill height
#define NAV_PILL_X                  16      // Pill X position
#define NAV_PILL_Y                  16      // Pill Y position
#define NAV_PILL_RADIUS             22      // Pill corner radius (fully rounded)
#define NAV_TOAST_DURATION_MS       2000    // Toast display duration
#define NAV_TOAST_FADE_MS           300     // Toast fade in/out duration

// ============================================================================
// Console Cards (updated per UI spec)
// ============================================================================
#define CONSOLE_CARD_WIDTH              200     // Reverted to original 200px
#define CONSOLE_CARD_HEIGHT             205     // +5px bottom margin
#define CONSOLE_CARD_SPACING            100     // Horizontal spacing
#define CONSOLE_CARD_START_Y            150
#define CONSOLE_CARD_FOCUS_SCALE_MIN    0.95f
#define CONSOLE_CARD_FOCUS_SCALE_MAX    1.0f
#define CONSOLE_CARD_FOCUS_DURATION_MS  180
#define CONSOLE_CARD_GLOW_COLOR         0xFFFF9034  // #3490FF in ABGR

// Card typography
#define CARD_TITLE_FONT_SIZE            20      // Card name text
#define CARD_LOGO_MAX_WIDTH             120     // PS5 logo max width
#define CARD_LOGO_TOP_PADDING           20      // 20px from top of card
#define CARD_NAME_BAR_BOTTOM_OFFSET     80      // Distance from bottom
#define CARD_TEXT_BASELINE_OFFSET       7       // Vertical baseline adjustment

// ============================================================================
// Toggle Switch Animation
// ============================================================================
#define TOGGLE_ANIMATION_DURATION_MS    180     // 180ms for smooth feel

// ============================================================================
// Hints Popup
// ============================================================================
#define HINTS_POPUP_DURATION_MS         7000
#define HINTS_FADE_DURATION_MS          500

// ============================================================================
// Card Cache
// ============================================================================
#define CARD_CACHE_UPDATE_INTERVAL_US   (10 * 1000000)  // 10 seconds

// ============================================================================
// Text Width Cache
// ============================================================================
#define TEXT_WIDTH_CACHE_SIZE           16

// ============================================================================
// Tooltip
// ============================================================================
#define MAX_TOOLTIP_CHARS               200

// ============================================================================
// Legacy Layout (will be phased out)
// ============================================================================
#define HEADER_BAR_X                    136
#define HEADER_BAR_Y                    43
#define HEADER_BAR_H                    26
#define HEADER_BAR_W                    774
#define HOST_SLOTS_X                    (HEADER_BAR_X - 86)
#define HOST_SLOTS_Y                    (HEADER_BAR_Y + HEADER_BAR_H + 43)
#define HOST_SLOT_W                     400
#define HOST_SLOT_H                     190

// ============================================================================
// Texture Paths
// ============================================================================
#define TEXTURE_PATH                    "app0:/assets/"
#define IMG_PS4_PATH                    TEXTURE_PATH "ps4.png"
#define IMG_PS4_OFF_PATH                TEXTURE_PATH "ps4_off.png"
#define IMG_PS4_REST_PATH               TEXTURE_PATH "ps4_rest.png"
#define IMG_PS5_PATH                    TEXTURE_PATH "ps5.png"
#define IMG_PS5_OFF_PATH                TEXTURE_PATH "ps5_off.png"
#define IMG_PS5_REST_PATH               TEXTURE_PATH "ps5_rest.png"
#define IMG_DISCOVERY_HOST              TEXTURE_PATH "discovered_host.png"

// ============================================================================
// Debug Menu
// ============================================================================
#ifndef VITARPS5_DEBUG_MENU
#define VITARPS5_DEBUG_MENU             0
#endif

#ifndef VIDEO_LOSS_ALERT_DEFAULT_US
#define VIDEO_LOSS_ALERT_DEFAULT_US     (5 * 1000 * 1000ULL)
#endif

// ============================================================================
// Graphics Primitives (ui_graphics.c)
// ============================================================================
#define UI_SHADOW_OFFSET_PX             4       // Drop shadow offset in pixels
#define UI_SPINNER_SEGMENTS             32      // Segments for smooth spinner arc
#define UI_CIRCLE_OUTLINE_SEGMENTS      48      // Segments for circle outlines
#define UI_OFFSCREEN_MARGIN             100     // Margin for offscreen culling

// Loss indicator badge
#define UI_LOSS_INDICATOR_MARGIN        18      // Margin from screen edge
#define UI_LOSS_INDICATOR_PADDING_X     18      // Horizontal padding
#define UI_LOSS_INDICATOR_PADDING_Y     6       // Vertical padding
