#include "video_overlay.h"

#include "context.h"
#include "debug_tools.h"
#include "ui.h"
#include "ui/ui_component.h"
#include "ui/ui_draw_stats.h"
#include "ui/ui_pill.h"
#include "ui/ui_shapes.h"
#include "ui/ui_stream_stats.h"
#include "ui/ui_theme.h"
#include "ui/ui_text.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

enum {
  SCREEN_WIDTH = 960,
  SCREEN_HEIGHT = 544,
};

static bool poor_net_active = false;
static uint64_t stream_exit_hint_start_us = 0;

#define POOR_NET_TEXT "Network Unstable"

/** Width of the Network Unstable pill; measured once on first use, 0 until then. */
static int poor_net_width = 0;

/** Alert length used when the stream has not set one (the Network Alerts default, 5 s). */
#define POOR_NET_DEFAULT_DURATION_US (5 * 1000 * 1000ULL)

/**
 * Draw the Network Unstable pill (C19 unstable, SPEC 3.5) at the bottom right while the alert
 * deadline is in the future. It fades linearly over the alert duration through the layer
 * opacity (restored to 1 before returning), on top of the pill's own pulse.
 */
static void draw_indicators(void) {
  if (!poor_net_active)
    return;

  uint64_t now_us = sceKernelGetProcessTimeWide();
  if (!context.stream.loss_alert_until_us || now_us >= context.stream.loss_alert_until_us) {
    poor_net_active = false;
    return;
  }

  uint64_t duration = context.stream.loss_alert_duration_us ? context.stream.loss_alert_duration_us
                                                            : POOR_NET_DEFAULT_DURATION_US;
  float opacity = (float)(context.stream.loss_alert_until_us - now_us) / (float)duration;
  if (opacity > 1.0f)
    opacity = 1.0f;

  if (poor_net_width == 0)
    poor_net_width = ui_pill_width(UI_PILL_UNSTABLE, POOR_NET_TEXT);

  ui_layer_set_alpha(opacity);
  ui_pill_draw(UI_PILL_UNSTABLE, SCREEN_WIDTH - UI_STREAM_OVERLAY_MARGIN - poor_net_width,
               SCREEN_HEIGHT - UI_STREAM_OVERLAY_MARGIN - UI_PILL_H, poor_net_width, POOR_NET_TEXT);
  ui_layer_set_alpha(1.0f);
}

/** Parts of the exit hint pill: "Back to menu: Hold [L] + [R] + [Start]". */
static UiPillPart exit_hint_parts[] = {
    {.text = "Back to menu: Hold "},
    {.glyph = UI_BTN_L},
    {.text = " + "},
    {.glyph = UI_BTN_R},
    {.text = " + "},
    {.glyph = UI_BTN_FILTER},
};
#define EXIT_HINT_PART_COUNT ((int)(sizeof(exit_hint_parts) / sizeof(exit_hint_parts[0])))

/** Width of the exit hint pill; measured once on first use, 0 until then. */
static int exit_hint_width = 0;

/**
 * Draw the exit hint (C19 plain, SPEC 3.5): visible for UI_STREAM_HINT_VISIBLE_MS from the first
 * frame of the stream, then a linear fade over UI_STREAM_HINT_FADE_MS. The fade is the layer
 * opacity, restored to 1 before returning.
 */
static void draw_stream_exit_hint(void) {
  if (!context.config.show_stream_exit_hint)
    return;

  uint64_t now_us = sceKernelGetProcessTimeWide();
  if (stream_exit_hint_start_us == 0) {
    stream_exit_hint_start_us = now_us;
  }

  const uint64_t visible_us = UI_STREAM_HINT_VISIBLE_MS * 1000ULL;
  const uint64_t fade_us = UI_STREAM_HINT_FADE_MS * 1000ULL;
  uint64_t elapsed_us = now_us - stream_exit_hint_start_us;
  if (elapsed_us >= visible_us + fade_us) {
    return;
  }

  float opacity = 1.0f;
  if (elapsed_us > visible_us) {
    opacity = 1.0f - (float)(elapsed_us - visible_us) / (float)fade_us;
  }

  if (exit_hint_width == 0) {
    exit_hint_width = ui_pill_plain_layout(exit_hint_parts, EXIT_HINT_PART_COUNT);
  }

  ui_layer_set_alpha(opacity);
  ui_pill_plain_draw(SCREEN_WIDTH - UI_STREAM_OVERLAY_MARGIN - exit_hint_width,
                     UI_STREAM_OVERLAY_MARGIN, exit_hint_width, exit_hint_parts,
                     EXIT_HINT_PART_COUNT);
  ui_layer_set_alpha(1.0f);
}

/** Row labels of the stats panel, top to bottom; the value strings in StatsCache follow this order.
 */
static const char *const stats_labels[UI_STATS_ROWS] = {"Latency", "FPS"};
static const char *const stats_title = "Stream Stats";

/**
 * The stats panel's text and measurements. Rebuilt at most once per UI_STATS_REBUILD_US so the
 * numbers do not flicker and the draw path never formats or measures text.
 */
typedef struct {
  bool valid;      /**< False until the first build after a stream start. */
  bool latency_na; /**< The latency row already reads N/A, so no stale check is needed. */
  uint64_t built_us;
  char values[UI_STATS_ROWS][32];
  int value_w[UI_STATS_ROWS];
  int label_w[UI_STATS_ROWS];
  int panel_w;
} StatsCache;

static StatsCache stats_cache;

/** Measure the cached texts and set the panel width: the widest row or the title, never under
 * UI_STATS_MIN_W. */
static void stats_cache_measure(void) {
  int content_w = ui_text_face_width(UI_FACE_T16, stats_title);
  for (int i = 0; i < UI_STATS_ROWS; i++) {
    stats_cache.label_w[i] = ui_text_face_width(UI_FACE_T16, stats_labels[i]);
    stats_cache.value_w[i] = ui_text_face_width(UI_FACE_T16, stats_cache.values[i]);
    int row_w = stats_cache.label_w[i] + UI_STATS_COL_GAP + stats_cache.value_w[i];
    if (row_w > content_w)
      content_w = row_w;
  }
  stats_cache.panel_w = content_w + UI_STATS_PAD_X * 2;
  if (stats_cache.panel_w < UI_STATS_MIN_W)
    stats_cache.panel_w = UI_STATS_MIN_W;
}

/**
 * Keep the stats cache current. Rebuilds from the live stream metrics on the first call after a
 * reset and then once per UI_STATS_REBUILD_US. Between rebuilds only the stale check runs: the
 * frame the latency crosses UI_STREAM_STATS_STALE_US it switches to N/A at once.
 */
static void stats_cache_update(uint64_t now_us) {
  if (!stats_cache.valid || now_us - stats_cache.built_us >= UI_STATS_REBUILD_US) {
    ui_stream_stats_format(now_us, context.stream.metrics_last_update_us,
                           context.stream.measured_rtt_ms, context.stream.measured_incoming_fps,
                           context.stream.target_fps, context.stream.negotiated_fps,
                           stats_cache.values[0], sizeof(stats_cache.values[0]),
                           stats_cache.values[1], sizeof(stats_cache.values[1]));
    stats_cache.latency_na = strcmp(stats_cache.values[0], UI_STREAM_STATS_NA) == 0;
    stats_cache.built_us = now_us;
    stats_cache.valid = true;
    stats_cache_measure();
    return;
  }

  if (!stats_cache.latency_na &&
      ui_stream_stats_metrics_stale(now_us, context.stream.metrics_last_update_us)) {
    snprintf(stats_cache.values[0], sizeof(stats_cache.values[0]), "%s", UI_STREAM_STATS_NA);
    stats_cache.latency_na = true;
    stats_cache_measure();
  }
}

/**
 * Draw the stream stats panel (C25, SPEC 3.5) in its fixed slot. Paper cost: 9-slice 9, title 1,
 * one run per label and per value (14 in all).
 */
static void draw_stream_stats_panel(void) {
  if (!context.config.show_latency) {
    stats_cache.valid = false;  // switching it back on builds the text on the first frame
    return;
  }

  stats_cache_update(sceKernelGetProcessTimeWide());

  const int box_w = stats_cache.panel_w;
  const int box_h =
      UI_STATS_PAD_Y * 2 + UI_STATS_LINE_H + UI_STATS_TITLE_GAP + UI_STATS_ROWS * UI_STATS_LINE_H;
  const int box_x = SCREEN_WIDTH - UI_STREAM_OVERLAY_MARGIN - box_w;
  UiRect box = {box_x, UI_STATS_TOP, box_w, box_h};
  ui_shape9_draw(UI_SHAPE9_MD, box, UI_PANEL);

  const int text_x = box_x + UI_STATS_PAD_X;
  int line_y = UI_STATS_TOP + UI_STATS_PAD_Y;
  ui_text_draw_face_centered_v(UI_FACE_T16, text_x, line_y, UI_STATS_LINE_H, UI_TEXT_2,
                               stats_title);
  line_y += UI_STATS_LINE_H + UI_STATS_TITLE_GAP;
  for (int i = 0; i < UI_STATS_ROWS; i++) {
    ui_text_draw_face_centered_v(UI_FACE_T16, text_x, line_y, UI_STATS_LINE_H, UI_TEXT_3,
                                 stats_labels[i]);
    ui_text_draw_face_centered_v(UI_FACE_T16,
                                 box_x + box_w - UI_STATS_PAD_X - stats_cache.value_w[i], line_y,
                                 UI_STATS_LINE_H, UI_TEXT, stats_cache.values[i]);
    line_y += UI_STATS_LINE_H;
  }
}

void vitavideo_overlay_render(void) {
  UI_DRAW_STATS_FRAME_BEGIN();
  draw_stream_exit_hint();
  draw_stream_stats_panel();
  draw_indicators();
#if VITARPS5_DEBUG_TOOLS
  debug_tools_draw_widget();
#endif
  UI_DRAW_STATS_FRAME_END("overlay");
}

void vitavideo_overlay_on_stream_start(void) {
  stream_exit_hint_start_us = 0;
  memset(&stats_cache, 0, sizeof(stats_cache));
}

void vitavideo_overlay_on_stream_stop(void) {
  stream_exit_hint_start_us = 0;
  memset(&stats_cache, 0, sizeof(stats_cache));
}

void vitavideo_overlay_show_poor_net_indicator(void) {
  if (!context.config.show_network_indicator)
    return;
  uint64_t now_us = sceKernelGetProcessTimeWide();
  if (now_us - context.stream.net_unstable_last_activated_us < 500000ULL)
    return;
  context.stream.net_unstable_last_activated_us = now_us;
  LOGD("PIPE/NET_UNSTABLE activated");
  poor_net_active = true;
}

void vitavideo_overlay_hide_poor_net_indicator(void) {
  poor_net_active = false;
}
