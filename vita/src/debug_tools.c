#include "debug_tools.h"

#if VITARPS5_DEBUG_TOOLS

#include "context.h"
#include "debug_input_panel.h"
#include "host_input.h"
#include "ui/ui_component.h"
#include "ui/ui_shapes.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"

#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#define DEBUG_WIDGET_TEXT_PADDING_X 10
#define DEBUG_WIDGET_TEXT "tap: resync"
#define DEBUG_INPUT_BTN_TEXT "Input test"
#define DEBUG_INPUT_DOT_R 4
#define DEBUG_INPUT_DOT_GAP 8

/* Resync bookkeeping, from an accepted tap until the new session's first frame. The UI thread
 * owns it except resync_first_frame_pending, which the video callback thread clears. */
static volatile bool resync_first_frame_pending = false;
static uint64_t resync_tap_us = 0;

volatile DebugInputSnapshot debug_input_snapshot;

void debug_tools_snapshot_reset(void) {
  debug_input_snapshot = (DebugInputSnapshot){0};
}

bool debug_tools_input_test_open(void) {
  return context.stream.input_test_open;
}

void debug_tools_on_stream_start(void) {
  context.stream.input_test_open = false;
  context.stream.input_test_toggle_request = DEBUG_PANEL_REQUEST_NONE;
  context.stream.input_test_close_requested = false;
  debug_input_panel_reset();
}

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

/* Draws the Input test button; the dot is green and the outline on while the panel is open. */
static void draw_input_button(bool open) {
  const DebugRect r = DEBUG_INPUT_BTN_RECT;
  if (open) {
    /* A 1 px LINE outline: the LINE shape, then the PANEL shape one px inside it. */
    ui_shape9_draw(UI_SHAPE9_SM, (UiRect){r.x, r.y, r.w, r.h}, UI_LINE);
    ui_shape9_draw(UI_SHAPE9_SM,
                   (UiRect){r.x + UI_LW1, r.y + UI_LW1, r.w - 2 * UI_LW1, r.h - 2 * UI_LW1},
                   UI_PANEL);
  } else {
    ui_shape9_draw(UI_SHAPE9_SM, (UiRect){r.x, r.y, r.w, r.h}, UI_PANEL);
  }
  const int dot_x = r.x + DEBUG_WIDGET_TEXT_PADDING_X;
  vita2d_draw_fill_circle((float)(dot_x + DEBUG_INPUT_DOT_R), (float)(r.y + r.h / 2),
                          (float)DEBUG_INPUT_DOT_R, open ? UI_OK : UI_TEXT_3);
  ui_text_draw_face_centered_v(UI_FACE_T16, dot_x + 2 * DEBUG_INPUT_DOT_R + DEBUG_INPUT_DOT_GAP,
                               r.y, r.h, open ? UI_TEXT : UI_TEXT_2, DEBUG_INPUT_BTN_TEXT);
}

void debug_tools_draw_widget(void) {
  if (!context.stream.is_streaming || context.stream.reconnect_overlay_active)
    return;

  const DebugRect resync = DEBUG_RESYNC_RECT;
  ui_shape9_draw(UI_SHAPE9_SM, (UiRect){resync.x, resync.y, resync.w, resync.h}, UI_PANEL);
  ui_text_draw_face_centered_v(UI_FACE_T16, resync.x + DEBUG_WIDGET_TEXT_PADDING_X, resync.y,
                               resync.h, UI_TEXT_2, DEBUG_WIDGET_TEXT);

  const bool open = context.stream.input_test_open;
  draw_input_button(open);

  /* The panel goes with the widgets, and also while a stop, teardown or resync is under way. */
  if (open && !context.stream.stop_requested && !context.stream.teardown_in_progress &&
      !resync_first_frame_pending)
    debug_input_panel_draw();
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

/* Opens or closes the Input test panel; @p how is logged ("touch" or "combo"). */
static void set_panel_open(bool open, const char *how) {
  context.stream.input_test_open = open;
  debug_input_panel_reset();
  LOGD("INPUT_TEST panel %s via %s", open ? "opened" : "closed", how);
}

void debug_tools_ui_tick(void) {
  uint8_t toggle_source = context.stream.input_test_toggle_request;
  if (toggle_source != DEBUG_PANEL_REQUEST_NONE) {
    context.stream.input_test_toggle_request = DEBUG_PANEL_REQUEST_NONE;
    set_panel_open(!context.stream.input_test_open,
                   toggle_source == DEBUG_PANEL_REQUEST_COMBO ? "combo" : "touch");
  }
  if (context.stream.input_test_close_requested) {
    context.stream.input_test_close_requested = false;
    if (context.stream.input_test_open)
      set_panel_open(false, "touch");
  }

  if (context.stream.resync_tap_requested) {
    context.stream.resync_tap_requested = false;
    handle_tap();
  }
}

#endif /* VITARPS5_DEBUG_TOOLS */
