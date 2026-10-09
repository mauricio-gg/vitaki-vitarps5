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

#include "ui/ui_gesture.h"

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

/* Touch thresholds (UI_TOUCH_DRAG_PX, UI_LONG_PRESS_MS) live in ui_gesture.h, which has no SDK
 * dependency so the gesture rules can be tested natively. */

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

/* Blur levels: the wave is rendered into a small target and drawn upscaled with bilinear
 * filtering. Soft uses 1/4 scale, Strong and Dark 1/16 (SPEC C27, FEASIBILITY section 8).
 * Both sizes divide the screen exactly, so the upscale factor is VITA_WIDTH / width. */
#define UI_BG_BLUR_SOFT_W 240
#define UI_BG_BLUR_SOFT_H 136
#define UI_BG_BLUR_STRONG_W 60
#define UI_BG_BLUR_STRONG_H 34
/* GXM reads a linear texture with rows padded to a multiple of this many pixels. */
#define UI_BG_BLUR_STRIDE_ALIGN 8

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
#define UI_CAT_SWIPE_PX 56 /* horizontal swipe distance per category step (SPEC C01) */

/* ============================================================================
 * C02 XmbList
 * ============================================================================ */

#define UI_LIST_X 224
#define UI_LIST_Y 160
#define UI_LIST_W 368
#define UI_LIST_H 336
#define UI_ITEM_ICON 38
#define UI_ROOM_ICON_GRID 48 /* Room icon in the Change-icon picker grid (SPEC C12) */
#define UI_ROOM_ICON_RING 64 /* Room icon in the Connecting ring centre (SPEC C03) */
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
 * C05 OptionsColumn (ui_options_column.c): x 608 to the right edge, the console's Triangle menu
 * ============================================================================ */

#define UI_OPTS_X 608
#define UI_OPTS_W 352
/** The column's content (title, labels, dividers) sits this far from its left and right edges. */
#define UI_OPTS_PAD UI_MARGIN_X
#define UI_OPTS_TITLE_Y 88
#define UI_OPTS_SUB_Y 120
#define UI_OPTS_ROW_Y 160
#define UI_OPTS_ROW_H UI_ROW_H_LARGE
/** The focus bar reaches this far into the column's padding on each side; the label sits inside it.
 */
#define UI_OPTS_BAR_INSET UI_S4
#define UI_OPTS_LABEL_PAD UI_S2
#define UI_OPTS_GLOW UI_ROW_GLOW
#define UI_OPTS_GLOW_PCT UI_ROW_GLOW_PCT
/** The left edge fades from EDGE_0 to PANEL_EDGE over this share of the column's width. */
#define UI_OPTS_FEATHER_PCT 18
#define UI_OPTS_SLIDE_MS UI_D2_MS
#define UI_OPTS_DISABLED_PCT 45
/** Opacity of the Home layers (top bar, categories, list, detail) while the column is open. */
#define UI_OPTS_BEHIND_PCT 25
/** Most rows the column holds (Connect, Connect via, Re-pair, and room for Change icon). */
#define UI_OPTS_MAX_ROWS 4
/** Bytes kept of the console name shown as the column's title. */
#define UI_OPTS_NAME_MAX 64

/* ============================================================================
 * C11 Popup (ui_popup.c): the one modal frame. Sizes S, M and L are 480 wide at x 240.
 * ============================================================================ */

#define UI_POPUP_X 240
#define UI_POPUP_W 480
#define UI_POPUP_PAD UI_S4
#define UI_POPUP_S_Y 144
#define UI_POPUP_S_H 256
#define UI_POPUP_M_Y 96
#define UI_POPUP_M_H 352
#define UI_POPUP_L_Y 56
#define UI_POPUP_L_H 432
#define UI_POPUP_ICON 32
#define UI_POPUP_ICON_GAP UI_S2
/** Space between the last header line (title or subtitle) and the body. */
#define UI_POPUP_BODY_GAP UI_S1
#define UI_POPUP_BODY_LINES 3
#define UI_POPUP_BUTTON_GAP UI_S2
#define UI_POPUP_BUTTON_MAX_W 208
#define UI_POPUP_MAX_BUTTONS 2
#define UI_POPUP_ENTER_MS UI_D2_MS
/** Bytes kept of the title and of the subtitle (UTF-8). */
#define UI_POPUP_TEXT_MAX 96

/* ============================================================================
 * C12 ListPopup (ui_list_popup.c): rows and the icon grid inside a C11 popup
 * ============================================================================ */

#define UI_LISTPOP_ROW_H UI_SHAPE_H_BAR
/** Label and right label sit this far inside the row's focus bar. */
#define UI_LISTPOP_ROW_PAD UI_S1
/** A list or grid in an M or L popup starts this far under the header. */
#define UI_LISTPOP_TOP_GAP UI_S2
/** Rows an L popup shows before it scrolls. */
#define UI_LISTPOP_MAX_VISIBLE 6
/** Rows or cells a list popup holds. */
#define UI_LISTPOP_MAX_ROWS 16
/** Bytes kept of a row's label and of its right label (UTF-8). */
#define UI_LISTPOP_TEXT_MAX 48
/** The check mark of the current row, drawn from the 32 px popup check art scaled down. */
#define UI_LISTPOP_CHECK 20
/** Space between a right label and the check mark. */
#define UI_LISTPOP_CHECK_GAP UI_S1
#define UI_LISTPOP_GRID_COLS 3
#define UI_LISTPOP_CELL_W 128
#define UI_LISTPOP_CELL_H 104
#define UI_LISTPOP_CELL_GAP UI_S2
#define UI_LISTPOP_CELL_ICON UI_ROOM_ICON_GRID
/** Gap between a cell's icon and its label. */
#define UI_LISTPOP_CELL_LABEL_GAP UI_S1
#define UI_LISTPOP_CELL_ICON_PCT 70
#define UI_LISTPOP_CELL_ICON_FOCUS_PCT 120
#define UI_LISTPOP_CELL_GLOW UI_ROW_GLOW
/** The quiet check of the current cell, and its inset from the cell's top-right corner. */
#define UI_LISTPOP_CELL_CHECK 16
#define UI_LISTPOP_CELL_CHECK_INSET UI_S1

/* The background freeze (ui_freeze.c): a half-resolution copy of the screen behind a popup. */
#define UI_FREEZE_W 480
#define UI_FREEZE_H 272

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
/** Space either side of a button glyph drawn inside a plain pill's label. */
#define UI_PILL_GLYPH_MARGIN 2

/** Stream overlay (SPEC 3.5): the exit hint pill sits UI_STREAM_OVERLAY_MARGIN from the top and
 * right edges, stays UI_STREAM_HINT_VISIBLE_MS after the stream starts, then fades out linearly
 * over UI_STREAM_HINT_FADE_MS. */
#define UI_STREAM_OVERLAY_MARGIN UI_S2
#define UI_STREAM_HINT_VISIBLE_MS 5000
#define UI_STREAM_HINT_FADE_MS 500

/* ============================================================================
 * C25 StatsPanel
 * ============================================================================ */

/** Fixed slot: right edge UI_STREAM_OVERLAY_MARGIN from the screen's right, top at y 64. */
#define UI_STATS_TOP 64
#define UI_STATS_MIN_W 176
#define UI_STATS_PAD_X UI_S2
#define UI_STATS_PAD_Y UI_S1
#define UI_STATS_LINE_H 24
/** Space under the title line. */
#define UI_STATS_TITLE_GAP 4
/** Smallest space between a row's label and its value. */
#define UI_STATS_COL_GAP UI_S2
#define UI_STATS_ROWS 2
/** The value text is rebuilt this often (microseconds), never per frame. */
#define UI_STATS_REBUILD_US 1000000ULL

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
#define UI_DETAIL_KV_H 32
/** Gap between a kv label and its value; the value is clipped to what is left. */
#define UI_DETAIL_KV_GAP UI_S1
/** The status message wraps to at most this many lines of UI_T16_LINE. */
#define UI_DETAIL_MESSAGE_LINES 3
/** Space left under the status line when a message follows it (the mock pulls it up by 8). */
#define UI_DETAIL_MESSAGE_GAP UI_S1
/** A list-kind panel without a description keeps this much blank under the title. */
#define UI_DETAIL_BLANK_H UI_S2

/* ============================================================================
 * Type logo (ui_type_logo.c): the wordmark cropped from PS5_logo.png (132 x 49) and ps4.png
 * (100 x 100), drawn white at any height. PS5 keeps 36 rows from row 8; PS4 keeps 24 rows from
 * row 38. The crop is scaled so the visible height is the wanted height (48 gives 176 and 200
 * wide).
 * ============================================================================ */

#define UI_LOGO_PS5_SRC_W 132
#define UI_LOGO_PS5_SRC_Y 8
#define UI_LOGO_PS5_SRC_H 36
#define UI_LOGO_PS4_SRC_W 100
#define UI_LOGO_PS4_SRC_Y 38
#define UI_LOGO_PS4_SRC_H 24

/* ============================================================================
 * C07 PageShell frame (ui_page_frame.c)
 * ============================================================================ */

#define UI_PAGE_ICON 32
#define UI_PAGE_TITLE_X 96
#define UI_PAGE_TITLE_H 48
/** The back chevron at the left of the title row (Settings, Profile, Controller): a UI_TAP_MIN box
 * at x 0 holding the art. */
#define UI_PAGE_BACK_ART 24
#define UI_PAGE_BACK_STROKE 2.0f

/* Page body: the group list on the left, the setting pane on the right, a description line under
 * both (SPEC C07). The pane shows UI_PAGE_PANE_ROWS rows of UI_ROW_H. */
#define UI_PAGE_BODY_Y UI_BODY_Y
#define UI_PAGE_GROUP_X UI_MARGIN_X
#define UI_PAGE_GROUP_W 256
#define UI_PAGE_PANE_X 336
#define UI_PAGE_PANE_W 576
#define UI_PAGE_PANE_ROWS 6
#define UI_PAGE_PANE_H (UI_PAGE_PANE_ROWS * UI_ROW_H)
#define UI_PAGE_DESC_Y 440
#define UI_PAGE_DESC_H 48
#define UI_PAGE_DESC_LINES 2
#define UI_PAGE_SCROLL_X 920

/* ============================================================================
 * C07 GroupList (ui_group_list.c): the left list of a page
 * ============================================================================ */

#define UI_GROUP_MAX 8
#define UI_GROUP_ROW_H UI_ROW_H_LARGE
/** The label sits this far from the list's left edge. */
#define UI_GROUP_PAD UI_S2
/** The current group's marker: a UI_LW2 wide bar at the left edge, inset from the row's top and
 * bottom. */
#define UI_GROUP_BAR_W UI_LW2
#define UI_GROUP_BAR_INSET UI_S2
#define UI_GROUP_GLOW 14
#define UI_GROUP_GLOW_PCT 20

/* ============================================================================
 * C08 SettingRow list, C09 Toggle, C10 ChoiceValue (ui_setting_list.c)
 * ============================================================================ */

#define UI_SETTING_MAX_ROWS 16
/** Most bytes of a choice's value text kept (longer values are shortened with an ellipsis). */
#define UI_SETTING_VALUE_MAX 48
#define UI_ROW_PAD UI_S2
#define UI_ROW_GLOW 12
#define UI_ROW_GLOW_PCT 20
/** The row shows FILL_ON for this long after it acted. */
#define UI_ROW_PRESS_MS UI_D1_MS
/** A vertical swipe on the pane moves the focus one row per this many pixels from touch-down. */
#define UI_ROW_SWIPE_PX UI_ROW_H

/* Toggle: a pill track with a UI_TOGGLE_BORDER border and a round knob inset UI_TOGGLE_INSET
 * from the track's inner edge; the knob travels UI_TOGGLE_TRAVEL; "On" or "Off" sits
 * UI_TOGGLE_TEXT_GAP after the track in a box UI_TOGGLE_TEXT_W wide. */
#define UI_TOGGLE_W 48
#define UI_TOGGLE_H UI_SHAPE_H_TRACK
#define UI_TOGGLE_KNOB 16
#define UI_TOGGLE_BORDER UI_LW2
#define UI_TOGGLE_INSET UI_LW2
#define UI_TOGGLE_TRAVEL 24
#define UI_TOGGLE_TEXT_GAP UI_S2
#define UI_TOGGLE_TEXT_W 24

/* Info: the value is right-aligned at the row padding, at least UI_INFO_VALUE_GAP after the label.
 */
#define UI_INFO_VALUE_GAP UI_S2
/* Error status row (SPEC C08): a warning icon before the value and a rule at the row's left edge.
 */
#define UI_SETTING_ERR_ICON 20
#define UI_SETTING_ERR_ICON_GAP UI_S1
#define UI_SETTING_ERR_RULE_W UI_LW2
/* A glyph inside a row label (the armed log out): this far from the words on either side. */
#define UI_SETTING_GLYPH_GAP UI_S1
/** A disabled row is drawn at this opacity. */
#define UI_ROW_DISABLED_PCT 50

/* Choice: a chevron box, the centred value, a chevron box. */
#define UI_CHOICE_VALUE_W 224
#define UI_CHOICE_ARROW 48
#define UI_CHOICE_ARROW_ART 16
/** The chevron is drawn as a line this thick (in art pixels) with round ends. */
#define UI_CHOICE_ARROW_STROKE 1.5f
/** Chevrons of a row that is not focused. */
#define UI_CHOICE_ARROW_DIM_PCT 50

/* ============================================================================
 * C24 ScrollIndicator (ui_scroll_indicator.c)
 * ============================================================================ */

#define UI_SCROLL_W UI_LW2

/* ============================================================================
 * C15 Toast (ui_toast.c): a pill centred at the bottom of the screen, one at a time. It rises
 * UI_RISE_PX and fades in over UI_TOAST_ENTER_MS, stays until UI_TOAST_VISIBLE_MS after it was
 * shown, then fades out over UI_TOAST_EXIT_MS.
 * ============================================================================ */

#define UI_TOAST_Y 432
#define UI_TOAST_H UI_SHAPE_H_BUTTON
#define UI_TOAST_PAD UI_S3
#define UI_TOAST_ICON 24
#define UI_TOAST_ICON_GAP UI_S2
#define UI_TOAST_MAX_W 720
#define UI_TOAST_ENTER_MS UI_D2_MS
#define UI_TOAST_VISIBLE_MS 3000
#define UI_TOAST_EXIT_MS UI_D2_MS
/** Bytes kept of a toast's text (UTF-8). */
#define UI_TOAST_TEXT_MAX 96

/* ============================================================================
 * Profile page (ui_profile.c, SPEC 3.7): the identity block under the group list
 * ============================================================================ */

#define UI_IDENT_Y 376
#define UI_IDENT_H 48
#define UI_IDENT_PAD UI_S2
#define UI_IDENT_GAP UI_S2
#define UI_IDENT_AVATAR 48
#define UI_IDENT_RING UI_LW2
#define UI_IDENT_ICON 28
/** The icon inside the avatar is drawn at this opacity. */
#define UI_IDENT_ICON_PCT 85
/** Bytes kept of the PSN Account ID shown in the identity block and the Account ID row. */
#define UI_IDENT_ID_MAX UI_SETTING_VALUE_MAX

/* ============================================================================
 * C03 ConnectingRing (ui_connecting_ring.c)
 * ============================================================================ */

#define UI_RING_SIZE 128
#define UI_RING_W UI_LW2
/** The glow is baked into the ring texture, which is padded by this much on every side. */
#define UI_RING_GLOW 12
#define UI_RING_GLOW_PCT 45
/** The room icon is half the ring. */
#define UI_RING_ICON_PCT 50
#define UI_RING_HALO 280
/** The halo fades linearly from its centre to nothing at this radius (68% of the 280 box's
 * corner distance in the mock). */
#define UI_RING_HALO_FADE_R 135

/* ============================================================================
 * C16 Spinner and ProgressSteps (ui_spinner.c, ui_steps.c)
 * ============================================================================ */

#define UI_SPINNER_ARC_DEG 270
#define UI_SPINNER_BIG 176
#define UI_SPINNER_BIG_MS 1800
#define UI_SPINNER_SMALL 16
#define UI_SPINNER_SMALL_MS 800

#define UI_STEPS_X 464
#define UI_STEPS_Y 152
#define UI_STEP_PAD UI_S1
/** Marker column (dot, spinner or number), then UI_S2 before the text. */
#define UI_STEP_MARKER_W 16
#define UI_STEP_DOT 12
#define UI_STEP_LINE UI_T20_LINE
#define UI_STEP_CUR_LINE UI_T28_LINE
#define UI_STEP_DETAIL_LINE UI_T16_LINE
/** A glow behind the current step's title, reaching this far past the text. */
#define UI_STEP_GLOW 12
#define UI_STEP_GLOW_PCT 20
/** Most steps ui_draw_steps() takes (the longest flow, ui_connecting_flow.h). */
#define UI_STEPS_MAX 7

/* ============================================================================
 * C22 TextButton (ui_text_button.c)
 * ============================================================================ */

#define UI_BUTTON_H UI_SHAPE_H_BUTTON
#define UI_BUTTON_MIN_W 128
#define UI_BUTTON_PAD UI_S3
/** The small variant (SPEC 4.1 footer buttons): a 32 px pill, T16 label, UI_S2 padding, at least
 * 96 wide, with a UI_TAP_MIN high hit rect. */
#define UI_BUTTON_SMALL_H 32
#define UI_BUTTON_SMALL_MIN_W 96
#define UI_BUTTON_SMALL_PAD UI_S2
#define UI_BUTTON_PRESS_MS UI_D1_MS
#define UI_BUTTON_DISABLED_PCT 45
#define UI_BUTTON_GLOW 12
#define UI_BUTTON_GLOW_PCT 20

/* ============================================================================
 * C17 PinField (ui_pin_field.c) and the PIN screen (ui_pin.c, SPEC 3.2)
 * ============================================================================ */

/* The number of digits is UI_PIN_DIGITS (ui_pin_digits.h). */
#define UI_PIN_BOX_W 56
#define UI_PIN_BOX_H 72
#define UI_PIN_GAP 8
/** The chevron boxes above and below the focused digit: hit and visible box, then the art. */
#define UI_PIN_CHEV_W 56
#define UI_PIN_CHEV_H 48
#define UI_PIN_CHEV_ART 20
/** The chevron stroke: the mock's 2 on a 24 grid, scaled to the art. */
#define UI_PIN_CHEV_STROKE 1.7f
/** Top of the row: the up chevron box. The digit boxes follow it, then the down chevron box. */
#define UI_PIN_ROW_Y 224
#define UI_PIN_BTN_Y 424
#define UI_PIN_BTN_COUNT 3
#define UI_PIN_BTN_GAP UI_S2
#define UI_PIN_BORDER UI_LW2
#define UI_PIN_GLOW 12
#define UI_PIN_GLOW_PCT 20
/** The cursor of an empty focused box blinks over UI_PIN_BLINK_MS: full, then
 * UI_PIN_CURSOR_LOW_PCT. */
#define UI_PIN_CURSOR_W 2
#define UI_PIN_CURSOR_H 40
#define UI_PIN_BLINK_MS 1000
#define UI_PIN_CURSOR_LOW_PCT 20
/** Digits and buttons of a field that is waiting for the console. */
#define UI_PIN_LOCKED_PCT UI_BUTTON_DISABLED_PCT
#define UI_PIN_PROMPT_Y 160
/** Gap between the page title and the console name and address after it. */
#define UI_PIN_SUB_GAP UI_S1
/** Bytes kept of the console name and address line and of the prompt. */
#define UI_PIN_TEXT_MAX 160

/* ============================================================================
 * C18 QrPanel (ui_qr_panel.c): the QR art sits on a plate with UI_QR_QUIET around it
 * ============================================================================ */

#define UI_QR_BOX 176
#define UI_QR_ART 160
#define UI_QR_QUIET ((UI_QR_BOX - UI_QR_ART) / 2)

/* ============================================================================
 * Phone login (ui_profile_login.c, SPEC 3.7): the pane the PlayStation Network group shows while
 * a login runs. The title is one line, the QR panel and the steps share the next band, then the
 * Code and URL lines, then the three buttons.
 * ============================================================================ */

#define UI_LOGIN_TITLE_H 32
#define UI_LOGIN_QR_Y (UI_BODY_Y + UI_LOGIN_TITLE_H + UI_S1)
#define UI_LOGIN_STEPS_X (UI_PAGE_PANE_X + UI_QR_BOX + UI_S3)
#define UI_LOGIN_STEP_PITCH (UI_T16_LINE + UI_S1)
/** Width of the step number column, and the gap around a glyph drawn inside a step. */
#define UI_LOGIN_STEP_NUM_W 18
#define UI_LOGIN_GLYPH_GAP 4
#define UI_LOGIN_INFO_Y (UI_LOGIN_QR_Y + UI_QR_BOX + UI_S2)
#define UI_LOGIN_INFO_GAP UI_S2
#define UI_LOGIN_BUTTONS_Y 432
#define UI_LOGIN_BUTTON_GAP UI_S2

/* ============================================================================
 * Connecting screen (ui_connecting.c, SPEC 3.4)
 * ============================================================================ */

/** The art box: halo, spinner and ring are centred in it. */
#define UI_CONN_ART_X 68
#define UI_CONN_ART_Y 168
#define UI_CONN_ART_W 320
#define UI_CONN_ART_H 200
#define UI_CONN_LOGO_H 32
#define UI_CONN_LOGO_Y 384
#define UI_CONN_CANCEL_Y 440

/* ============================================================================
 * Reconnecting screen (ui_reconnecting.c, SPEC 3.4): the right column starts here, under the
 * same art box as Connecting
 * ============================================================================ */

#define UI_RECON_X UI_STEPS_X
#define UI_RECON_Y 168
#define UI_RECON_LINE_H 32
#define UI_RECON_BITRATE_H 40
/** Gap above the "Attempt" and "Please wait..." lines. */
#define UI_RECON_NOTE_GAP UI_S2
/** Bitrate shown when no recovery bitrate is set yet (the old screen's fallback). */
#define UI_RECON_DEFAULT_KBPS 800

/* ============================================================================
 * Controller page (ui_controller_page.c, SPEC C20 and section 3.8)
 * ============================================================================ */

/** The Summary page 1 front diagram box. The height keeps the art's 874 : 396 shape at this
 * width, rounded up so the art is limited by the width. */
#define UI_CTRL_FRONT_X 164
#define UI_CTRL_FRONT_Y 176
#define UI_CTRL_FRONT_W 630
#define UI_CTRL_FRONT_H 286

/** The preset switcher in the title row's right slot: chevron box, label box, chevron box, with
 * UI_CTRL_PRESET_GAP between them, ending at UI_CONTENT_RIGHT. The label is centred in its box. */
#define UI_CTRL_PRESET_LABEL_W 96
#define UI_CTRL_PRESET_GAP UI_S1

/** The L1 and R1 callouts: a UI_CTRL_CALLOUT_W box (wider when the text is) UI_CTRL_CALLOUT_H high
 * at UI_CTRL_CALLOUT_Y, the left one at UI_CTRL_CALLOUT_LEFT_X, the right one ending at
 * UI_CTRL_CALLOUT_RIGHT_EDGE. The hit rect reaches UI_CTRL_CALLOUT_HIT_PAD above and below. */
#define UI_CTRL_CALLOUT_Y 136
#define UI_CTRL_CALLOUT_H 32
#define UI_CTRL_CALLOUT_W 136
#define UI_CTRL_CALLOUT_HIT_PAD 8
#define UI_CTRL_CALLOUT_LEFT_X 64
#define UI_CTRL_CALLOUT_RIGHT_EDGE 896
#define UI_CTRL_CALLOUT_TEXT_MAX 48
/** The callout's arrow (Roboto has no U+2192, so it is a baked texture): UI_CTRL_ARROW_W x
 * UI_CTRL_ARROW_H, the head's strokes reaching UI_CTRL_ARROW_HEAD back from the tip, drawn
 * UI_CTRL_ARROW_STROKE thick. It sits one space (at T20) from the shoulder name and from the
 * output, and UI_CTRL_ARROW_DROP px below the box's middle, where the capitals' middle is. */
#define UI_CTRL_ARROW_W 16
#define UI_CTRL_ARROW_H 12
#define UI_CTRL_ARROW_HEAD 4.0f
#define UI_CTRL_ARROW_STROKE 1.5f
#define UI_CTRL_ARROW_DROP 2
/** The leader ends at the shoulder: this far from the diagram's top, and from its left (L1) or
 * right (R1) side, in percent of the diagram box. It ends in a UI_CTRL_DOT px dot. */
#define UI_CTRL_SHOULDER_X_PCT 10
#define UI_CTRL_SHOULDER_Y_PCT 10
#define UI_CTRL_DOT 6

/** The Summary page 2 rear diagram box (art 720 : 327, the height rounded up like the front's) and
 * the two zone-view boxes, front (874 : 396) and rear. */
#define UI_CTRL_REAR_X 170
#define UI_CTRL_REAR_Y 176
#define UI_CTRL_REAR_W 620
#define UI_CTRL_REAR_H 282
#define UI_CTRL_ZONE_FRONT_X 120
#define UI_CTRL_ZONE_FRONT_Y 144
#define UI_CTRL_ZONE_FRONT_W 720
#define UI_CTRL_ZONE_FRONT_H 327
#define UI_CTRL_ZONE_REAR_X 140
#define UI_CTRL_ZONE_REAR_Y 152
#define UI_CTRL_ZONE_REAR_W 680
#define UI_CTRL_ZONE_REAR_H 309

/** The footers: the preset description at the left margin, the page label at the right. The page
 * label's hit rect reaches UI_CTRL_FOOT_HIT_PAD_X to each side and UI_CTRL_FOOT_HIT_PAD_Y above
 * and below, to UI_TAP_MIN high. */
#define UI_CTRL_FOOT_Y 464
#define UI_CTRL_FOOT_H 24
#define UI_CTRL_FOOT_HIT_PAD_X 8
#define UI_CTRL_FOOT_HIT_PAD_Y 12

/** The small Clear and Whole surface buttons, centred as a row in the footer band, UI_S2 apart. */
#define UI_CTRL_FOOT_BTN_Y 460
#define UI_CTRL_FOOT_BTN_GAP UI_S2

/** In a zone view the preset name follows the title as a T16 sub, this far after the title. */
#define UI_CTRL_SUB_GAP (UI_S2 + UI_S1)
