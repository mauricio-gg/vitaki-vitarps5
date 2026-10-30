#pragma once

/*
 * GH #275: debug-only stream tools. A small widget in the bottom-right corner of the stream
 * screen reads "tap: resync"; tapping it on the front touchscreen ends the
 * session cleanly and reconnects at once ("resync"), and the log records the time from the
 * tap to the new session's first frame (PIPE/RESYNC_FIRST_FRAME).
 *
 * Everything here exists only when VITARPS5_DEBUG_TOOLS is 1 (tools/build.sh: --env testing,
 * or the "debug" command). tools/build.sh always passes an explicit -DVITARPS5_DEBUG_TOOLS=0|1;
 * the default below only covers builds that bypass it.
 */

#ifndef VITARPS5_DEBUG_TOOLS
#define VITARPS5_DEBUG_TOOLS 0
#endif

#if VITARPS5_DEBUG_TOOLS

#include <stdbool.h>
#include <stdint.h>

#include "input_test_click.h"

/* Widget rectangle in screen pixels (960x544). */
#define DEBUG_WIDGET_SCREEN_WIDTH 960
#define DEBUG_WIDGET_SCREEN_HEIGHT 544
#define DEBUG_WIDGET_MARGIN 6
#define DEBUG_WIDGET_W 190
#define DEBUG_WIDGET_H 26
#define DEBUG_WIDGET_X (DEBUG_WIDGET_SCREEN_WIDTH - DEBUG_WIDGET_W - DEBUG_WIDGET_MARGIN)
#define DEBUG_WIDGET_Y (DEBUG_WIDGET_SCREEN_HEIGHT - DEBUG_WIDGET_H - DEBUG_WIDGET_MARGIN)

/* Front touch coordinates (0..1919 x 0..1087) are 2x screen pixels. */
#define DEBUG_WIDGET_TOUCH_PER_SCREEN_PX 2

/* GH #458: the Input test button sits INPUT_BTN_GAP left of the resync widget. */
#define DEBUG_INPUT_BTN_GAP 6
#define DEBUG_INPUT_BTN_W 120
#define DEBUG_INPUT_BTN_H 26
#define DEBUG_INPUT_BTN_X (DEBUG_WIDGET_X - DEBUG_INPUT_BTN_GAP - DEBUG_INPUT_BTN_W)
#define DEBUG_INPUT_BTN_Y DEBUG_WIDGET_Y

/* Both buttons take touches in an invisible box as wide as the button itself and 48 px tall,
 * reaching the bottom of the screen. The boxes never reach past their button's own x-span, so the
 * gap between the buttons and the right 6 px of the screen's bottom edge stay with the game. */
#define DEBUG_TAP_BOX_H 48
#define DEBUG_TAP_BOX_Y (DEBUG_WIDGET_SCREEN_HEIGHT - DEBUG_TAP_BOX_H)

/* The panel, docked left, and the Close pill in its 48 px header (16 px side padding). */
#define DEBUG_PANEL_X 8
#define DEBUG_PANEL_Y 8
#define DEBUG_PANEL_W 384
#define DEBUG_PANEL_H 528
#define DEBUG_PANEL_HEADER_H 48
#define DEBUG_PANEL_PAD_X 16
#define DEBUG_CLOSE_W 88
#define DEBUG_CLOSE_H 32
#define DEBUG_CLOSE_X (DEBUG_PANEL_X + DEBUG_PANEL_W - DEBUG_PANEL_PAD_X - DEBUG_CLOSE_W)
#define DEBUG_CLOSE_Y (DEBUG_PANEL_Y + (DEBUG_PANEL_HEADER_H - DEBUG_CLOSE_H) / 2)
#define DEBUG_CLOSE_TAP_PAD 8

/* A rectangle in screen pixels. */
typedef struct debug_rect_t {
  int x, y, w, h;
} DebugRect;

/* The visible boxes, which the drawer uses. */
#define DEBUG_RESYNC_RECT \
  ((DebugRect){DEBUG_WIDGET_X, DEBUG_WIDGET_Y, DEBUG_WIDGET_W, DEBUG_WIDGET_H})
#define DEBUG_INPUT_BTN_RECT \
  ((DebugRect){DEBUG_INPUT_BTN_X, DEBUG_INPUT_BTN_Y, DEBUG_INPUT_BTN_W, DEBUG_INPUT_BTN_H})
#define DEBUG_PANEL_RECT ((DebugRect){DEBUG_PANEL_X, DEBUG_PANEL_Y, DEBUG_PANEL_W, DEBUG_PANEL_H})
#define DEBUG_CLOSE_RECT ((DebugRect){DEBUG_CLOSE_X, DEBUG_CLOSE_Y, DEBUG_CLOSE_W, DEBUG_CLOSE_H})

/* The areas the touch filter swallows, derived from the constants above. */
#define DEBUG_RESYNC_TAP_RECT \
  ((DebugRect){DEBUG_WIDGET_X, DEBUG_TAP_BOX_Y, DEBUG_WIDGET_W, DEBUG_TAP_BOX_H})
#define DEBUG_INPUT_TAP_RECT \
  ((DebugRect){DEBUG_INPUT_BTN_X, DEBUG_TAP_BOX_Y, DEBUG_INPUT_BTN_W, DEBUG_TAP_BOX_H})
#define DEBUG_CLOSE_TAP_RECT                                                             \
  ((DebugRect){DEBUG_CLOSE_X - DEBUG_CLOSE_TAP_PAD, DEBUG_CLOSE_Y - DEBUG_CLOSE_TAP_PAD, \
               DEBUG_CLOSE_W + 2 * DEBUG_CLOSE_TAP_PAD, DEBUG_CLOSE_H + 2 * DEBUG_CLOSE_TAP_PAD})

/* True when the screen-pixel point lies inside @p r. */
static inline bool debug_rect_contains(DebugRect r, int screen_x, int screen_y) {
  return screen_x >= r.x && screen_x < r.x + r.w && screen_y >= r.y && screen_y < r.y + r.h;
}

/* What a touch that starts at a raw front-touch coordinate belongs to. */
typedef enum debug_touch_target_t {
  DEBUG_TOUCH_TARGET_NONE,        /* the game's touch: sent as usual */
  DEBUG_TOUCH_TARGET_RESYNC,      /* swallowed; requests a resync */
  DEBUG_TOUCH_TARGET_INPUT_TEST,  /* swallowed; requests a panel toggle */
  DEBUG_TOUCH_TARGET_PANEL_CLOSE, /* swallowed; requests a panel close (panel open only) */
} DebugTouchTarget;

/* Classifies a touch-down. Shared by the touch filter and the tests; the drawer uses the visible
 * rects above, which the tap boxes are derived from. The panel body is deliberately NOT a target:
 * touches under it still reach the game. */
static inline DebugTouchTarget debug_touch_target_at(int touch_x, int touch_y, bool panel_open) {
  int sx = touch_x / DEBUG_WIDGET_TOUCH_PER_SCREEN_PX;
  int sy = touch_y / DEBUG_WIDGET_TOUCH_PER_SCREEN_PX;
  if (debug_rect_contains(DEBUG_RESYNC_TAP_RECT, sx, sy))
    return DEBUG_TOUCH_TARGET_RESYNC;
  if (debug_rect_contains(DEBUG_INPUT_TAP_RECT, sx, sy))
    return DEBUG_TOUCH_TARGET_INPUT_TEST;
  if (panel_open && debug_rect_contains(DEBUG_CLOSE_TAP_RECT, sx, sy))
    return DEBUG_TOUCH_TARGET_PANEL_CLOSE;
  return DEBUG_TOUCH_TARGET_NONE;
}

/* Where a toggle request came from (stored in stream.input_test_toggle_request). */
#define DEBUG_PANEL_REQUEST_NONE 0
#define DEBUG_PANEL_REQUEST_TOUCH 1
#define DEBUG_PANEL_REQUEST_COMBO 2

/* L + R + Triangle held for INPUT_COMBO_HOLD_MS (input_combos.h) toggles the panel. */

/* What the input thread last SENT, plus the raw motion sample of the same pass, for the panel.
 * Written by the input thread once per pass, read by the UI thread with no lock. Torn reads
 * between fields are acceptable for a debug panel (every field is a naturally aligned 32 or
 * 64-bit value). Touch coordinates are in the 1920x942 space Chiaki sends. */
#define DEBUG_SNAPSHOT_TOUCHES 2
typedef struct debug_input_snapshot_t {
  struct {
    bool active; /* a finger is sent for this slot */
    uint16_t x, y;
  } touches[DEBUG_SNAPSHOT_TOUCHES];
  float sent_accel[3]; /* after the deadband: what is sent */
  float sent_gyro[3];
  float raw_accel[3]; /* sceMotion, this pass */
  float raw_gyro[3];
  InputTestClick click;
  uint32_t combo_held_ms; /* L+R+Triangle held so far (0 when not held), capped at the threshold */
} DebugInputSnapshot;

/* Written by the input thread, read by the UI thread; see DebugInputSnapshot. */
extern volatile DebugInputSnapshot debug_input_snapshot;

/* Zeroes the snapshot; the input thread calls it when it starts (a new stream). */
void debug_tools_snapshot_reset(void);

/* True while the Input test panel is open. Owned by the UI thread; the input thread reads it. */
bool debug_tools_input_test_open(void);

/* Per-stream hook (from vitavideo_overlay_on_stream_start): the panel starts closed. */
void debug_tools_on_stream_start(void);

/* Draws the widget; called last in the stream overlay so it sits on top of the video. */
void debug_tools_draw_widget(void);

/* UI thread, once per main-loop pass: acts on a pending widget tap. */
void debug_tools_ui_tick(void);

/* Video callback thread: the first video frame of the session arrived. */
void debug_tools_on_first_frame(void);

/* Drops the resync bookkeeping (cancel, exhausted, aborted, or no new stream). */
void debug_tools_resync_clear(void);

#endif /* VITARPS5_DEBUG_TOOLS */
