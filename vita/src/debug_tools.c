#include "debug_tools.h"

#if VITARPS5_DEBUG_TOOLS

#include "context.h"
#include "host_input.h"
#include "ui/ui_constants.h"
#include "ui/ui_graphics.h"
#include "ui/ui_text.h"

#include <chiaki/time.h>

#include <stdio.h>

#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#define DEBUG_WIDGET_CORNER_RADIUS 8
#define DEBUG_WIDGET_TEXT_PADDING_X 10
#define DEBUG_WIDGET_TEXT_CAPACITY 40
#define DEBUG_WIDGET_BG_COLOR RGBA8(20, 20, 24, 220)
#define DEBUG_WIDGET_TEXT_COLOR RGBA8(0xD8, 0xE8, 0xFF, 255)
#define DEBUG_ABS_STR_CAPACITY 16
#define DEBUG_ABS_UNKNOWN "na"

extern vita2d_font *font;

/* Mirror of the receiver's published abs figure. Single writer: the video callback thread
 * (debug_tools_publish_abs), reset by the stream reset; readers: the UI thread. One aligned
 * 32-bit read per field, no locks. abs_windows == 0 means nothing published yet. */
static volatile int32_t abs_ms = 0;
static volatile int32_t abs_first_ms = 0;
static volatile uint32_t abs_windows = 0;

/* Last stall (inter-arrival gap at or above the stale tracker threshold) of this session, as
 * the low 32 bits of the lib monotonic ms clock. Single writer: the video callback thread. */
static volatile uint32_t stall_last_ms = 0;
static volatile bool stall_seen = false;

/* Widget text, rebuilt on the UI thread only when abs_windows changes (about once a second). */
static char widget_text[DEBUG_WIDGET_TEXT_CAPACITY] = "abs -- ms  tap: resync";
static uint32_t widget_text_windows = 0;

/* Resync bookkeeping, from an accepted tap until the new session's first abs window. The UI
 * thread owns it except resync_first_frame_pending, which the video callback thread clears. */
static bool resync_active = false;
static volatile bool resync_first_frame_pending = false;
static bool resync_abs_pending = false;
static uint64_t resync_tap_us = 0;
static int32_t resync_abs_before_ms = 0;
static bool resync_abs_before_known = false;

/* Formats value into out, or DEBUG_ABS_UNKNOWN when no window has been published. */
static const char *format_abs(char *out, size_t out_size, bool known, int32_t value) {
  if (!known)
    return DEBUG_ABS_UNKNOWN;
  snprintf(out, out_size, "%d", (int)value);
  return out;
}

void debug_tools_publish_abs(const ChiakiVideoReceiver *receiver) {
  if (!receiver)
    return;
  uint32_t windows = receiver->published_windows;
  if (windows == abs_windows)
    return;
  abs_first_ms = receiver->published_abs_first_ms;
  abs_ms = receiver->published_abs_ms;
  abs_windows = windows;
}

void debug_tools_note_stall(uint64_t arrival_ms) {
  stall_last_ms = (uint32_t)arrival_ms;
  stall_seen = true;
}

void debug_tools_reset_session(void) {
  stall_seen = false;
  abs_windows = 0;
  abs_ms = 0;
  abs_first_ms = 0;
}

void debug_tools_on_first_frame(void) {
  if (!resync_first_frame_pending)
    return;
  resync_first_frame_pending = false;
  uint64_t ttff_ms = (sceKernelGetProcessTimeWide() - resync_tap_us) / 1000ULL;
  LOGD("PIPE/RESYNC_FIRST_FRAME ttff_ms=%llu", (unsigned long long)ttff_ms);
}

void debug_tools_resync_clear(void) {
  resync_active = false;
  resync_first_frame_pending = false;
  resync_abs_pending = false;
}

void debug_tools_draw_widget(void) {
  if (!context.stream.is_streaming || context.stream.reconnect_overlay_active)
    return;

  uint32_t windows = abs_windows;
  if (windows != widget_text_windows) {
    if (windows == 0) {
      snprintf(widget_text, sizeof(widget_text), "abs -- ms  tap: resync");
    } else {
      snprintf(widget_text, sizeof(widget_text), "abs %d ms  tap: resync", (int)abs_ms);
    }
    widget_text_windows = windows;
  }

  ui_draw_card_with_shadow(DEBUG_WIDGET_X, DEBUG_WIDGET_Y, DEBUG_WIDGET_W, DEBUG_WIDGET_H,
                           DEBUG_WIDGET_CORNER_RADIUS, DEBUG_WIDGET_BG_COLOR);
  ui_text_draw_centered_v(font, DEBUG_WIDGET_X + DEBUG_WIDGET_TEXT_PADDING_X, DEBUG_WIDGET_Y,
                          DEBUG_WIDGET_H, DEBUG_WIDGET_TEXT_COLOR, FONT_SIZE_SMALL, widget_text);
}

/* Returns why a tap must be dropped right now, or NULL when a resync may start. A resync needs a
 * live, flowing stream and no stop, teardown, soft restart or recovery already under way. */
static const char *tap_ignore_reason(void) {
  if (!context.stream.session_init || !context.stream.is_streaming ||
      !context.stream.video_first_frame_logged)
    return "not streaming";
  if (context.stream.stop_requested || context.stream.teardown_in_progress)
    return "stop or teardown in progress";
  if (context.stream.fast_restart_active)
    return "soft restart in progress";
  if (context.stream.recovery_active || context.stream.reconnect_overlay_active)
    return "reconnect in progress";
  return NULL;
}

/* Handles one widget tap on the UI thread. */
static void handle_tap(void) {
  const char *ignore_reason = tap_ignore_reason();
  if (ignore_reason) {
    LOGD("PIPE/RESYNC_TAP ignored reason=\"%s\"", ignore_reason);
    return;
  }

  bool known = abs_windows != 0;
  int32_t abs_now_ms = abs_ms;
  char now_str[DEBUG_ABS_STR_CAPACITY];
  char first_str[DEBUG_ABS_STR_CAPACITY];
  long long since_stall_ms = -1;
  if (stall_seen) {
    since_stall_ms = (long long)((uint32_t)chiaki_time_now_monotonic_ms() - stall_last_ms);
  }
  LOGD("PIPE/RESYNC_TAP abs_ms=%s abs_start_ms=%s since_last_stall_ms=%lld",
       format_abs(now_str, sizeof(now_str), known, abs_now_ms),
       format_abs(first_str, sizeof(first_str), known, abs_first_ms), since_stall_ms);

  resync_tap_us = sceKernelGetProcessTimeWide();
  resync_abs_before_ms = abs_now_ms;
  resync_abs_before_known = known;
  resync_active = true;
  resync_abs_pending = true;
  resync_first_frame_pending = true;
  if (!host_request_stream_resync()) {
    LOGD("PIPE/RESYNC_TAP ignored reason=\"stop was not accepted\"");
    debug_tools_resync_clear();
  }
}

void debug_tools_ui_tick(void) {
  // The new session's first abs window lands about a second after its first frame; only count
  // windows once that first frame arrived, so the old session's last figure can never match.
  if (resync_abs_pending && !resync_first_frame_pending && abs_windows != 0) {
    char before_str[DEBUG_ABS_STR_CAPACITY];
    uint64_t since_tap_ms = (sceKernelGetProcessTimeWide() - resync_tap_us) / 1000ULL;
    LOGD("PIPE/RESYNC_ABS abs_before_ms=%s abs_after_ms=%d since_tap_ms=%llu",
         format_abs(before_str, sizeof(before_str), resync_abs_before_known, resync_abs_before_ms),
         (int)abs_ms, (unsigned long long)since_tap_ms);
    debug_tools_resync_clear();
  }

  if (context.stream.resync_tap_requested) {
    context.stream.resync_tap_requested = false;
    handle_tap();
  }
}

#endif /* VITARPS5_DEBUG_TOOLS */
