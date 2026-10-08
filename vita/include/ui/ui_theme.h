/**
 * @file ui_theme.h
 * @brief Theme tokens and component geometry for the XMB UI (issue #271, SPEC.md section 1)
 *
 * Source of truth: docs/design/ui-mocks/SPEC.md section 1 and tokens-xmb.css.
 *
 * Rule for all XMB code: no raw colours or layout numbers outside this header.
 * Tokens (colours, type, spacing, lines, motion) are global; geometry that belongs
 * to one component is a UI_<COMPONENT>_* constant in that component's section below.
 * The older ui_constants.h stays for the screens that are not rebuilt yet.
 */

#pragma once

#include <vita2d.h>

/* ============================================================================
 * Colour (SPEC 1.1). Every colour is a vita2d ABGR value with alpha baked in.
 * ============================================================================ */

/** Converts a percentage (0..100) to an 8-bit alpha, rounded. */
#define UI_ALPHA_PCT(pct) (((pct) * 255 + 50) / 100)

#define UI_WHITE_PCT(pct) RGBA8(255, 255, 255, UI_ALPHA_PCT(pct))

#define UI_TEXT RGBA8(0xFF, 0xFF, 0xFF, 0xFF)
#define UI_TEXT_2 RGBA8(0xDD, 0xE3, 0xF0, 0xFF)
#define UI_TEXT_3 RGBA8(0xB3, 0xBD, 0xD2, 0xFF)
#define UI_OK RGBA8(0x4E, 0xE0, 0x9A, 0xFF)
#define UI_WARN RGBA8(0xFF, 0xBE, 0x4D, 0xFF)
#define UI_ERR RGBA8(0xFF, 0x80, 0x80, 0xFF)
#define UI_INTERNET RGBA8(0xA9, 0xB2, 0xFF, 0xFF)
#define UI_IDLE RGBA8(0xC9, 0xD0, 0xDF, 0xFF)
#define UI_ACCENT RGBA8(0x6D, 0xB4, 0xFF, 0xFF)
#define UI_ACCENT_ZONE RGBA8(0x6D, 0xB4, 0xFF, UI_ALPHA_PCT(28))

#define UI_PANEL RGBA8(6, 12, 28, UI_ALPHA_PCT(92))
#define UI_SCRIM RGBA8(2, 5, 14, UI_ALPHA_PCT(55))
#define UI_SCRIM_STRONG RGBA8(2, 5, 14, UI_ALPHA_PCT(82))
#define UI_HUD RGBA8(6, 10, 22, UI_ALPHA_PCT(60))
#define UI_PAGE_WASH RGBA8(3, 6, 16, UI_ALPHA_PCT(60))
#define UI_FILL_FOCUS UI_WHITE_PCT(12)
#define UI_FILL_ON UI_WHITE_PCT(26)
#define UI_LINE UI_WHITE_PCT(26)
#define UI_LINE_FAINT UI_WHITE_PCT(14)

/* Effect colours */
#define UI_GLOW UI_WHITE_PCT(70)
#define UI_GLOW_SOFT UI_WHITE_PCT(35)
#define UI_GLOW_INNER UI_WHITE_PCT(45)
#define UI_SHADOW RGBA8(0, 0, 0, UI_ALPHA_PCT(55))
#define UI_LEADER UI_WHITE_PCT(55)
#define UI_ZONE_LINE UI_WHITE_PCT(22)
#define UI_ZONE_MAPPED_LINE RGBA8(160, 205, 255, UI_ALPHA_PCT(60))
#define UI_RING_FILL UI_WHITE_PCT(10)
#define UI_HALO RGBA8(130, 170, 255, UI_ALPHA_PCT(28))
#define UI_QR_PLATE RGBA8(0xFA, 0xFA, 0xFA, 0xFF)
#define UI_QR_INK RGBA8(0x0A, 0x0A, 0x0A, 0xFF)
#define UI_QR_HIDDEN RGBA8(26, 26, 26, UI_ALPHA_PCT(55))
#define UI_PANEL_EDGE RGBA8(4, 8, 20, UI_ALPHA_PCT(92))
#define UI_EDGE_0 RGBA8(4, 8, 20, 0)
#define UI_GLASS_VEIL RGBA8(2, 5, 14, UI_ALPHA_PCT(14))
#define UI_GLASS_FROST UI_WHITE_PCT(5)
#define UI_GLASS_VEIL_DARK RGBA8(2, 5, 14, UI_ALPHA_PCT(20))

/* Home vignette gradients */
#define UI_VIG_1 RGBA8(2, 5, 14, UI_ALPHA_PCT(50))
#define UI_VIG_2 RGBA8(2, 5, 14, UI_ALPHA_PCT(34))
#define UI_VIG_3 RGBA8(2, 5, 14, UI_ALPHA_PCT(38))
#define UI_VIG_STRONG RGBA8(2, 5, 14, UI_ALPHA_PCT(55))
#define UI_VIG_TOP RGBA8(0, 0, 0, UI_ALPHA_PCT(32))
#define UI_VIG_BOTTOM RGBA8(0, 0, 0, UI_ALPHA_PCT(40))

/* Wave palette (one fixed set, no time of day) */
#define UI_BG_STAGE RGBA8(0x06, 0x0B, 0x1C, 0xFF)
#define UI_BG_TOP RGBA8(0x06, 0x20, 0x4A, 0xFF)
#define UI_BG_MID RGBA8(0x0F, 0x45, 0x85, 0xFF)
#define UI_BG_BOTTOM RGBA8(0x24, 0x5F, 0x9C, 0xFF)
#define UI_BG_RIBBON_1 RGBA8(0x96, 0xCD, 0xFF, 0xFF)
#define UI_BG_RIBBON_2 RGBA8(0x64, 0xB4, 0xF0, 0xFF)
#define UI_BG_RIBBON_3 RGBA8(0xBE, 0xDC, 0xFF, 0xFF)
#define UI_BG_HORIZON RGBA8(160, 210, 255, UI_ALPHA_PCT(28))

/* ============================================================================
 * Type (SPEC 1.2). Five faces from two loaded weights; sizes are the atlas sizes.
 * ============================================================================ */

#define UI_WEIGHT_LIGHT 300
#define UI_WEIGHT_REGULAR 400

#define UI_T14_SIZE 14
#define UI_T14_LINE 20
#define UI_T14_WEIGHT UI_WEIGHT_REGULAR
#define UI_T16_SIZE 16
#define UI_T16_LINE 24
#define UI_T16_WEIGHT UI_WEIGHT_REGULAR
#define UI_T20_SIZE 20
#define UI_T20_LINE 24
#define UI_T20_WEIGHT UI_WEIGHT_LIGHT
#define UI_T28_SIZE 28
#define UI_T28_LINE 32
#define UI_T28_WEIGHT UI_WEIGHT_LIGHT
#define UI_T40_SIZE 40
#define UI_T40_LINE 48
#define UI_T40_WEIGHT UI_WEIGHT_LIGHT

/** The five type faces; the order is the index into the table in ui_text.c. */
typedef enum ui_face_t {
  UI_FACE_T14 = 0,
  UI_FACE_T16,
  UI_FACE_T20,
  UI_FACE_T28,
  UI_FACE_T40,
  UI_FACE_COUNT
} UiFace;

/* ============================================================================
 * Spacing, layout, lines, radii (SPEC 1.3). One 8 px grid.
 * ============================================================================ */

#define UI_S1 8
#define UI_S2 16
#define UI_S3 24
#define UI_S4 32
#define UI_S5 40
#define UI_S6 48

#define UI_MARGIN_X 48
#define UI_TOP_Y 16
#define UI_TITLE_Y 64
#define UI_RULE_Y 120
#define UI_BODY_Y 136
#define UI_HINT_Y 496
#define UI_ROW_H 48
#define UI_ROW_H_LARGE 56
#define UI_ICON_BOX 64
#define UI_TAP_MIN 48

#define UI_LW1 1
#define UI_LW2 2

#define UI_R_SM 8
#define UI_R_MD 16

/* Motion */
#define UI_D1_MS 150
#define UI_D2_MS 300
#define UI_D3_MS 600
#define UI_TOGGLE_MS 180
#define UI_CASCADE_STEP_MS 45
#define UI_CASCADE_MAX_ROWS 6
/** Ease-out curve cubic-bezier(.22, .7, .2, 1): control points (x1, y1) and (x2, y2). */
#define UI_EASE_X1 0.22f
#define UI_EASE_Y1 0.7f
#define UI_EASE_X2 0.2f
#define UI_EASE_Y2 1.0f

/* D-pad hold-repeat (SPEC 4, rule 3) */
#define UI_REPEAT_DELAY_MS 400
#define UI_REPEAT_INTERVAL_MS 100

/* Touch: a finger that moves further than this from touch-down is never also a tap. */
#define UI_TOUCH_DRAG_PX 8

/* ============================================================================
 * C01 CategoryBar
 * ============================================================================ */

#define UI_CAT_COUNT 4
#define UI_CAT_BOX 64
#define UI_CAT_ART 48
#define UI_CAT_Y 104
#define UI_CAT_X0 256
#define UI_CAT_FAR 128
#define UI_CAT_STEP 112
#define UI_CAT_SCALE_ON 1.25f
#define UI_CAT_SCALE_LEFT 0.75f
#define UI_CAT_UNFOCUSED_PCT 62
#define UI_CAT_LABEL_GAP UI_S2
#define UI_CAT_STRIP_Y 64
#define UI_CAT_STRIP_H 112
#define UI_CAT_HIT 64

/* ============================================================================
 * C02 XmbList
 * ============================================================================ */

#define UI_LIST_X 224
#define UI_LIST_Y 160
#define UI_LIST_W 368
#define UI_LIST_H 336
#define UI_ITEM_ICON 38
#define UI_LIST_ICON_BOX 64
#define UI_LIST_ICON_CX 256
#define UI_LIST_TEXT_X 304
#define UI_LIST_TEXT_W 288
#define UI_LIST_FOCUS_Y 192
#define UI_LIST_ROW_H 64
#define UI_LIST_FOCUS_GAP 16
#define UI_LIST_SLIDE 64
#define UI_LIST_GLOW 112
#define UI_LIST_GLOW_PCT 50
#define UI_LIST_DIM_PCT 55
/** Status dot: diameter 12 px, then UI_S1 before the label. */
#define UI_LIST_DOT_R 6
#define UI_LIST_DOT_GAP UI_S1
/** Gap between the status label and the "Internet" route label. */
#define UI_LIST_ROUTE_GAP UI_S1
/** Rows fade out over the last UI_LIST_FADE_H pixels of the viewport (bottom edge UI_LIST_Y +
 * UI_LIST_H). */
#define UI_LIST_FADE_H 48
/** Largest number of rows the list can hold (matches the console cache). */
#define UI_LIST_MAX_ITEMS 64

/* ============================================================================
 * Home screen (ui_home.c)
 * ============================================================================ */

/** Holding Confirm this long on a console with both a local and an Internet route opens "Connect
 * via". */
#define UI_HOME_LONG_PRESS_MS 600
