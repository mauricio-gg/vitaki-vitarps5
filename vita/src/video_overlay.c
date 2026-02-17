#include "video_overlay.h"

#include "context.h"
#include "ui.h"
#include "ui/ui_graphics.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#define VIDEO_LOSS_ALERT_DEFAULT_US (5 * 1000 * 1000ULL)
#define STREAM_EXIT_HINT_VISIBLE_US (5 * 1000 * 1000ULL)
#define STREAM_EXIT_HINT_FADE_US    (500 * 1000ULL)

enum {
  SCREEN_WIDTH = 960,
  SCREEN_HEIGHT = 544,
};

typedef struct {
  bool activated;
  uint8_t alpha;
  bool plus;
} indicator_status;

static indicator_status poor_net_indicator = {0};
static uint64_t stream_exit_hint_start_us = 0;
static bool stream_exit_hint_visible_this_frame = false;

extern vita2d_font* font;

static void draw_pill(int x, int y, int width, int height, uint32_t color) {
  if (height <= 0 || width <= 0)
    return;

  int radius = height / 2;
  if (radius <= 0) {
    vita2d_draw_rectangle(x, y, width, height, color);
    return;
  }

  if (radius * 2 > width)
    radius = width / 2;

  int body_width = width - 2 * radius;
  if (body_width > 0)
    vita2d_draw_rectangle(x + radius, y, body_width, height, color);

  int center_y = y + radius;
  int radius_sq = radius * radius;
  for (int py = 0; py < height; ++py) {
    int dy = (y + py) - center_y;
    int inside = radius_sq - dy * dy;
    if (inside <= 0)
      continue;
    int dx = (int)ceilf(sqrtf((float)inside));
    if (dx <= 0)
      continue;

    vita2d_draw_rectangle(x + radius - dx, y + py, dx, 1, color);
    vita2d_draw_rectangle(x + width - radius, y + py, dx, 1, color);
  }
}

static void draw_indicators(void) {
  if (!poor_net_indicator.activated)
    return;

  uint64_t now_us = sceKernelGetProcessTimeWide();
  if (!context.stream.loss_alert_until_us ||
      now_us >= context.stream.loss_alert_until_us) {
    poor_net_indicator.activated = false;
    return;
  }

  uint64_t duration = context.stream.loss_alert_duration_us ?
      context.stream.loss_alert_duration_us : VIDEO_LOSS_ALERT_DEFAULT_US;
  uint64_t remaining = context.stream.loss_alert_until_us - now_us;
  float alpha_ratio = duration ? (float)remaining / (float)duration : 0.0f;
  if (alpha_ratio < 0.0f)
    alpha_ratio = 0.0f;
  uint8_t alpha = (uint8_t)(alpha_ratio * 255.0f);

  const int margin = 18;
  const int dot_radius = 6;
  const int padding_x = 18;
  const int padding_y = 6;
  const char *headline = "Network Unstable";
  int text_width = vita2d_font_text_width(font, FONT_SIZE_SMALL, headline);
  int box_w = padding_x * 2 + dot_radius * 2 + 10 + text_width;
  int box_h = padding_y * 2 + FONT_SIZE_SMALL + 4;
  int box_x = SCREEN_WIDTH - box_w - margin;
  int box_y = SCREEN_HEIGHT - box_h - margin;

  uint8_t bg_alpha = (uint8_t)(alpha_ratio * 200.0f);
  if (bg_alpha < 30)
    bg_alpha = 30;
  uint32_t bg_color = RGBA8(0, 0, 0, bg_alpha);
  draw_pill(box_x, box_y, box_w, box_h, bg_color);

  int dot_x = box_x + padding_x;
  int dot_y = box_y + box_h / 2;
  vita2d_draw_fill_circle(dot_x, dot_y, dot_radius, RGBA8(0xF4, 0x43, 0x36, alpha));

  int text_x = dot_x + dot_radius + 10;
  int text_y = box_y + box_h / 2 + (FONT_SIZE_SMALL / 2) - 2;
  vita2d_font_draw_text(font, text_x, text_y,
                        RGBA8(0xFF, 0xFF, 0xFF, alpha), FONT_SIZE_SMALL, headline);
}

static void draw_stream_exit_hint(void) {
  stream_exit_hint_visible_this_frame = false;
  if (!context.config.show_stream_exit_hint)
    return;

  uint64_t now_us = sceKernelGetProcessTimeWide();
  if (stream_exit_hint_start_us == 0) {
    stream_exit_hint_start_us = now_us;
  }

  uint64_t elapsed_us = now_us - stream_exit_hint_start_us;
  uint64_t total_visible_us = STREAM_EXIT_HINT_VISIBLE_US + STREAM_EXIT_HINT_FADE_US;
  if (elapsed_us >= total_visible_us) {
    return;
  }

  float alpha_ratio = 1.0f;
  if (elapsed_us > STREAM_EXIT_HINT_VISIBLE_US) {
    uint64_t fade_elapsed_us = elapsed_us - STREAM_EXIT_HINT_VISIBLE_US;
    if (STREAM_EXIT_HINT_FADE_US > 0) {
      alpha_ratio = 1.0f - ((float)fade_elapsed_us / (float)STREAM_EXIT_HINT_FADE_US);
    } else {
      alpha_ratio = 0.0f;
    }
    if (alpha_ratio < 0.0f)
      alpha_ratio = 0.0f;
    if (alpha_ratio > 1.0f)
      alpha_ratio = 1.0f;
  }

  const int margin = 18;
  const int padding_x = 14;
  const int padding_y = 7;
  const char *hint = "Back to menu: Hold L + R + Start";
  int text_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, hint);
  int box_w = text_w + (padding_x * 2);
  int box_h = FONT_SIZE_SMALL + (padding_y * 2) + 4;
  int box_x = SCREEN_WIDTH - box_w - margin;
  int box_y = margin;

  uint8_t bg_alpha = (uint8_t)(180.0f * alpha_ratio);
  uint8_t text_alpha = (uint8_t)(240.0f * alpha_ratio);
  draw_pill(box_x, box_y, box_w, box_h, RGBA8(0, 0, 0, bg_alpha));
  vita2d_font_draw_text(font, box_x + padding_x, box_y + box_h - padding_y - 2,
                        RGBA8(0xFF, 0xFF, 0xFF, text_alpha), FONT_SIZE_SMALL, hint);
  stream_exit_hint_visible_this_frame = true;
}

static void draw_stream_stats_panel(void) {
  if (!context.config.show_latency)
    return;

  char latency_value[32] = "N/A";
  char fps_value[32] = "N/A";
  const char *labels[] = {"Latency", "FPS"};
  const char *values[] = {latency_value, fps_value};
  const int row_count = 2;

  uint64_t now_us = sceKernelGetProcessTimeWide();
  bool metrics_recent = context.stream.metrics_last_update_us != 0 &&
                        (now_us - context.stream.metrics_last_update_us) <= 3000000ULL;
  if (metrics_recent && context.stream.measured_rtt_ms > 0) {
    snprintf(latency_value, sizeof(latency_value), "%u ms", context.stream.measured_rtt_ms);
  }

  uint32_t incoming_fps = context.stream.measured_incoming_fps;
  uint32_t target_fps = context.stream.target_fps ?
                        context.stream.target_fps : context.stream.negotiated_fps;
  if (incoming_fps > 0 && target_fps > 0) {
    snprintf(fps_value, sizeof(fps_value), "%u / %u", incoming_fps, target_fps);
  } else if (incoming_fps > 0) {
    snprintf(fps_value, sizeof(fps_value), "%u", incoming_fps);
  }

  const char *title = "Stream Stats";
  const int margin = 18;
  const int top_offset = stream_exit_hint_visible_this_frame ? 44 : 0;
  const int padding_x = 14;
  const int padding_y = 10;
  const int row_gap = 5;
  const int col_gap = 14;
  const int line_h = FONT_SIZE_SMALL + row_gap;
  const int title_h = FONT_SIZE_SMALL + 6;

  int label_col_w = 0;
  int value_col_w = 0;
  for (int i = 0; i < row_count; i++) {
    int label_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, labels[i]);
    int value_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, values[i]);
    if (label_w > label_col_w) label_col_w = label_w;
    if (value_w > value_col_w) value_col_w = value_w;
  }
  int title_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, title);

  int content_w = label_col_w + col_gap + value_col_w;
  if (title_w > content_w) content_w = title_w;

  int box_w = content_w + (padding_x * 2);
  int box_h = padding_y + title_h + (row_count * line_h) + padding_y;
  int box_x = SCREEN_WIDTH - box_w - margin;
  int box_y = margin + top_offset;

  ui_draw_card_with_shadow(box_x, box_y, box_w, box_h, 10, RGBA8(20, 20, 24, 220));
  vita2d_font_draw_text(font, box_x + padding_x, box_y + padding_y + FONT_SIZE_SMALL,
                        RGBA8(0xD8, 0xE8, 0xFF, 255), FONT_SIZE_SMALL, title);

  int row_y = box_y + padding_y + title_h + FONT_SIZE_SMALL;
  for (int i = 0; i < row_count; i++) {
    int value_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, values[i]);
    int value_x = box_x + box_w - padding_x - value_w;
    vita2d_font_draw_text(font, box_x + padding_x, row_y,
                          RGBA8(0xB8, 0xC1, 0xCC, 255), FONT_SIZE_SMALL, labels[i]);
    vita2d_font_draw_text(font, value_x, row_y,
                          RGBA8(0xFF, 0xFF, 0xFF, 255), FONT_SIZE_SMALL, values[i]);
    row_y += line_h;
  }
}

void vitavideo_overlay_render(void) {
  draw_stream_exit_hint();
  draw_stream_stats_panel();
  draw_indicators();
}

void vitavideo_overlay_on_stream_start(void) {
  stream_exit_hint_start_us = 0;
  stream_exit_hint_visible_this_frame = false;
}

void vitavideo_overlay_on_stream_stop(void) {
  stream_exit_hint_start_us = 0;
  stream_exit_hint_visible_this_frame = false;
}

void vitavideo_overlay_show_poor_net_indicator(void) {
  if (!context.config.show_network_indicator)
    return;
  LOGD("PIPE/NET_UNSTABLE activated");
  poor_net_indicator.activated = true;
}

void vitavideo_overlay_hide_poor_net_indicator(void) {
  poor_net_indicator.activated = false;
  memset(&poor_net_indicator, 0, sizeof(indicator_status));
}
