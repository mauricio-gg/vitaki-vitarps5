#pragma once

/*
 * GH #275: debug-only stream tools. A small widget in the bottom-right corner of the stream
 * screen shows the live PIPE/DELIVERY "abs" delay figure; tapping it on the front touchscreen
 * ends the session cleanly and reconnects at once ("resync") so the felt delay before and
 * after can be compared in the log.
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

#include <chiaki/videoreceiver.h>

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

/* True when the screen-pixel point lies inside the widget rect. Shared by the drawer and the
 * touch filter so the drawn box and the swallowed area can never disagree. */
static inline bool debug_widget_contains_screen_point(int screen_x, int screen_y) {
  return screen_x >= DEBUG_WIDGET_X && screen_x < DEBUG_WIDGET_X + DEBUG_WIDGET_W &&
         screen_y >= DEBUG_WIDGET_Y && screen_y < DEBUG_WIDGET_Y + DEBUG_WIDGET_H;
}

/* Same test for a raw front-touch coordinate. */
static inline bool debug_widget_contains_touch_point(int touch_x, int touch_y) {
  return debug_widget_contains_screen_point(touch_x / DEBUG_WIDGET_TOUCH_PER_SCREEN_PX,
                                            touch_y / DEBUG_WIDGET_TOUCH_PER_SCREEN_PX);
}

/* Draws the widget; called last in the stream overlay so it sits on top of the video. */
void debug_tools_draw_widget(void);

/* UI thread, once per main-loop pass: acts on a pending widget tap and reports the
 * post-resync figures once the new session has produced them. */
void debug_tools_ui_tick(void);

/* Video callback thread: mirrors the receiver's published abs figure (lib/videoreceiver.h)
 * into UI-safe storage. NULL receiver is ignored. */
void debug_tools_publish_abs(const ChiakiVideoReceiver *receiver);

/* Video callback thread: an inter-arrival gap at or above the stall threshold ended at
 * arrival_ms (lib monotonic ms clock). */
void debug_tools_note_stall(uint64_t arrival_ms);

/* Per-session reset of the stall and abs mirrors (called with the other per-stream resets). */
void debug_tools_reset_session(void);

/* Video callback thread: the first video frame of the session arrived. */
void debug_tools_on_first_frame(void);

/* Drops the resync bookkeeping (cancel, exhausted, aborted, or no new stream). */
void debug_tools_resync_clear(void);

#endif /* VITARPS5_DEBUG_TOOLS */
