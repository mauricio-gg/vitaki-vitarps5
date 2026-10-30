/**
 * @file ui_mic.c
 * @brief The on-screen microphone control of the stream overlay (see ui_mic.h)
 */

#include "ui/ui_mic.h"

#include <math.h>

#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "context.h"
#include "mic.h"
#include "ui/ui_bake.h"
#include "ui/ui_component.h"
#include "ui/ui_pill.h"
#include "ui/ui_shapes.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"

/* ============================================================================
 * Icon: a Material-style mic on a 24 x 24 grid, baked white and tinted at draw time
 * ============================================================================ */

#define ICON_GRID 24.0f
#define ICON_STROKE 1.7f
/* Capsule: a segment with round ends. */
#define CAPSULE_X 12.0f
#define CAPSULE_TOP_Y 6.0f
#define CAPSULE_BOTTOM_Y 11.0f
#define CAPSULE_R 3.2f
/* Cradle: the lower half of a circle around the capsule's bottom, then a stem and a foot. */
#define CRADLE_CY 11.0f
#define CRADLE_R 6.6f
#define STEM_TOP_Y (CRADLE_CY + CRADLE_R)
#define STEM_BOTTOM_Y 21.0f
#define FOOT_HALF_W 3.6f
/* The mute slash, drawn only on the muted icon. */
#define SLASH_A_X 4.5f
#define SLASH_A_Y 4.0f
#define SLASH_B_X 19.5f
#define SLASH_B_Y 20.0f
#define SLASH_STROKE 2.0f

static vita2d_texture *s_icon_live = NULL;
static vita2d_texture *s_icon_muted = NULL;

/** Coverage (0..1) of a shape whose signed distance to its edge is @sd pixels (negative inside). */
static float coverage(float sd) {
  return 0.5f - sd;
}

/** Alpha of the mic icon at (@px, @py) of a @art x @art texture; @ctx points to a bool that is
 * true for the slashed (muted) icon. */
static float icon_alpha(float px, float py, const void *ctx) {
  const bool slashed = *(const bool *)ctx;
  const float scale = ICON_GRID / (float)UI_MIC_ICON;
  const float x = px * scale;
  const float y = py * scale;
  const float px_per_unit = 1.0f / scale;

  float best = coverage(
      (ui_bake_distance_to_segment(x, y, CAPSULE_X, CAPSULE_TOP_Y, CAPSULE_X, CAPSULE_BOTTOM_Y) -
       CAPSULE_R) *
      px_per_unit);

  /* The cradle is only its lower half; above the circle's centre line it ends at its two tips. */
  float cradle_d;
  if (y >= CRADLE_CY) {
    cradle_d = fabsf(hypotf(x - CAPSULE_X, y - CRADLE_CY) - CRADLE_R);
  } else {
    const float left = hypotf(x - (CAPSULE_X - CRADLE_R), y - CRADLE_CY);
    const float right = hypotf(x - (CAPSULE_X + CRADLE_R), y - CRADLE_CY);
    cradle_d = left < right ? left : right;
  }
  const float stem_d =
      ui_bake_distance_to_segment(x, y, CAPSULE_X, STEM_TOP_Y, CAPSULE_X, STEM_BOTTOM_Y);
  const float foot_d = ui_bake_distance_to_segment(x, y, CAPSULE_X - FOOT_HALF_W, STEM_BOTTOM_Y,
                                                   CAPSULE_X + FOOT_HALF_W, STEM_BOTTOM_Y);
  float line_d = cradle_d < stem_d ? cradle_d : stem_d;
  line_d = line_d < foot_d ? line_d : foot_d;
  const float line_a = coverage((line_d - ICON_STROKE / 2.0f) * px_per_unit);
  best = best > line_a ? best : line_a;

  if (slashed) {
    const float slash_d =
        ui_bake_distance_to_segment(x, y, SLASH_A_X, SLASH_A_Y, SLASH_B_X, SLASH_B_Y);
    const float slash_a = coverage((slash_d - SLASH_STROKE / 2.0f) * px_per_unit);
    best = best > slash_a ? best : slash_a;
  }
  return best;
}

void ui_mic_init(void) {
  if (s_icon_live)
    return;
  static const bool LIVE = false;
  static const bool MUTED = true;
  s_icon_live = ui_bake_white(UI_MIC_ICON, UI_MIC_ICON, icon_alpha, &LIVE);
  s_icon_muted = ui_bake_white(UI_MIC_ICON, UI_MIC_ICON, icon_alpha, &MUTED);
}

/* ============================================================================
 * State
 * ============================================================================ */

static const char TEXT_ON[] = "Mic on";
static const char TEXT_MUTED[] = "Mic muted";
static UiPillPart s_failed_parts[] = {{.text = "Mic unavailable"}};
#define FAILED_PART_COUNT ((int)(sizeof(s_failed_parts) / sizeof(s_failed_parts[0])))

/** Set by any thread, consumed by ui_mic_tick(): 0 for none, else the MicSource plus 1. */
static volatile int s_toggle_request = 0;
/** The touch area as the input thread should see it; written only by ui_mic_tick(). */
static volatile MicTouchMode s_touch_mode = MIC_TOUCH_MODE_NONE;
static volatile int s_touch_label_w = 0;

/** When the label was last asked for; 0 when it is not showing. */
static uint64_t s_label_start_us = 0;
/** When the mic was first seen failed this stream; 0 while it has not. */
static uint64_t s_failed_since_us = 0;
/** Animated width and opacity of the button, and when they last moved. */
static bool s_anim_valid = false;
static float s_anim_w = MIC_BTN_W;
static float s_anim_alpha = 0.0f;
static uint64_t s_anim_us = 0;
/** Measured once on first draw; 0 until then. */
static int s_text_on_w = 0;
static int s_text_muted_w = 0;
static int s_failed_w = 0;

void ui_mic_on_stream_start(void) {
  s_toggle_request = 0;
  s_touch_mode = MIC_TOUCH_MODE_NONE;
  s_touch_label_w = 0;
  s_label_start_us = 0;
  s_failed_since_us = 0;
  s_anim_valid = false;
}

void ui_mic_on_stream_stop(void) {
  s_toggle_request = 0;
  s_touch_mode = MIC_TOUCH_MODE_NONE;
  s_touch_label_w = 0;
}

void ui_mic_request_toggle(MicSource source) {
  s_toggle_request = (int)source + 1;
}

/** True while the label is still showing at @now_us. */
static bool label_showing(uint64_t now_us) {
  return s_label_start_us != 0 && now_us - s_label_start_us < UI_MIC_LABEL_MS * 1000ULL;
}

void ui_mic_tick(void) {
  const uint64_t now_us = sceKernelGetProcessTimeWide();
  const int request = __atomic_exchange_n(&s_toggle_request, 0, __ATOMIC_SEQ_CST);

  if (!context.config.microphone) {
    s_touch_mode = MIC_TOUCH_MODE_NONE;
    s_touch_label_w = 0;
    return;
  }

  MicState state = vitamic_state();
  if (state == MIC_STATE_FAILED && s_failed_since_us == 0)
    s_failed_since_us = now_us;

  if (request && (state == MIC_STATE_LIVE || state == MIC_STATE_MUTED)) {
    vitamic_set_muted(state == MIC_STATE_LIVE, (MicSource)(request - 1));
    state = vitamic_state();
    /* The corner icon has no label; the button shows what the toggle did. */
    if (context.config.show_mic_button)
      s_label_start_us = now_us;
  }

  if (state != MIC_STATE_LIVE && state != MIC_STATE_MUTED) {
    s_touch_mode = MIC_TOUCH_MODE_NONE;
    s_touch_label_w = 0;
    return;
  }
  if (!context.config.show_mic_button) {
    s_touch_mode = MIC_TOUCH_MODE_CORNER;
    s_touch_label_w = 0;
    return;
  }
  s_touch_label_w = !label_showing(now_us)
                        ? 0
                        : (state == MIC_STATE_MUTED ? MIC_BTN_LABEL_MUTED_W : MIC_BTN_LABEL_ON_W);
  s_touch_mode = MIC_TOUCH_MODE_BUTTON;
}

MicTouchMode ui_mic_touch_mode(void) {
  return s_touch_mode;
}

int ui_mic_touch_label_w(void) {
  return s_touch_label_w;
}

/* ============================================================================
 * Draw
 * ============================================================================ */

/** Draw @icon at (@x, @y) tinted @color through the layer opacity. */
static void draw_icon(vita2d_texture *icon, int x, int y, uint32_t color) {
  if (icon)
    vita2d_draw_texture_tint(icon, (float)x, (float)y, ui_layer_color(color));
}

/** Move @cur towards @target by the share of the gap @dt_ms covers (exponential ease, so a new
 * target in mid-flight just bends the motion). Snaps when closer than @snap. */
static float approach(float cur, float target, float dt_ms, float snap) {
  const float k = 1.0f - expf(-dt_ms / (float)UI_MIC_ANIM_TAU_MS);
  const float next = cur + (target - cur) * k;
  return fabsf(target - next) < snap ? target : next;
}

/** Draw the bottom-centre button: eased width and opacity, icon, and the label while it shows. */
static void draw_button(MicState state, uint64_t now_us) {
  const bool muted = state == MIC_STATE_MUTED;
  const bool label = label_showing(now_us);
  const int label_w = muted ? MIC_BTN_LABEL_MUTED_W : MIC_BTN_LABEL_ON_W;
  const float target_w = label ? (float)label_w : (float)MIC_BTN_W;
  const float rest_pct = muted ? UI_MIC_REST_MUTED_PCT : UI_MIC_REST_LIVE_PCT;
  const float target_alpha = label ? 1.0f : rest_pct / 100.0f;

  if (!s_anim_valid) {
    s_anim_valid = true;
    s_anim_w = target_w;
    s_anim_alpha = target_alpha;
  } else {
    const float dt_ms = (float)(now_us - s_anim_us) / 1000.0f;
    s_anim_w = approach(s_anim_w, target_w, dt_ms, 0.5f);
    s_anim_alpha = approach(s_anim_alpha, target_alpha, dt_ms, 0.01f);
  }
  s_anim_us = now_us;

  if (s_text_on_w == 0) {
    s_text_on_w = ui_text_face_width(UI_FACE_T16, TEXT_ON);
    s_text_muted_w = ui_text_face_width(UI_FACE_T16, TEXT_MUTED);
  }
  const char *text = muted ? TEXT_MUTED : TEXT_ON;
  const int text_w = muted ? s_text_muted_w : s_text_on_w;
  const uint32_t ink = muted ? UI_ERR : UI_TEXT;

  const int w = (int)(s_anim_w + 0.5f);
  const int x = (MIC_SCREEN_W - w) / 2;
  /* How far the pill has grown towards the label width: 0 at rest, 1 when fully expanded. */
  float grown = (s_anim_w - (float)MIC_BTN_W) / (float)(label_w - MIC_BTN_W);
  grown = grown < 0.0f ? 0.0f : (grown > 1.0f ? 1.0f : grown);

  ui_layer_set_alpha(s_anim_alpha);
  ui_shape3_draw(UI_SHAPE3_PILL_32, x, MIC_BTN_Y, w, UI_HUD);

  const float content_w = (float)UI_MIC_ICON + grown * (float)(UI_MIC_ICON_GAP + text_w);
  const int icon_x = x + (int)(((float)w - content_w) / 2.0f);
  draw_icon(muted ? s_icon_muted : s_icon_live, icon_x, MIC_BTN_Y + (MIC_BTN_H - UI_MIC_ICON) / 2,
            ink);
  if (grown > 0.05f) {
    ui_layer_set_alpha(s_anim_alpha * grown);
    ui_text_draw_face_centered_v(UI_FACE_T16, icon_x + UI_MIC_ICON + UI_MIC_ICON_GAP, MIC_BTN_Y,
                                 MIC_BTN_H, ink, text);
  }
  ui_layer_set_alpha(1.0f);
}

/** Draw the corner icon: a dark disc, a red ring and slashed icon when muted. */
static void draw_corner(MicState state) {
  const bool muted = state == MIC_STATE_MUTED;
  const float cx = (float)(MIC_CORNER_X + MIC_CORNER_SIZE / 2);
  const float cy = (float)(MIC_CORNER_Y + MIC_CORNER_SIZE / 2);
  const float r = (float)(MIC_CORNER_SIZE / 2);
  if (muted) {
    vita2d_draw_fill_circle(cx, cy, r, ui_layer_color(UI_ERR));
    vita2d_draw_fill_circle(cx, cy, r - 1.0f, ui_layer_color(UI_PANEL));
  } else {
    vita2d_draw_fill_circle(cx, cy, r, ui_layer_color(UI_HUD));
  }
  draw_icon(muted ? s_icon_muted : s_icon_live, MIC_CORNER_X + (MIC_CORNER_SIZE - UI_MIC_ICON) / 2,
            MIC_CORNER_Y + (MIC_CORNER_SIZE - UI_MIC_ICON) / 2, muted ? UI_ERR : UI_TEXT);
}

/** Draw the "Mic unavailable" pill where the button would be, for UI_MIC_FAILED_MS and then a
 * linear fade; it has no touch area. */
static void draw_failed(uint64_t now_us) {
  if (s_failed_since_us == 0)
    return;
  const uint64_t visible_us = UI_MIC_FAILED_MS * 1000ULL;
  const uint64_t fade_us = UI_STREAM_HINT_FADE_MS * 1000ULL;
  const uint64_t elapsed_us = now_us - s_failed_since_us;
  if (elapsed_us >= visible_us + fade_us)
    return;
  if (s_failed_w == 0)
    s_failed_w = ui_pill_plain_layout(s_failed_parts, FAILED_PART_COUNT);
  ui_layer_set_alpha(
      elapsed_us <= visible_us ? 1.0f : 1.0f - (float)(elapsed_us - visible_us) / (float)fade_us);
  ui_pill_plain_draw((MIC_SCREEN_W - s_failed_w) / 2, MIC_BTN_Y, s_failed_w, s_failed_parts,
                     FAILED_PART_COUNT);
  ui_layer_set_alpha(1.0f);
}

void ui_mic_draw(void) {
  if (!context.config.microphone)
    return;
  const uint64_t now_us = sceKernelGetProcessTimeWide();
  const MicState state = vitamic_state();
  if (state == MIC_STATE_FAILED) {
    draw_failed(now_us);
    return;
  }
  if (state != MIC_STATE_LIVE && state != MIC_STATE_MUTED)
    return;
  if (context.config.show_mic_button)
    draw_button(state, now_us);
  else
    draw_corner(state);
}
