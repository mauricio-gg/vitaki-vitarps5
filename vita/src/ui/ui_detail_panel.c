/**
 * @file ui_detail_panel.c
 * @brief C04 DetailPanel (SPEC.md C04)
 */

#include "ui/ui_detail_panel.h"

#include <string.h>

#include "ui/ui_animation.h"
#include "ui/ui_motion.h"
#include "ui/ui_text.h"
#include "ui/ui_text_wrap.h"
#include "ui/ui_theme.h"

/** Bytes of a fitted text, with its terminator. Longer texts are cut at a character boundary. */
#define FIT_MAX 64

static const char FIT_ELLIPSIS[] = "...";

/** A text fitted to a width, kept until the text changes. */
typedef struct fit_cache_t {
  bool valid;
  char src[FIT_MAX];  ///< the text this entry was made for
  char out[FIT_MAX];  ///< src, cut with "..." so that it fits
  int width;          ///< pixel width of out
} FitCache;

static FitCache s_title_fit;
static FitCache s_label_fit[UI_DETAIL_MAX_ROWS];
static FitCache s_value_fit[UI_DETAIL_MAX_ROWS];

/** The wrapped status message, kept until the message changes. */
static bool s_message_valid = false;
static char s_message_src[UI_WRAP_LINE_MAX];
static UiWrapped s_message_wrapped;

/** The focused item the panel last drew, and when it started rising. */
static bool s_key_valid = false;
static uint32_t s_key = 0;
static uint64_t s_rise_start_us = 0;

/* ============================================================================
 * Cached text fitting
 * ============================================================================ */

/** Copy @src into @dst (@size bytes), cutting before a UTF-8 continuation byte. */
static void copy_utf8(char *dst, size_t size, const char *src) {
  size_t n = strlen(src);
  if (n >= size) {
    n = size - 1;
    while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80)
      n--;
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
}

/** Remove the last UTF-8 character of @s. */
static void drop_last_char(char *s) {
  size_t n = strlen(s);
  if (n == 0)
    return;
  n--;
  while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80)
    n--;
  s[n] = '\0';
}

/**
 * fit_cached() - The text @src in @face, ended with "..." when it is wider than @max_w.
 * The measuring is redone only when @src differs from what @cache was made for.
 *
 * @return the text to draw; its width is in cache->width
 */
static const char *fit_cached(FitCache *cache, UiFace face, const char *src, int max_w) {
  if (cache->valid && strncmp(cache->src, src, FIT_MAX - 1) == 0)
    return cache->out;

  copy_utf8(cache->src, sizeof(cache->src), src);
  strcpy(cache->out, cache->src);
  cache->width = ui_text_face_width(face, cache->out);

  char body[FIT_MAX];
  strcpy(body, cache->src);
  while (cache->width > max_w && body[0]) {
    drop_last_char(body);
    size_t room = sizeof(cache->out) - sizeof(FIT_ELLIPSIS);
    copy_utf8(cache->out, room + 1, body);
    strcat(cache->out, FIT_ELLIPSIS);
    cache->width = ui_text_face_width(face, cache->out);
  }
  cache->valid = true;
  return cache->out;
}

/** Width function for ui_text_wrap(): the T16 face the message is drawn in. */
static int measure_t16(const char *s, void *ctx) {
  (void)ctx;
  return ui_text_face_width(UI_FACE_T16, s);
}

/** The status message wrapped to the panel; rewrapped only when the message text changed. */
static const UiWrapped *wrapped_message(const char *message) {
  if (!s_message_valid || strncmp(s_message_src, message, sizeof(s_message_src) - 1) != 0) {
    copy_utf8(s_message_src, sizeof(s_message_src), message);
    ui_text_wrap(s_message_src, UI_DETAIL_W, measure_t16, NULL, &s_message_wrapped);
    s_message_valid = true;
  }
  return &s_message_wrapped;
}

/* ============================================================================
 * Drawing
 * ============================================================================ */

/** Draw the wordmark of the type logo with its top at @y, white at opacity @k. */
static void draw_logo(const UiDetailContent *c, int y, float k) {
  if (!c->logo)
    return;
  const bool ps5 = c->logo_kind == UI_DETAIL_LOGO_PS5;
  const float scale = ps5 ? (float)UI_DETAIL_LOGO_PS5_W / (float)UI_DETAIL_PS5_SRC_W
                          : (float)UI_DETAIL_LOGO_PS4_W / (float)UI_DETAIL_PS4_SRC_W;
  vita2d_draw_texture_tint_part_scale(c->logo, (float)UI_DETAIL_X, (float)y, 0.0f,
                                      (float)(ps5 ? UI_DETAIL_PS5_SRC_Y : UI_DETAIL_PS4_SRC_Y),
                                      (float)(ps5 ? UI_DETAIL_PS5_SRC_W : UI_DETAIL_PS4_SRC_W),
                                      (float)(ps5 ? UI_DETAIL_PS5_SRC_H : UI_DETAIL_PS4_SRC_H),
                                      scale, scale, ui_color_scale_alpha(UI_TEXT, k));
}

/** Draw the title (T28) with its line top at @y. */
static void draw_title(const char *title, int y, float k) {
  const char *text = fit_cached(&s_title_fit, UI_FACE_T28, title ? title : "", UI_DETAIL_W);
  ui_text_draw_face_centered_v(UI_FACE_T28, UI_DETAIL_X, y, UI_T28_LINE,
                               ui_color_scale_alpha(UI_TEXT, k), text);
}

/** Draw the status dot and label with the line top at @y. */
static void draw_status_line(const UiDetailContent *c, int y, float k) {
  uint32_t color = ui_color_scale_alpha(c->status_color, k);
  vita2d_draw_fill_circle((float)(UI_DETAIL_X + UI_LIST_DOT_R), (float)(y + UI_T16_LINE / 2),
                          (float)UI_LIST_DOT_R, color);
  ui_text_draw_face_centered_v(UI_FACE_T16, UI_DETAIL_X + UI_LIST_DOT_R * 2 + UI_LIST_DOT_GAP, y,
                               UI_T16_LINE, color, c->status ? c->status : "");
}

/**
 * draw_message() - Draw the wrapped status message with its first line at @y.
 * @return the y below the last line
 */
static int draw_message(const UiDetailContent *c, int y, float k) {
  const UiWrapped *wrapped = wrapped_message(c->message);
  uint32_t color = ui_color_scale_alpha(c->message_color, k);
  for (int i = 0; i < wrapped->count && i < UI_DETAIL_MESSAGE_LINES; i++) {
    ui_text_draw_face_centered_v(UI_FACE_T16, UI_DETAIL_X, y, UI_T16_LINE, color,
                                 wrapped->lines[i]);
    y += UI_T16_LINE;
  }
  return y;
}

/** Draw kv row @index with its top at @y: label left, value right, hairline under it. */
static void draw_row(const UiDetailRow *row, int index, int y, float k) {
  const char *label = fit_cached(&s_label_fit[index], UI_FACE_T16, row->label, UI_DETAIL_W);
  int label_w = s_label_fit[index].width;
  ui_text_draw_face_centered_v(UI_FACE_T16, UI_DETAIL_X, y, UI_DETAIL_KV_H,
                               ui_color_scale_alpha(UI_TEXT, k), label);

  if (row->value && row->value[0]) {
    int room = UI_DETAIL_W - label_w - UI_DETAIL_KV_GAP;
    const char *value = fit_cached(&s_value_fit[index], UI_FACE_T16, row->value, room);
    uint32_t color = row->value_color ? row->value_color : UI_TEXT;
    ui_text_draw_face_centered_v(UI_FACE_T16, UI_DETAIL_X + UI_DETAIL_W - s_value_fit[index].width,
                                 y, UI_DETAIL_KV_H, ui_color_scale_alpha(color, k), value);
  }
  vita2d_draw_rectangle((float)UI_DETAIL_X, (float)(y + UI_DETAIL_KV_H - UI_LW1),
                        (float)UI_DETAIL_W, (float)UI_LW1, ui_color_scale_alpha(UI_LINE_FAINT, k));
}

/** Draw the kv rows from @y down. */
static void draw_rows(const UiDetailContent *c, int y, float k) {
  int count = c->row_count < UI_DETAIL_MAX_ROWS ? c->row_count : UI_DETAIL_MAX_ROWS;
  for (int i = 0; i < count; i++)
    draw_row(&c->rows[i], i, y + i * UI_DETAIL_KV_H, k);
}

/** Opacity and rise offset of the panel; (1, 0) once it has settled. */
static float rise_progress(void) {
  return UI_EASE_OUT(ui_motion_progress(ui_anim_elapsed_ms(s_rise_start_us), 0.0f, UI_D2_MS));
}

void ui_detail_panel_restart_rise(void) {
  s_key_valid = false;
}

void ui_detail_panel_draw(const UiDetailContent *c) {
  if (c->kind == UI_DETAIL_NONE) {
    s_key_valid = false;
    return;
  }
  if (!s_key_valid || s_key != c->key) {
    s_key = c->key;
    s_key_valid = true;
    s_rise_start_us = ui_anim_now_us();
  }

  const float k = rise_progress();
  int y = UI_DETAIL_Y + (int)((1.0f - k) * (float)UI_RISE_PX + 0.5f);

  if (c->kind == UI_DETAIL_CONSOLE) {
    draw_logo(c, y, k);
    y += UI_DETAIL_LOGO_H + UI_S2;
    draw_title(c->title, y, k);
    y += UI_T28_LINE;
    draw_status_line(c, y, k);
    y += UI_T16_LINE;
    if (c->message && c->message[0])
      y = draw_message(c, y + UI_DETAIL_MESSAGE_GAP, k);
    y += UI_S2;
  } else {
    draw_title(c->title, y, k);
    y += UI_T28_LINE;
    if (c->description && c->description[0]) {
      ui_text_draw_face_centered_v(UI_FACE_T16, UI_DETAIL_X, y, UI_T16_LINE,
                                   ui_color_scale_alpha(UI_TEXT, k), c->description);
      y += UI_T16_LINE;
    } else {
      y += UI_DETAIL_BLANK_H;
    }
    y += UI_S2;
  }
  draw_rows(c, y, k);
}
