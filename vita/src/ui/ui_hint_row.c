/**
 * @file ui_hint_row.c
 * @brief C06 HintRow (SPEC.md C06)
 */

#include "ui/ui_hint_row.h"

#include <vita2d.h>
#include <psp2/kernel/processmgr.h>

#include "context.h"
#include "ui/ui_bake.h"
#include "ui/ui_chrome_layout.h"
#include "ui/ui_internal.h"
#include "ui/ui_pill.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"

#define BADGE_DIR "app0:/assets/glyphs/"

static const char ALERT_TEXT[] = "Network Unstable";

/** The combined D-pad Left/Right hint ("Change") and Up/Down hint. */
#define ACTION_DPAD_H (UI_BTN_LEFT | UI_BTN_RIGHT)
#define ACTION_DPAD_V (UI_BTN_UP | UI_BTN_DOWN)

/** The D-pad left-right glyph is drawn in a 24 x 24 grid (the mock's dpadh icon): two triangles.
 * The up-down glyph is the same two triangles with x and y swapped. The up glyph (the mock's
 * dpadu icon) is one triangle pointing up. */
#define DPAD_GRID 24
#define DPAD_TRIANGLES 2
#define DPAD_SUBSAMPLES 4

typedef struct dpad_point_t {
  float x;
  float y;
} DpadPoint;

static const DpadPoint DPAD_TRIANGLE[DPAD_TRIANGLES][3] = {
    {{9.0f, 7.0f}, {3.0f, 12.0f}, {9.0f, 17.0f}},
    {{15.0f, 7.0f}, {21.0f, 12.0f}, {15.0f, 17.0f}},
};

static const DpadPoint DPAD_UP_TRIANGLE[1][3] = {
    {{7.0f, 15.0f}, {12.0f, 9.0f}, {17.0f, 15.0f}},
};

static vita2d_texture *s_badge_l = NULL;
static vita2d_texture *s_badge_r = NULL;
static vita2d_texture *s_badge_start = NULL;
static vita2d_texture *s_badge_dpad_h = NULL;
static vita2d_texture *s_badge_dpad_v = NULL;
static vita2d_texture *s_badge_dpad_up = NULL;

/** True when (@px, @py) lies inside triangle @t (either winding). */
static bool in_triangle(const DpadPoint t[3], float px, float py) {
  float sign[3];
  for (int i = 0; i < 3; i++) {
    const DpadPoint a = t[i];
    const DpadPoint b = t[(i + 1) % 3];
    sign[i] = (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
  }
  const bool has_neg = sign[0] < 0.0f || sign[1] < 0.0f || sign[2] < 0.0f;
  const bool has_pos = sign[0] > 0.0f || sign[1] > 0.0f || sign[2] > 0.0f;
  return !(has_neg && has_pos);
}

/** Alpha of a glyph made of @count triangles on the 24 x 24 grid: coverage, supersampled
 * DPAD_SUBSAMPLES^2. @vertical swaps x and y, turning the left-right pair into up-down. */
static float triangles_alpha(float px, float py, const DpadPoint (*triangles)[3], int count,
                             bool vertical) {
  const float cell = (float)DPAD_GRID / (float)UI_HINT_GLYPH_H;
  const float x0 = (px - 0.5f) * cell;
  const float y0 = (py - 0.5f) * cell;
  int hits = 0;
  for (int sy = 0; sy < DPAD_SUBSAMPLES; sy++) {
    for (int sx = 0; sx < DPAD_SUBSAMPLES; sx++) {
      const float across = x0 + ((float)sx + 0.5f) / (float)DPAD_SUBSAMPLES * cell;
      const float down = y0 + ((float)sy + 0.5f) / (float)DPAD_SUBSAMPLES * cell;
      const float sample_x = vertical ? down : across;
      const float sample_y = vertical ? across : down;
      for (int t = 0; t < count; t++) {
        if (in_triangle(triangles[t], sample_x, sample_y)) {
          hits++;
          break;
        }
      }
    }
  }
  return (float)hits / (float)(DPAD_SUBSAMPLES * DPAD_SUBSAMPLES);
}

/** Alpha of the left-right or up-down glyph. @ctx points to a bool, true for up-down. */
static float dpad_alpha(float px, float py, const void *ctx) {
  return triangles_alpha(px, py, DPAD_TRIANGLE, DPAD_TRIANGLES, *(const bool *)ctx);
}

/** Alpha of the up-only glyph; @ctx is unused. */
static float dpad_up_alpha(float px, float py, const void *ctx) {
  (void)ctx;
  return triangles_alpha(px, py, DPAD_UP_TRIANGLE, 1, false);
}

void ui_hint_row_init(void) {
  s_badge_l = ui_load_png_linear(BADGE_DIR "hint_l.png");
  s_badge_r = ui_load_png_linear(BADGE_DIR "hint_r.png");
  s_badge_start = ui_load_png_linear(BADGE_DIR "hint_start.png");
  static const bool HORIZONTAL = false;
  static const bool VERTICAL = true;
  if (!s_badge_dpad_h) {
    s_badge_dpad_h = ui_bake_white(UI_HINT_GLYPH_H, UI_HINT_GLYPH_H, dpad_alpha, &HORIZONTAL);
    s_badge_dpad_v = ui_bake_white(UI_HINT_GLYPH_H, UI_HINT_GLYPH_H, dpad_alpha, &VERTICAL);
    s_badge_dpad_up = ui_bake_white(UI_HINT_GLYPH_H, UI_HINT_GLYPH_H, dpad_up_alpha, NULL);
  }
}

/**
 * network_unstable_active() - True while a Network Unstable alert is showing on a menu.
 *
 * True when not streaming, the Show Network Alerts setting on, and the loss alert deadline in the
 * future. The host side sets the deadline on packet-loss bursts.
 */
static bool network_unstable_active(void) {
  if (context.stream.is_streaming || !context.config.show_network_indicator)
    return false;
  const uint64_t until_us = context.stream.loss_alert_until_us;
  return until_us != 0 && sceKernelGetProcessTimeWide() < until_us;
}

/** Texture of a single-glyph action, or NULL when the action has none. */
static vita2d_texture *glyph_texture(uint32_t action) {
  const bool circle_confirm = context.config.circle_btn_confirm;
  switch (action) {
    case UI_BTN_CONFIRM:
      return circle_confirm ? symbol_circle : symbol_ex;
    case UI_BTN_CANCEL:
      return circle_confirm ? symbol_ex : symbol_circle;
    case UI_BTN_OPTIONS:
      return symbol_triangle;
    case UI_BTN_CLEAR:
      return symbol_square;
    case UI_BTN_FILTER:
      return s_badge_start;
    case UI_BTN_L:
      return s_badge_l;
    case UI_BTN_R:
      return s_badge_r;
    case ACTION_DPAD_H:
      return s_badge_dpad_h;
    case ACTION_DPAD_V:
      return s_badge_dpad_v;
    case UI_BTN_UP:
      return s_badge_dpad_up;
    default:
      return NULL;
  }
}

/** Drawn width of @tex scaled to UI_HINT_GLYPH_H high; 0 for NULL. */
static int glyph_width(const vita2d_texture *tex) {
  if (!tex)
    return 0;
  return (int)vita2d_texture_get_width(tex) * UI_HINT_GLYPH_H / (int)vita2d_texture_get_height(tex);
}

/** Width of the glyph (or glyph pair) of @action; 0 when it has none. */
static int action_glyph_width(uint32_t action) {
  if (action == (UI_BTN_L | UI_BTN_R))
    return glyph_width(s_badge_l) + UI_HINT_LR_GAP + glyph_width(s_badge_r);
  return glyph_width(glyph_texture(action));
}

/** Width of a whole hint: glyph, gap, label. */
static int item_width(const UiHintItem *item) {
  const int glyph_w = action_glyph_width(item->action);
  const int label_w = ui_text_face_width(UI_FACE_T14, item->label);
  return glyph_w > 0 ? glyph_w + UI_HINT_GLYPH_GAP + label_w : label_w;
}

void ui_hint_row_layout(UiHintLayout *layout, const UiHintItem *items, int count) {
  if (count > UI_HINT_MAX_ITEMS)
    count = UI_HINT_MAX_ITEMS;

  int widths[UI_HINT_MAX_ITEMS];
  bool low[UI_HINT_MAX_ITEMS];
  bool keep[UI_HINT_MAX_ITEMS];
  for (int i = 0; i < count; i++) {
    widths[i] = item_width(&items[i]);
    low[i] = items[i].low_priority;
  }

  layout->alert = network_unstable_active();
  /* Show Button Hints off: the row keeps only its alert slot. Dropping every item here makes
   * draw and tap inert for all screens without them testing the setting. */
  if (!context.config.show_button_hints)
    count = 0;
  const int area_x0 = UI_MARGIN_X;
  const int area_x1 =
      layout->alert ? UI_CONTENT_RIGHT - UI_HINT_ALERT_W - UI_HINT_ALERT_GAP : UI_CONTENT_RIGHT;
  const int total = ui_hint_row_fit(widths, low, count, UI_HINT_GAP, area_x1 - area_x0, keep);

  int x = (area_x0 + area_x1 - total) / 2;
  if (x < area_x0)
    x = area_x0;
  layout->count = 0;
  for (int i = 0; i < count; i++) {
    if (!keep[i])
      continue;
    const int n = layout->count++;
    layout->items[n] = items[i];
    layout->x[n] = x;
    layout->hit[n] = (UiRect){x, UI_HINT_Y, widths[i], UI_HINT_H};
    x += widths[i] + UI_HINT_GAP;
  }
}

/** Draw @tex scaled to UI_HINT_GLYPH_H high at (@x, @y) with @tint. */
static void draw_glyph(vita2d_texture *tex, int x, int y, uint32_t tint) {
  if (!tex)
    return;
  const float scale = (float)UI_HINT_GLYPH_H / (float)vita2d_texture_get_height(tex);
  vita2d_draw_texture_tint_scale(tex, (float)x, (float)y, scale, scale, tint);
}

int ui_hint_row_glyph_width(uint32_t action) {
  return glyph_width(glyph_texture(action));
}

void ui_hint_row_glyph_draw(uint32_t action, int x, int y, int h, uint32_t tint) {
  draw_glyph(glyph_texture(action), x, y + (h - UI_HINT_GLYPH_H) / 2, ui_layer_color(tint));
}

void ui_hint_row_draw(const UiHintLayout *layout) {
  const int glyph_y = UI_HINT_Y + (UI_HINT_H - UI_HINT_GLYPH_H) / 2;

  for (int i = 0; i < layout->count; i++) {
    const UiHintItem *item = &layout->items[i];
    const float k = item->dim ? (float)UI_HINT_DIM_PCT / 100.0f : 1.0f;
    const uint32_t tint = ui_color_scale_alpha(UI_TEXT, k);
    int x = layout->x[i];

    if (item->action == (UI_BTN_L | UI_BTN_R)) {
      draw_glyph(s_badge_l, x, glyph_y, tint);
      x += glyph_width(s_badge_l) + UI_HINT_LR_GAP;
      draw_glyph(s_badge_r, x, glyph_y, tint);
      x += glyph_width(s_badge_r) + UI_HINT_GLYPH_GAP;
    } else if (glyph_texture(item->action)) {
      draw_glyph(glyph_texture(item->action), x, glyph_y, tint);
      x += glyph_width(glyph_texture(item->action)) + UI_HINT_GLYPH_GAP;
    }
    ui_text_draw_face_centered_v(UI_FACE_T14, x, UI_HINT_Y, UI_HINT_H,
                                 ui_color_scale_alpha(UI_TEXT_2, k), item->label);
  }

  if (layout->alert) {
    const int w = ui_pill_width(UI_PILL_UNSTABLE, ALERT_TEXT);
    ui_pill_draw(UI_PILL_UNSTABLE, UI_CONTENT_RIGHT - w, UI_HINT_Y + (UI_HINT_H - UI_PILL_H) / 2, w,
                 ALERT_TEXT);
  }
}

uint32_t ui_hint_row_tap(const UiHintLayout *layout, const UiInput *in) {
  if (!ui_touch_tap(in))
    return 0;
  for (int i = 0; i < layout->count; i++) {
    const UiRect hit = layout->hit[i];
    if (!ui_rect_contains(hit, in->touch.x, in->touch.y))
      continue;
    const uint32_t action = layout->items[i].action;
    if (action != (UI_BTN_L | UI_BTN_R))
      return action;
    return in->touch.x < (float)(hit.x + hit.w / 2) ? UI_BTN_L : UI_BTN_R;
  }
  return 0;
}
