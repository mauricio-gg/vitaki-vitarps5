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

/** Right edge of the content area (screen width minus UI_MARGIN_X). */
#define UI_CONTENT_RIGHT 912

/* Motion */
#define UI_D1_MS 150
#define UI_D2_MS 300
#define UI_D3_MS 600
#define UI_TOGGLE_MS 180
#define UI_CASCADE_STEP_MS 45
#define UI_CASCADE_MAX_ROWS 6
/** A rising item (list row cascade, detail panel) starts this many pixels low and fades in. */
#define UI_RISE_PX 16
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
 * C27 Background (ui_background.c). Constants are the formulas of paintRibbons in
 * docs/design/ui-mocks/xmb-wave.js; times there are milliseconds.
 * ============================================================================ */

/** Alpha of a colour with its alpha byte cleared: the same colour, fully transparent. */
#define UI_COLOR_CLEAR(c) ((c) & 0x00FFFFFFu)

/* Vertex update rate: 30 Hz, halved to 15 Hz on the Connecting and Reconnecting screens.
 * The slack lets a 60 Hz frame that lands just short of the interval still update. */
#define UI_BG_UPDATE_US 33333
#define UI_BG_UPDATE_SLOW_US 66666
#define UI_BG_UPDATE_SLACK_US 8000
#define UI_BG_MS_PER_US 0.001f

/* Vertical gradient: top, mid at 55% of the screen height, bottom. */
#define UI_BG_MID_STOP_PCT 55

/* Horizon glow: a radial gradient centred low on the screen, drawn as a triangle fan. */
#define UI_BG_HORIZON_X 560
#define UI_BG_HORIZON_Y 520
#define UI_BG_HORIZON_R 520
#define UI_BG_HORIZON_SEGMENTS 32

/* Ribbons: ribbon i has k = i / (count - 1); colours cycle through the three ribbon tokens. */
#define UI_BG_RIBBON_COUNT 5
#define UI_BG_RIBBON_COLOURS 3
#define UI_BG_COL_X0 (-30)
#define UI_BG_COL_X1 990
#define UI_BG_COL_STEP 15
#define UI_BG_COLS (((UI_BG_COL_X1 - UI_BG_COL_X0) / UI_BG_COL_STEP) + 1)
#define UI_BG_AMP_BASE 40.0f
#define UI_BG_AMP_PER_K 60.0f
#define UI_BG_SPEED_BASE 0.00014f
#define UI_BG_SPEED_PER_K 0.00022f
#define UI_BG_Y_BASE 310.0f
#define UI_BG_Y_PER_RIBBON 28.0f
#define UI_BG_Y_PER_K (-20.0f)
#define UI_BG_ALPHA_BASE 0.07f
#define UI_BG_ALPHA_PER_K 0.15f
/** Waves 1 and 2 of the centre line: spatial frequency, phase offset per ribbon, and the
 * second wave's share of the amplitude. */
#define UI_BG_WAVE1_FREQ 0.0048f
#define UI_BG_WAVE1_TIME_MUL 1.3f
#define UI_BG_WAVE1_PHASE 1.7f
#define UI_BG_WAVE2_FREQ 0.011f
#define UI_BG_WAVE2_AMP 0.28f
/** Band thickness: base + per k, modulated by a slow sine. */
#define UI_BG_THICK_BASE 34.0f
#define UI_BG_THICK_PER_K 34.0f
#define UI_BG_THICK_FREQ 0.0038f
#define UI_BG_THICK_TIME 0.0002f
#define UI_BG_THICK_MOD_BASE 16.0f
#define UI_BG_THICK_MOD_PER_K 20.0f
/** Highlight line: wobble around the band centre, width and alpha. */
#define UI_BG_LINE_FREQ 0.02f
#define UI_BG_LINE_TIME 0.0005f
#define UI_BG_LINE_WOBBLE 0.1f
#define UI_BG_LINE_W_BASE 1.0f
#define UI_BG_LINE_W_PER_K 1.0f
#define UI_BG_LINE_ALPHA_BASE 0.05f
#define UI_BG_LINE_ALPHA_PER_K 0.16f
/** Horizontal fade of a band: transparent at both screen edges, full alpha between 30% and
 * 70% of the width (0.9 of it at the 70% stop). */
#define UI_BG_FADE_IN_PCT 30
#define UI_BG_FADE_OUT_PCT 70
#define UI_BG_FADE_OUT_GAIN 0.9f
/** The mock adds the ribbons to the wave (additive blending); vita2d blends with normal alpha,
 * which is darker. The band alpha is multiplied by this to keep the ribbons as bright. */
#define UI_BG_BLEND_GAIN 1.4f

/* Dust: points placed once from a small linear generator, then twinkling and drifting. */
#define UI_BG_DUST_COUNT 36
#define UI_BG_DUST_X_MUL 9301
#define UI_BG_DUST_X_ADD 49297
#define UI_BG_DUST_Y_MUL 7919
#define UI_BG_DUST_Y_ADD 1013
#define UI_BG_DUST_MOD 233280
#define UI_BG_DUST_SIZE_MUL 37
#define UI_BG_DUST_SIZE_STEPS 10
#define UI_BG_DUST_SIZE_BASE 0.5f
#define UI_BG_DUST_SIZE_DIV 12.0f
#define UI_BG_DUST_PHASE_STEP 1.3f
#define UI_BG_DUST_TWINKLE_BASE 0.3f
#define UI_BG_DUST_TWINKLE_AMP 0.3f
#define UI_BG_DUST_TWINKLE_TIME 0.0012f
#define UI_BG_DUST_ALPHA 0.5f
#define UI_BG_DUST_DRIFT 0.004f
#define UI_BG_DUST_BOB_TIME 0.0004f
#define UI_BG_DUST_BOB_AMP 6.0f

/** Depth of every background vertex (vita2d draws 2D shapes at z = 0.5). */
#define UI_BG_Z 0.5f

/* Home vignette: three gradient layers (xmb.css .vig), stops as a percentage along each axis.
 * Right edge: VIG_1 to VIG_2 at 38% to clear at 62%. Left edge: VIG_STRONG to VIG_3 at 50% to
 * clear at 70%. Vertical: VIG_TOP to clear at 20%, clear until 82%, then VIG_BOTTOM. */
#define UI_VIG_RIGHT_MID_PCT 38
#define UI_VIG_RIGHT_END_PCT 62
#define UI_VIG_LEFT_MID_PCT 50
#define UI_VIG_LEFT_END_PCT 70
#define UI_VIG_TOP_END_PCT 20
#define UI_VIG_BOTTOM_START_PCT 82

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
/** Gap on each side of an inline button glyph in a status line. */
#define UI_LIST_GLYPH_GAP 4
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

/* ============================================================================
 * Rounded shapes (SPEC 1.3): fixed heights of the baked 3-slice shapes
 * ============================================================================ */

#define UI_SHAPE_H_BAR 48
#define UI_SHAPE_H_BAR_LARGE 56
#define UI_SHAPE_H_BUTTON 48
#define UI_SHAPE_H_PILL 32
#define UI_SHAPE_H_TRACK 24

/* =====================================================================
/* ============================================================================
 * C19 Pill
 * ============================================================================ */

#define UI_PILL_H UI_SHAPE_H_PILL
#define UI_PILL_PAD UI_S2
#define UI_PILL_DOT_R 6
#define UI_PILL_DOT_GAP UI_S1
/** The unstable pill pulses its opacity between 100% and UI_PILL_PULSE_MIN_PCT over this period. */
#define UI_PILL_PULSE_MS 1400
#define UI_PILL_PULSE_MIN_PCT 55

/* ============================================================================
 * C23 TopBar
 * ============================================================================ */

#define UI_TOPBAR_H 32
#define UI_TOPBAR_ICON 24
#define UI_TOPBAR_GAP UI_S3
#define UI_TOPBAR_ICON_GAP UI_S1
#define UI_TOPBAR_SLOT_PAD UI_S3
/** System reads (Wi-Fi link, battery, clock) are refreshed this often, never per frame. */
#define UI_TOPBAR_POLL_MS 1000
/** Wi-Fi icon opacity while there is no link. */
#define UI_TOPBAR_OFFLINE_PCT 45

/* ============================================================================
 * C06 HintRow
 * ============================================================================ */

#define UI_HINT_H 48
#define UI_HINT_GAP UI_S3
#define UI_HINT_GLYPH_H 20
#define UI_HINT_GLYPH_GAP UI_S1
/** Gap between the two badges of the combined L R glyph. */
#define UI_HINT_LR_GAP 4
#define UI_HINT_DIM_PCT 45
/** Right slot reserved for the Network Unstable pill. */
#define UI_HINT_ALERT_W 200
/** Hints never run closer than this to the alert slot. */
#define UI_HINT_ALERT_GAP UI_S2
/** The most hints one row can hold. */
#define UI_HINT_MAX_ITEMS 8

/* ============================================================================
 * C04 DetailPanel
 * ============================================================================ */

#define UI_DETAIL_X 608
#define UI_DETAIL_Y 192
#define UI_DETAIL_W 304
#define UI_DETAIL_LOGO_H 48
#define UI_DETAIL_LOGO_PS5_W 176
#define UI_DETAIL_LOGO_PS4_W 200
/** Source rows cropped to the wordmark: PS5_logo.png (132 x 49) keeps 36 rows from row 8; ps4.png
 * (100 x 100) keeps 24 rows from row 38. The crop is scaled so the visible height is
 * UI_DETAIL_LOGO_H. */
#define UI_DETAIL_PS5_SRC_W 132
#define UI_DETAIL_PS5_SRC_Y 8
#define UI_DETAIL_PS5_SRC_H 36
#define UI_DETAIL_PS4_SRC_W 100
#define UI_DETAIL_PS4_SRC_Y 38
#define UI_DETAIL_PS4_SRC_H 24
#define UI_DETAIL_KV_H 32
/** Gap between a kv label and its value; the value is clipped to what is left. */
#define UI_DETAIL_KV_GAP UI_S1
/** The status message wraps to at most this many lines of UI_T16_LINE. */
#define UI_DETAIL_MESSAGE_LINES 3
/** Space left under the status line when a message follows it (the mock pulls it up by 8). */
#define UI_DETAIL_MESSAGE_GAP UI_S1
/** A list-kind panel without a description keeps this much blank under the title. */
#define UI_DETAIL_BLANK_H UI_S2
