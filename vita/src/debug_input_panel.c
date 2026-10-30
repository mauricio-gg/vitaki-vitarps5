#include "debug_input_panel.h"

#include "input_combos.h"

#if VITARPS5_DEBUG_TOOLS

#include "ui/ui_component.h"
#include "ui/ui_shapes.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

/* The click light stays on at least this long after a press, so a 50 ms click is visible at 60 fps.
 */
#define PANEL_LIGHT_MIN_MS 250
/* The hold that reads "in range" (inclusive): the 50 ms click hold, with room either side. */
#define PANEL_HOLD_OK_MIN_MS 40
#define PANEL_HOLD_OK_MAX_MS 70
/* A lifted finger leaves a ring this long. */
#define PANEL_GHOST_MS 400
/* The ring starts at this share of full opacity (percent) and fades to nothing. */
#define PANEL_GHOST_START_PCT 60
/* The numbers (tilt lines and table) are rebuilt at this interval so they can be read. */
#define PANEL_REBUILD_US 100000ULL
/* Roll/pitch difference above which "sent differs from raw" shows (degrees). */
#define PANEL_TILT_WARN_DEG 5.0f

/* Panel-relative layout (px), from the approved mock (docs/design/ui-mocks/input-test.html). */
#define PANEL_TOUCH_Y 48
#define PANEL_RULE1_Y 196
#define PANEL_CLICK_Y 204
#define PANEL_CLICK_H 40
#define PANEL_RULE2_Y 252
#define PANEL_MOTION_Y 256
#define PANEL_FOOTER_H 48
#define PANEL_CONTENT_W (DEBUG_PANEL_W - 2 * DEBUG_PANEL_PAD_X)

#define PANEL_LABEL_H 20
#define PANEL_PAD_Y 20
#define PANEL_PAD_W 232
#define PANEL_PAD_H 114
#define PANEL_PAD_THIRD_X 77
#define PANEL_PAD_THIRD_Y 38
#define PANEL_DOT_R 8
#define PANEL_HALO_R 10
#define PANEL_READOUT_X 248
#define PANEL_READOUT_ROW_H 24
#define PANEL_READOUT_LABEL_GAP 4
#define PANEL_READOUT_VALUE_X 24
#define PANEL_READOUT_Y_VALUE_X 72
#define PANEL_TOUCH_MAX_X 1919
#define PANEL_TOUCH_MAX_Y 941

#define PANEL_LIGHT_D 24
#define PANEL_CLICK_GAP 16
#define PANEL_BIG_MIN_W 40
#define PANEL_UNIT_GAP 4

#define PANEL_HZ_Y 18
#define PANEL_HZ_SIZE 72
#define PANEL_HZ_SENT_X 80
#define PANEL_HZ_HALF_UNITS 44.0f  /* max pitch shift, in the mock's 96-unit box */
#define PANEL_HZ_UNIT_PX 0.75f     /* 72 px / 96 units */
#define PANEL_HZ_GUIDE_UNITS 14.0f /* guide lines either side of the horizon, in units */
#define PANEL_HZ_MARK_HALF 3       /* half length of each centre-mark tick */
#define PANEL_HZ_MARK_GAP 3        /* gap between the two ticks */
#define PANEL_TILT_X 168
#define PANEL_TILT_Y 20
#define PANEL_TABLE_Y 112
#define PANEL_TABLE_LABEL_W 32
#define PANEL_TABLE_COL_W 77
#define PANEL_TABLE_GAP 12
#define PANEL_TABLE_HEAD_H 20
#define PANEL_TABLE_HEAD_ROW 22
#define PANEL_TABLE_ROW_H 22
#define PANEL_AXES 3

#define PANEL_PILL_H 20
#define PANEL_PILL_PAD_X 8
#define PANEL_FOOTER_GAP 8
#define PANEL_PROGRESS_W 72
#define PANEL_PROGRESS_H 4

#define PANEL_CLOSE_GLYPH 12
#define PANEL_CLOSE_PAD_LEFT 12
#define PANEL_CLOSE_GLYPH_GAP 8
#define PANEL_TITLE_GAP 8

#define PANEL_DEG_PER_RAD 57.29578f
#define PANEL_PITCH_FULL_DEG 90.0f
#define PANEL_TEXT_LEN 16

static const char PANEL_TITLE[] = "Input test";
static const char PANEL_SUBTITLE[] = "what is sent";
static const char PANEL_NO_TOUCH[] = "touch the screen";
static const char PANEL_TOUCH_LABEL[] = "FRONT TOUCH \xC2\xB7 1920 x 942";
static const char PANEL_MOTION_LABEL[] = "MOTION \xC2\xB7 raw Vita vs as sent";
static const char PANEL_TILT_LABEL[] = "TILT deg (roll / pitch)";
static const char PANEL_ACCEL_HEAD[] = "ACCEL (G)";
static const char PANEL_GYRO_HEAD[] = "GYRO (rad/s)";
static const char PANEL_RAW[] = "raw";
static const char PANEL_SENT[] = "sent";
static const char PANEL_RAW_PREFIX[] = "raw ";
static const char PANEL_SENT_PREFIX[] = "sent ";
static const char PANEL_CLICK[] = "Click";
static const char PANEL_HOLD[] = "Hold";
static const char PANEL_MS[] = "ms";
static const char PANEL_IN_RANGE[] = "in range";
static const char PANEL_EXPECT[] = "expect ~50";
static const char PANEL_DIFFERS[] = "sent differs from raw";
static const char PANEL_MATCHES[] = "sent matches raw";
static const char PANEL_CLOSE[] = "Close";
static const char PANEL_HINT_IDLE[] = "hold to close";
static const char PANEL_HINT_HELD[] = "keep holding";
static const char *const PANEL_PILLS[] = {"L", "R", "TRIANGLE"};
#define PANEL_PILL_COUNT 3
static const char PANEL_AXIS_NAMES[PANEL_AXES][2] = {"x", "y", "z"};

/* Text widths that never change; measured once, on the first drawn frame. */
typedef struct {
  bool valid;
  int title, no_touch, click, hold, raw, sent, raw_prefix, sent_prefix, accel_head, gyro_head;
  int in_range, expect, pill[PANEL_PILL_COUNT];
} FixedWidths;

/* Roll and pitch in degrees. */
typedef struct {
  float roll, pitch;
} Tilt;

/* The click row's text; rebuilt only when the click statistics change. */
typedef struct {
  bool valid;
  uint32_t count, last_hold_ms;
  bool hold_valid;
  char count_text[PANEL_TEXT_LEN], hold_text[PANEL_TEXT_LEN];
  int count_w, hold_w;
} ClickText;

/* A finger's sent position as text, rebuilt only when the position changes. */
typedef struct {
  bool valid, active;
  uint16_t x, y;
  char x_text[PANEL_TEXT_LEN], y_text[PANEL_TEXT_LEN];
} TouchText;

/* The numbers read at 10 Hz: the tilt lines and the accel/gyro table. */
typedef struct {
  bool valid;
  uint64_t built_us;
  char raw_tilt[PANEL_TEXT_LEN * 2], sent_tilt[PANEL_TEXT_LEN * 2];
  bool differs;
  char cell[PANEL_AXES][4]
           [PANEL_TEXT_LEN]; /* per axis: accel raw, accel sent, gyro raw, gyro sent */
  int cell_w[PANEL_AXES][4];
} MotionText;

/* The ring left where a finger lifted; at most one, dropped as soon as any finger is down. */
typedef struct {
  bool visible;
  int x, y;
  uint64_t since_us;
} Ghost;

static FixedWidths fixed;
static ClickText click_text;
static TouchText touch_text[DEBUG_SNAPSHOT_TOUCHES];
static MotionText motion_text;
static Ghost ghost;
static bool prev_active[DEBUG_SNAPSHOT_TOUCHES];
static int prev_x[DEBUG_SNAPSHOT_TOUCHES], prev_y[DEBUG_SNAPSHOT_TOUCHES];

void debug_input_panel_reset(void) {
  memset(&click_text, 0, sizeof(click_text));
  memset(touch_text, 0, sizeof(touch_text));
  memset(&motion_text, 0, sizeof(motion_text));
  memset(&ghost, 0, sizeof(ghost));
  memset(prev_active, 0, sizeof(prev_active));
}

/* ---- small drawing helpers ---- */

static void fill_rect(int x, int y, int w, int h, uint32_t color) {
  vita2d_draw_rectangle((float)x, (float)y, (float)w, (float)h, color);
}

/* A hollow rectangle with a border of @p t px inside the given box. */
static void box_outline(int x, int y, int w, int h, int t, uint32_t color) {
  fill_rect(x, y, w, t, color);
  fill_rect(x, y + h - t, w, t, color);
  fill_rect(x, y + t, t, h - 2 * t, color);
  fill_rect(x + w - t, y + t, t, h - 2 * t, color);
}

static void fill_circle(int cx, int cy, int r, uint32_t color) {
  vita2d_draw_fill_circle((float)cx, (float)cy, (float)r, color);
}

/* The panel colour without transparency, for the inside of a ring drawn as two stacked discs. */
static uint32_t panel_opaque(void) {
  return ui_rgb_alpha(ui_theme_current()->deep, 0xFF);
}

/* A ring of @p r with a 2 px wall in @p color (a disc with an opaque disc inside). */
static void ring(int cx, int cy, int r, uint32_t color) {
  fill_circle(cx, cy, r, color);
  fill_circle(cx, cy, r - UI_LW2, panel_opaque());
}

static void text(UiFace face, int x, int row_y, int row_h, uint32_t color, const char *s) {
  ui_text_draw_face_centered_v(face, x, row_y, row_h, color, s);
}

/* Draws @p s so its right edge is at @p right_x, given its measured width @p w. */
static void text_right(UiFace face, int right_x, int row_y, int row_h, uint32_t color,
                       const char *s, int w) {
  text(face, right_x - w, row_y, row_h, color, s);
}

static int clampi(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

/* Where a sent touch (1920x942 space) lands on the pad, relative to its top-left corner. */
static int pad_offset_x(int touch_x) {
  return clampi(touch_x, 0, PANEL_TOUCH_MAX_X) * (PANEL_PAD_W - 1) / PANEL_TOUCH_MAX_X;
}

static int pad_offset_y(int touch_y) {
  return clampi(touch_y, 0, PANEL_TOUCH_MAX_Y) * (PANEL_PAD_H - 1) / PANEL_TOUCH_MAX_Y;
}

static void measure_fixed(void) {
  if (fixed.valid)
    return;
  fixed.title = ui_text_face_width(UI_FACE_T20, PANEL_TITLE);
  fixed.no_touch = ui_text_face_width(UI_FACE_T14, PANEL_NO_TOUCH);
  fixed.click = ui_text_face_width(UI_FACE_T16, PANEL_CLICK);
  fixed.hold = ui_text_face_width(UI_FACE_T16, PANEL_HOLD);
  fixed.raw = ui_text_face_width(UI_FACE_T14, PANEL_RAW);
  fixed.sent = ui_text_face_width(UI_FACE_T14, PANEL_SENT);
  fixed.raw_prefix = ui_text_face_width(UI_FACE_T16, PANEL_RAW_PREFIX);
  fixed.sent_prefix = ui_text_face_width(UI_FACE_T16, PANEL_SENT_PREFIX);
  fixed.accel_head = ui_text_face_width(UI_FACE_T14, PANEL_ACCEL_HEAD);
  fixed.gyro_head = ui_text_face_width(UI_FACE_T14, PANEL_GYRO_HEAD);
  fixed.in_range = ui_text_face_width(UI_FACE_T14, PANEL_IN_RANGE);
  fixed.expect = ui_text_face_width(UI_FACE_T14, PANEL_EXPECT);
  for (int i = 0; i < PANEL_PILL_COUNT; i++)
    fixed.pill[i] = ui_text_face_width(UI_FACE_T14, PANEL_PILLS[i]);
  fixed.valid = true;
}

/* ---- tilt ---- */

/* Roll and pitch of a gravity vector read as a Vita frame (flat face up = -z). */
static Tilt tilt_from_vita(const float a[3]) {
  return (Tilt){atan2f(a[0], -a[2]) * PANEL_DEG_PER_RAD,
                atan2f(a[1], hypotf(a[0], a[2])) * PANEL_DEG_PER_RAD};
}

/* The same pose, read from the sent accel in the DualSense frame (flat = +y). The sent accel is
 * the Vita's (ax, ay, az) as (-ax, -az, ay) (motion_frame.h), so roll = atan2(ax, -az) becomes
 * atan2(-x, y) and pitch = atan2(ay, hypot(ax, az)) becomes atan2(z, hypot(x, y)): the same
 * physical roll or pitch tilts both horizons the same way. */
static Tilt tilt_from_dualsense(const float a[3]) {
  return (Tilt){atan2f(-a[0], a[1]) * PANEL_DEG_PER_RAD,
                atan2f(a[2], hypotf(a[0], a[1])) * PANEL_DEG_PER_RAD};
}

/* Draws the line through (cx, cy) along (dx, dy), cut to the box [x0..x1] x [y0..y1] by working out
 * its end points (vita2d has no clipping). */
static void line_in_box(float cx, float cy, float dx, float dy, float x0, float y0, float x1,
                        float y1, uint32_t color) {
  const float far = 1000.0f;
  float t_min = -far, t_max = far;
  const float c[2] = {cx, cy}, d[2] = {dx, dy}, lo[2] = {x0, y0}, hi[2] = {x1, y1};
  for (int i = 0; i < 2; i++) {
    if (fabsf(d[i]) < 1e-6f) {
      if (c[i] < lo[i] || c[i] > hi[i])
        return;
      continue;
    }
    float ta = (lo[i] - c[i]) / d[i], tb = (hi[i] - c[i]) / d[i];
    if (ta > tb) {
      float tmp = ta;
      ta = tb;
      tb = tmp;
    }
    if (ta > t_min)
      t_min = ta;
    if (tb < t_max)
      t_max = tb;
  }
  if (t_min > t_max)
    return;
  vita2d_draw_line(cx + t_min * dx, cy + t_min * dy, cx + t_max * dx, cy + t_max * dy, color);
}

/* One horizon box: faint fill, outline, the horizon (2 px, rotated by roll, shifted down by pitch)
 * with a guide line either side, and the centre mark. */
static void draw_horizon(int bx, int by, Tilt tilt) {
  fill_rect(bx, by, PANEL_HZ_SIZE, PANEL_HZ_SIZE, UI_GLASS_FROST);
  box_outline(bx, by, PANEL_HZ_SIZE, PANEL_HZ_SIZE, UI_LW1, UI_LINE);

  float shift = tilt.pitch / PANEL_PITCH_FULL_DEG * PANEL_HZ_HALF_UNITS;
  if (shift > PANEL_HZ_HALF_UNITS)
    shift = PANEL_HZ_HALF_UNITS;
  if (shift < -PANEL_HZ_HALF_UNITS)
    shift = -PANEL_HZ_HALF_UNITS;
  float roll_rad = tilt.roll / PANEL_DEG_PER_RAD;
  float dx = cosf(roll_rad), dy = sinf(roll_rad);
  float nx = -dy, ny = dx; /* the horizon's normal: "down" in its own frame */
  float mid = PANEL_HZ_SIZE * 0.5f;
  float cx = bx + mid, cy = by + mid + shift * PANEL_HZ_UNIT_PX;
  float x0 = bx + UI_LW1, y0 = by + UI_LW1;
  float x1 = bx + PANEL_HZ_SIZE - UI_LW1, y1 = by + PANEL_HZ_SIZE - UI_LW1;
  float guide = PANEL_HZ_GUIDE_UNITS * PANEL_HZ_UNIT_PX;

  uint32_t line = UI_LINE;
  line_in_box(cx + nx * guide, cy + ny * guide, dx, dy, x0, y0, x1, y1, line);
  line_in_box(cx - nx * guide, cy - ny * guide, dx, dy, x0, y0, x1, y1, line);
  uint32_t horizon = UI_TEXT;
  line_in_box(cx - nx * 0.5f, cy - ny * 0.5f, dx, dy, x0, y0, x1, y1, horizon);
  line_in_box(cx + nx * 0.5f, cy + ny * 0.5f, dx, dy, x0, y0, x1, y1, horizon);

  int mx = bx + PANEL_HZ_SIZE / 2, my = by + PANEL_HZ_SIZE / 2;
  uint32_t accent = UI_ACCENT;
  fill_rect(mx - PANEL_HZ_MARK_GAP - PANEL_HZ_MARK_HALF, my - 1, PANEL_HZ_MARK_HALF, UI_LW2,
            accent);
  fill_rect(mx + PANEL_HZ_MARK_GAP, my - 1, PANEL_HZ_MARK_HALF, UI_LW2, accent);
  fill_circle(mx, my, 2, accent);
}

/* ---- cached text ---- */

static void rebuild_motion(const DebugInputSnapshot *s, Tilt raw, Tilt sent, uint64_t now_us) {
  if (motion_text.valid && now_us - motion_text.built_us < PANEL_REBUILD_US)
    return;
  motion_text.built_us = now_us;
  motion_text.valid = true;
  snprintf(motion_text.raw_tilt, sizeof(motion_text.raw_tilt), "%+.0f / %+.0f", raw.roll,
           raw.pitch);
  snprintf(motion_text.sent_tilt, sizeof(motion_text.sent_tilt), "%+.0f / %+.0f", sent.roll,
           sent.pitch);
  motion_text.differs = fabsf(raw.roll - sent.roll) > PANEL_TILT_WARN_DEG ||
                        fabsf(raw.pitch - sent.pitch) > PANEL_TILT_WARN_DEG;
  for (int i = 0; i < PANEL_AXES; i++) {
    snprintf(motion_text.cell[i][0], PANEL_TEXT_LEN, "%+.2f", s->raw_accel[i]);
    snprintf(motion_text.cell[i][1], PANEL_TEXT_LEN, "%+.2f", s->sent_accel[i]);
    snprintf(motion_text.cell[i][2], PANEL_TEXT_LEN, "%+.3f", s->raw_gyro[i]);
    snprintf(motion_text.cell[i][3], PANEL_TEXT_LEN, "%+.3f", s->sent_gyro[i]);
    for (int c = 0; c < 4; c++)
      motion_text.cell_w[i][c] = ui_text_face_width(UI_FACE_T16, motion_text.cell[i][c]);
  }
}

static void rebuild_click(const InputTestClick *c) {
  if (click_text.valid && click_text.count == c->count && click_text.hold_valid == c->hold_valid &&
      click_text.last_hold_ms == c->last_hold_ms)
    return;
  click_text.valid = true;
  click_text.count = c->count;
  click_text.hold_valid = c->hold_valid;
  click_text.last_hold_ms = c->last_hold_ms;
  snprintf(click_text.count_text, PANEL_TEXT_LEN, "%u", (unsigned)c->count);
  if (c->hold_valid)
    snprintf(click_text.hold_text, PANEL_TEXT_LEN, "%u", (unsigned)c->last_hold_ms);
  else
    snprintf(click_text.hold_text, PANEL_TEXT_LEN, "-");
  click_text.count_w = ui_text_face_width(UI_FACE_T20, click_text.count_text);
  click_text.hold_w = ui_text_face_width(UI_FACE_T20, click_text.hold_text);
}

static void rebuild_touch(int slot, bool active, uint16_t x, uint16_t y) {
  TouchText *t = &touch_text[slot];
  if (t->valid && t->active == active && (!active || (t->x == x && t->y == y)))
    return;
  t->valid = true;
  t->active = active;
  t->x = x;
  t->y = y;
  if (active) {
    snprintf(t->x_text, PANEL_TEXT_LEN, "%u", (unsigned)x);
    snprintf(t->y_text, PANEL_TEXT_LEN, "%u", (unsigned)y);
  } else {
    snprintf(t->x_text, PANEL_TEXT_LEN, "-");
    snprintf(t->y_text, PANEL_TEXT_LEN, "-");
  }
}

/* Remembers where a finger lifted, and drops the ring while any finger is down. */
static void update_ghost(const DebugInputSnapshot *s, uint64_t now_us) {
  bool any_active = false;
  for (int i = 0; i < DEBUG_SNAPSHOT_TOUCHES; i++) {
    if (s->touches[i].active) {
      any_active = true;
    } else if (prev_active[i]) {
      ghost.visible = true;
      ghost.x = prev_x[i];
      ghost.y = prev_y[i];
      ghost.since_us = now_us;
    }
    prev_active[i] = s->touches[i].active;
    prev_x[i] = s->touches[i].x;
    prev_y[i] = s->touches[i].y;
  }
  if (any_active || (ghost.visible && now_us - ghost.since_us >= PANEL_GHOST_MS * 1000ULL))
    ghost.visible = false;
}

/* ---- sections ---- */

static void draw_header(int px, int py) {
  text(UI_FACE_T20, px, py, DEBUG_PANEL_HEADER_H, UI_TEXT, PANEL_TITLE);
  text(UI_FACE_T14, px + fixed.title + PANEL_TITLE_GAP, py, DEBUG_PANEL_HEADER_H, UI_TEXT_3,
       PANEL_SUBTITLE);

  DebugRect close = DEBUG_CLOSE_RECT;
  ui_shape3_draw(UI_SHAPE3_PILL_32_OUTLINE, close.x, close.y, close.w, UI_LINE);
  int gx = close.x + PANEL_CLOSE_PAD_LEFT;
  int gy = close.y + (close.h - PANEL_CLOSE_GLYPH) / 2;
  uint32_t white = UI_TEXT;
  for (int w = 0; w < UI_LW2; w++) { /* a 2 px stroke is two 1 px lines side by side */
    vita2d_draw_line((float)(gx + w), (float)gy, (float)(gx + w + PANEL_CLOSE_GLYPH - 1),
                     (float)(gy + PANEL_CLOSE_GLYPH - 1), white);
    vita2d_draw_line((float)(gx + w + PANEL_CLOSE_GLYPH - 1), (float)gy, (float)(gx + w),
                     (float)(gy + PANEL_CLOSE_GLYPH - 1), white);
  }
  text(UI_FACE_T16, gx + PANEL_CLOSE_GLYPH + PANEL_CLOSE_GLYPH_GAP, close.y, close.h, white,
       PANEL_CLOSE);
}

static void draw_touch_section(int x, int y, const DebugInputSnapshot *s, bool lit,
                               uint64_t now_us) {
  text(UI_FACE_T14, x, y, PANEL_LABEL_H, UI_TEXT_3, PANEL_TOUCH_LABEL);

  const int pad_x = x, pad_y = y + PANEL_PAD_Y;
  fill_rect(pad_x, pad_y, PANEL_PAD_W, PANEL_PAD_H, UI_GLASS_FROST);
  fill_rect(pad_x + PANEL_PAD_THIRD_X, pad_y, UI_LW1, PANEL_PAD_H, UI_LINE_FAINT);
  fill_rect(pad_x + 2 * PANEL_PAD_THIRD_X, pad_y, UI_LW1, PANEL_PAD_H, UI_LINE_FAINT);
  fill_rect(pad_x, pad_y + PANEL_PAD_THIRD_Y, PANEL_PAD_W, UI_LW1, UI_LINE_FAINT);
  fill_rect(pad_x, pad_y + 2 * PANEL_PAD_THIRD_Y, PANEL_PAD_W, UI_LW1, UI_LINE_FAINT);
  if (lit)
    box_outline(pad_x, pad_y, PANEL_PAD_W, PANEL_PAD_H, UI_LW2, UI_OK);
  else
    box_outline(pad_x, pad_y, PANEL_PAD_W, PANEL_PAD_H, UI_LW1, UI_LINE);

  bool any_active = false;
  for (int i = 0; i < DEBUG_SNAPSHOT_TOUCHES; i++)
    any_active = any_active || s->touches[i].active;
  if (!any_active)
    text(UI_FACE_T14, pad_x + (PANEL_PAD_W - fixed.no_touch) / 2, pad_y, PANEL_PAD_H, UI_TEXT_3,
         PANEL_NO_TOUCH);

  if (ghost.visible) {
    uint64_t age_us = now_us - ghost.since_us;
    uint32_t alpha = (uint32_t)(255 * PANEL_GHOST_START_PCT / 100 *
                                (PANEL_GHOST_MS * 1000ULL - age_us) / (PANEL_GHOST_MS * 1000ULL));
    ring(pad_x + pad_offset_x(ghost.x), pad_y + pad_offset_y(ghost.y), PANEL_DOT_R,
         (UI_TEXT_3 & 0x00FFFFFFu) | (alpha << 24));
  }

  for (int i = 0; i < DEBUG_SNAPSHOT_TOUCHES; i++) {
    if (!s->touches[i].active)
      continue;
    int dx = pad_x + pad_offset_x(s->touches[i].x), dy = pad_y + pad_offset_y(s->touches[i].y);
    fill_circle(dx, dy, PANEL_HALO_R, panel_opaque());
    fill_circle(dx, dy, PANEL_DOT_R, i == 0 ? UI_TEXT : UI_ACCENT);
  }

  const int rx = x + PANEL_READOUT_X;
  for (int i = 0; i < DEBUG_SNAPSHOT_TOUCHES; i++) {
    const int row_y = pad_y + i * PANEL_READOUT_ROW_H;
    rebuild_touch(i, s->touches[i].active, s->touches[i].x, s->touches[i].y);
    const bool active = s->touches[i].active;
    const int cy = row_y + PANEL_READOUT_ROW_H / 2;
    uint32_t swatch = i == 0 ? UI_TEXT : UI_ACCENT;
    if (active)
      fill_circle(rx + PANEL_DOT_R, cy, PANEL_DOT_R, swatch);
    else
      ring(rx + PANEL_DOT_R, cy, PANEL_DOT_R, UI_TEXT_3);
    uint32_t value_color = active ? UI_TEXT : UI_TEXT_3;
    text(UI_FACE_T14, rx + PANEL_READOUT_VALUE_X, row_y, PANEL_READOUT_ROW_H, value_color,
         touch_text[i].x_text);
    text(UI_FACE_T14, rx + PANEL_READOUT_Y_VALUE_X, row_y, PANEL_READOUT_ROW_H, value_color,
         touch_text[i].y_text);
  }
  const int head_y = pad_y + DEBUG_SNAPSHOT_TOUCHES * PANEL_READOUT_ROW_H + PANEL_READOUT_LABEL_GAP;
  text(UI_FACE_T14, rx + PANEL_READOUT_VALUE_X, head_y, PANEL_LABEL_H, UI_TEXT_3, "x");
  text(UI_FACE_T14, rx + PANEL_READOUT_Y_VALUE_X, head_y, PANEL_LABEL_H, UI_TEXT_3, "y (as sent)");
}

static void draw_click_section(int x, int y, const InputTestClick *c, bool lit) {
  rebuild_click(c);
  const int cy = y + PANEL_CLICK_H / 2;
  if (lit)
    fill_circle(x + PANEL_LIGHT_D / 2, cy, PANEL_LIGHT_D / 2, UI_OK);
  else
    ring(x + PANEL_LIGHT_D / 2, cy, PANEL_LIGHT_D / 2, UI_LINE);

  int cx = x + PANEL_LIGHT_D + PANEL_CLICK_GAP;
  text(UI_FACE_T16, cx, y, PANEL_CLICK_H, UI_TEXT_2, PANEL_CLICK);
  cx += fixed.click + PANEL_CLICK_GAP;
  text(UI_FACE_T20, cx, y, PANEL_CLICK_H, UI_TEXT, click_text.count_text);
  cx += (click_text.count_w > PANEL_BIG_MIN_W ? click_text.count_w : PANEL_BIG_MIN_W) +
        PANEL_CLICK_GAP;
  fill_rect(cx, cy - PANEL_LIGHT_D / 2, UI_LW1, PANEL_LIGHT_D, UI_LINE_FAINT);
  cx += UI_LW1 + PANEL_CLICK_GAP;
  text(UI_FACE_T16, cx, y, PANEL_CLICK_H, UI_TEXT_2, PANEL_HOLD);
  cx += fixed.hold + PANEL_CLICK_GAP;
  text(UI_FACE_T20, cx, y, PANEL_CLICK_H, c->hold_valid ? UI_TEXT : UI_TEXT_3,
       click_text.hold_text);
  if (!c->hold_valid)
    return;

  text(UI_FACE_T14, cx + click_text.hold_w + PANEL_UNIT_GAP, y, PANEL_CLICK_H, UI_TEXT_3, PANEL_MS);
  bool ok = c->last_hold_ms >= PANEL_HOLD_OK_MIN_MS && c->last_hold_ms <= PANEL_HOLD_OK_MAX_MS;
  text_right(UI_FACE_T14, x + PANEL_CONTENT_W, y, PANEL_CLICK_H, ok ? UI_OK : UI_WARN,
             ok ? PANEL_IN_RANGE : PANEL_EXPECT, ok ? fixed.in_range : fixed.expect);
}

static void draw_table(int x, int y) {
  const int col_right[4] = {
      x + PANEL_TABLE_LABEL_W + PANEL_TABLE_COL_W,
      x + PANEL_TABLE_LABEL_W + 2 * PANEL_TABLE_COL_W,
      x + PANEL_TABLE_LABEL_W + 3 * PANEL_TABLE_COL_W + PANEL_TABLE_GAP,
      x + PANEL_TABLE_LABEL_W + 4 * PANEL_TABLE_COL_W + PANEL_TABLE_GAP,
  };
  const int accel_left = x + PANEL_TABLE_LABEL_W;
  const int gyro_left = col_right[1] + PANEL_TABLE_GAP;
  const int rule_y = y + PANEL_TABLE_HEAD_ROW - UI_LW1;

  text_right(UI_FACE_T14, col_right[1], y, PANEL_TABLE_HEAD_H, UI_TEXT_3, PANEL_ACCEL_HEAD,
             fixed.accel_head);
  text_right(UI_FACE_T14, col_right[3], y, PANEL_TABLE_HEAD_H, UI_TEXT_3, PANEL_GYRO_HEAD,
             fixed.gyro_head);
  fill_rect(accel_left, rule_y, col_right[1] - accel_left, UI_LW1, UI_LINE_FAINT);
  fill_rect(gyro_left, rule_y, col_right[3] - gyro_left, UI_LW1, UI_LINE_FAINT);

  const int sub_y = y + PANEL_TABLE_HEAD_ROW;
  for (int c = 0; c < 4; c++) {
    bool sent = (c & 1) != 0;
    text_right(UI_FACE_T14, col_right[c], sub_y, PANEL_TABLE_HEAD_H, sent ? UI_ACCENT : UI_TEXT_3,
               sent ? PANEL_SENT : PANEL_RAW, sent ? fixed.sent : fixed.raw);
  }

  const int rows_y = sub_y + PANEL_TABLE_HEAD_ROW;
  for (int i = 0; i < PANEL_AXES; i++) {
    const int row_y = rows_y + i * PANEL_TABLE_ROW_H;
    text(UI_FACE_T16, x, row_y, PANEL_TABLE_ROW_H, UI_TEXT_3, PANEL_AXIS_NAMES[i]);
    for (int c = 0; c < 4; c++)
      text_right(UI_FACE_T16, col_right[c], row_y, PANEL_TABLE_ROW_H, (c & 1) ? UI_ACCENT : UI_TEXT,
                 motion_text.cell[i][c], motion_text.cell_w[i][c]);
  }
}

static void draw_motion_section(int x, int y, const DebugInputSnapshot *s, uint64_t now_us) {
  Tilt raw = tilt_from_vita(s->raw_accel);
  Tilt sent = tilt_from_dualsense(s->sent_accel);
  rebuild_motion(s, raw, sent, now_us);

  text(UI_FACE_T14, x, y, PANEL_LABEL_H, UI_TEXT_3, PANEL_MOTION_LABEL);
  const int hz_y = y + PANEL_HZ_Y;
  draw_horizon(x, hz_y, raw);
  draw_horizon(x + PANEL_HZ_SENT_X, hz_y, sent);
  const int cap_y = hz_y + PANEL_HZ_SIZE;
  text(UI_FACE_T14, x + (PANEL_HZ_SIZE - fixed.raw) / 2, cap_y, PANEL_LABEL_H, UI_TEXT_3,
       PANEL_RAW);
  text(UI_FACE_T14, x + PANEL_HZ_SENT_X + (PANEL_HZ_SIZE - fixed.sent) / 2, cap_y, PANEL_LABEL_H,
       UI_ACCENT, PANEL_SENT);

  const int tx = x + PANEL_TILT_X;
  int ty = y + PANEL_TILT_Y;
  text(UI_FACE_T14, tx, ty, PANEL_LABEL_H, UI_TEXT_3, PANEL_TILT_LABEL);
  ty += PANEL_LABEL_H;
  text(UI_FACE_T16, tx, ty, UI_T16_LINE, UI_TEXT_3, PANEL_RAW_PREFIX);
  text(UI_FACE_T16, tx + fixed.raw_prefix, ty, UI_T16_LINE, UI_TEXT, motion_text.raw_tilt);
  ty += UI_T16_LINE;
  text(UI_FACE_T16, tx, ty, UI_T16_LINE, UI_TEXT_3, PANEL_SENT_PREFIX);
  text(UI_FACE_T16, tx + fixed.sent_prefix, ty, UI_T16_LINE, UI_ACCENT, motion_text.sent_tilt);
  ty += UI_T16_LINE + UI_LW2;
  text(UI_FACE_T14, tx, ty, PANEL_LABEL_H, motion_text.differs ? UI_WARN : UI_OK,
       motion_text.differs ? PANEL_DIFFERS : PANEL_MATCHES);

  draw_table(x, y + PANEL_TABLE_Y);
}

static void draw_footer(int x, int y, uint32_t combo_held_ms) {
  fill_rect(x, y, PANEL_CONTENT_W, UI_LW1, UI_LINE_FAINT);
  const int cy = y + PANEL_FOOTER_H / 2;
  int fx = x;
  for (int i = 0; i < PANEL_PILL_COUNT; i++) {
    int w = fixed.pill[i] + 2 * PANEL_PILL_PAD_X;
    box_outline(fx, cy - PANEL_PILL_H / 2, w, PANEL_PILL_H, UI_LW1, UI_TEXT_2);
    text(UI_FACE_T14, fx + PANEL_PILL_PAD_X, cy - PANEL_PILL_H / 2, PANEL_PILL_H, UI_TEXT,
         PANEL_PILLS[i]);
    fx += w + PANEL_FOOTER_GAP;
  }
  text(UI_FACE_T14, fx, y, PANEL_FOOTER_H, UI_TEXT_2,
       combo_held_ms > 0 ? PANEL_HINT_HELD : PANEL_HINT_IDLE);

  const int bar_x = x + PANEL_CONTENT_W - PANEL_PROGRESS_W;
  const int bar_y = cy - PANEL_PROGRESS_H / 2;
  fill_rect(bar_x, bar_y, PANEL_PROGRESS_W, PANEL_PROGRESS_H, UI_LINE_FAINT);
  uint32_t held = combo_held_ms > INPUT_COMBO_HOLD_MS ? INPUT_COMBO_HOLD_MS : combo_held_ms;
  fill_rect(bar_x, bar_y, (int)(PANEL_PROGRESS_W * held / INPUT_COMBO_HOLD_MS), PANEL_PROGRESS_H,
            UI_TEXT);
}

void debug_input_panel_draw(void) {
  const DebugInputSnapshot snap = debug_input_snapshot;
  const uint64_t now_us = sceKernelGetProcessTimeWide();
  measure_fixed();
  update_ghost(&snap, now_us);

  const bool lit = snap.click.down || (snap.click.count > 0 && now_us - snap.click.last_press_us <
                                                                   PANEL_LIGHT_MIN_MS * 1000ULL);

  const DebugRect p = DEBUG_PANEL_RECT;
  UiRect box = {p.x, p.y, p.w, p.h};
  ui_shape9_draw(UI_SHAPE9_MD, box, UI_PANEL);
  ui_shape9_draw(UI_SHAPE9_MD_BORDER, box, UI_LINE_FAINT);

  const int x = p.x + DEBUG_PANEL_PAD_X;
  draw_header(x, p.y);
  draw_touch_section(x, p.y + PANEL_TOUCH_Y, &snap, lit, now_us);
  fill_rect(x, p.y + PANEL_RULE1_Y, PANEL_CONTENT_W, UI_LW1, UI_LINE_FAINT);
  draw_click_section(x, p.y + PANEL_CLICK_Y, &snap.click, lit);
  fill_rect(x, p.y + PANEL_RULE2_Y, PANEL_CONTENT_W, UI_LW1, UI_LINE_FAINT);
  draw_motion_section(x, p.y + PANEL_MOTION_Y, &snap, now_us);
  draw_footer(x, p.y + p.h - PANEL_FOOTER_H, snap.combo_held_ms);
}

#endif /* VITARPS5_DEBUG_TOOLS */
