#include "debug_tools.h"

#if VITARPS5_DEBUG_TOOLS

#include "context.h"
#include "host_input.h"
#include "ui/ui_constants.h"
#include "ui/ui_graphics.h"
#include "ui/ui_text.h"

#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#define DEBUG_WIDGET_CORNER_RADIUS 8
#define DEBUG_WIDGET_TEXT_PADDING_X 10
#define DEBUG_WIDGET_BG_COLOR RGBA8(20, 20, 24, 220)
#define DEBUG_WIDGET_TEXT_COLOR RGBA8(0xD8, 0xE8, 0xFF, 255)
#define DEBUG_WIDGET_TEXT "tap: resync"

extern vita2d_font *font;

/* Resync bookkeeping, from an accepted tap until the new session's first frame. The UI thread
 * owns it except resync_first_frame_pending, which the video callback thread clears. */
static volatile bool resync_first_frame_pending = false;
static uint64_t resync_tap_us = 0;

void debug_tools_on_first_frame(void) {
  if (!resync_first_frame_pending)
    return;
  resync_first_frame_pending = false;
  uint64_t ttff_ms = (sceKernelGetProcessTimeWide() - resync_tap_us) / 1000ULL;
  LOGD("PIPE/RESYNC_FIRST_FRAME ttff_ms=%llu", (unsigned long long)ttff_ms);
}

void debug_tools_resync_clear(void) {
  resync_first_frame_pending = false;
}

void debug_tools_draw_widget(void) {
  if (!context.stream.is_streaming || context.stream.reconnect_overlay_active)
    return;

  ui_draw_card_with_shadow(DEBUG_WIDGET_X, DEBUG_WIDGET_Y, DEBUG_WIDGET_W, DEBUG_WIDGET_H,
                           DEBUG_WIDGET_CORNER_RADIUS, DEBUG_WIDGET_BG_COLOR);
  ui_text_draw_centered_v(font, DEBUG_WIDGET_X + DEBUG_WIDGET_TEXT_PADDING_X, DEBUG_WIDGET_Y,
                          DEBUG_WIDGET_H, DEBUG_WIDGET_TEXT_COLOR, FONT_SIZE_SMALL,
                          DEBUG_WIDGET_TEXT);
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

  LOGD("PIPE/RESYNC_TAP");

  resync_tap_us = sceKernelGetProcessTimeWide();
  resync_first_frame_pending = true;
  if (!host_request_stream_resync()) {
    LOGD("PIPE/RESYNC_TAP ignored reason=\"stop was not accepted\"");
    debug_tools_resync_clear();
  }
}

void debug_tools_ui_tick(void) {
  if (context.stream.resync_tap_requested) {
    context.stream.resync_tap_requested = false;
    handle_tap();
  }
}

#endif /* VITARPS5_DEBUG_TOOLS */
