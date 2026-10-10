/**
 * @file ui_category_bar.c
 * @brief C01 CategoryBar (SPEC.md C01)
 */

#include "ui/ui_category_bar.h"

#include "ui/ui_animation.h"
#include "ui/ui_gesture.h"
#include "ui/ui_motion.h"
#include "ui/ui_text.h"

/** Where an icon is in the slide. */
typedef struct cat_pose_t {
  float cx;
  float scale;
  float opacity;
  float label;
} CatPose;

/** Centre x of category @index while @focus is focused: focus at UI_CAT_X0, then fixed steps. */
static int category_center_x(int index, int focus) {
  int d = index - focus;
  if (d == 0)
    return UI_CAT_X0;
  if (d > 0)
    return UI_CAT_X0 + UI_CAT_FAR + (d - 1) * UI_CAT_STEP;
  return UI_CAT_X0 + d * UI_CAT_STEP;
}

/** Scale of category @index relative to its 48 px art. */
static float category_scale(int index, int focus) {
  if (index == focus)
    return UI_CAT_SCALE_ON;
  return index < focus ? UI_CAT_SCALE_LEFT : 1.0f;
}

/** Recompute every visible and hit rect for the current focus. */
static void layout_rects(UiCategoryBar *bar) {
  for (int i = 0; i < UI_CAT_COUNT; i++) {
    int size = (int)((float)UI_CAT_ART * category_scale(i, bar->focus));
    bar->visible[i].w = size;
    bar->visible[i].h = size;
    bar->visible[i].x = category_center_x(i, bar->focus) - size / 2;
    bar->visible[i].y = UI_CAT_Y - size / 2;
    bar->hit[i] = ui_rect_hit_from_visible(bar->visible[i], UI_CAT_HIT, UI_CAT_HIT);
  }
}

void ui_category_bar_init(UiCategoryBar *bar, vita2d_texture *const icons[UI_CAT_COUNT],
                          const char *const labels[UI_CAT_COUNT]) {
  for (int i = 0; i < UI_CAT_COUNT; i++) {
    bar->icons[i] = icons[i];
    bar->labels[i] = labels[i];
  }
  bar->focus = 0;
  bar->slide_start_us = 0;
  bar->swipe_active = false;
  bar->swipe_base = 0;
  layout_rects(bar);
}

void ui_category_bar_set_focus(UiCategoryBar *bar, int index) {
  bar->focus = index < 0 ? 0 : (index >= UI_CAT_COUNT ? UI_CAT_COUNT - 1 : index);
  bar->slide_start_us = 0;
  layout_rects(bar);
}

/** Settled pose of icon @index while @focus is focused. */
static CatPose pose_target(int index, int focus) {
  bool on = index == focus;
  return (CatPose){(float)category_center_x(index, focus), category_scale(index, focus),
                   on ? 1.0f : (float)UI_CAT_UNFOCUSED_PCT / 100.0f, on ? 1.0f : 0.0f};
}

/** Eased progress (0..1) of the slide; 1 when the bar is settled. */
static float slide_progress(const UiCategoryBar *bar) {
  if (!bar->slide_start_us)
    return 1.0f;
  return UI_EASE_OUT(ui_motion_progress(ui_anim_elapsed_ms(bar->slide_start_us), 0.0f, UI_D2_MS));
}

/** Pose of icon @index at slide progress @p. */
static CatPose pose_at(const UiCategoryBar *bar, int index, float p) {
  CatPose target = pose_target(index, bar->focus);
  if (p >= 1.0f)
    return target;
  return (CatPose){bar->from_cx[index] + (target.cx - bar->from_cx[index]) * p,
                   bar->from_scale[index] + (target.scale - bar->from_scale[index]) * p,
                   bar->from_opacity[index] + (target.opacity - bar->from_opacity[index]) * p,
                   bar->from_label[index] + (target.label - bar->from_label[index]) * p};
}

/** Draw the label of icon @index centred under @cx at opacity @k. */
static void draw_label(const UiCategoryBar *bar, int index, float cx, float k) {
  const char *label = bar->labels[index];
  int label_x = (int)(cx + 0.5f) - ui_text_face_width(UI_FACE_T16, label) / 2;
  int label_y = UI_CAT_Y + UI_CAT_BOX / 2 + UI_CAT_LABEL_GAP;
  ui_text_draw_face_centered_v(UI_FACE_T16, label_x, label_y, UI_T16_LINE,
                               ui_color_scale_alpha(UI_TEXT, k), label);
}

void ui_category_bar_draw(const UiCategoryBar *bar) {
  const float p = slide_progress(bar);
  CatPose poses[UI_CAT_COUNT];
  for (int i = 0; i < UI_CAT_COUNT; i++)
    poses[i] = pose_at(bar, i, p);

  vita2d_texture *glow = ui_glow_texture();
  if (glow) {
    vita2d_draw_texture_tint(glow, poses[bar->focus].cx - (float)UI_LIST_GLOW / 2.0f,
                             (float)(UI_CAT_Y - UI_LIST_GLOW / 2),
                             ui_layer_color(ui_color_scale_alpha(UI_GLOW, p)));
  }

  for (int i = 0; i < UI_CAT_COUNT; i++) {
    vita2d_texture *icon = bar->icons[i];
    if (!icon)
      continue;
    float size = (float)UI_CAT_ART * poses[i].scale;
    float scale = size / (float)vita2d_texture_get_width(icon);
    vita2d_draw_texture_tint_scale(icon, poses[i].cx - size / 2.0f, (float)UI_CAT_Y - size / 2.0f,
                                   scale, scale,
                                   ui_layer_color(ui_color_scale_alpha(UI_TEXT, poses[i].opacity)));
  }

  for (int i = 0; i < UI_CAT_COUNT; i++) {
    if (poses[i].label > 0.0f)
      draw_label(bar, i, poses[i].cx, poses[i].label);
  }
}

/**
 * swipe_target() - Category a horizontal swipe points at: the focus when the swipe began plus
 * whole steps of finger travel (swipe left = next), clamped. The swipe may start anywhere on the
 * screen; it counts only when it locked to the horizontal axis. Returns the current focus when
 * no such swipe is under way.
 */
static int swipe_target(UiCategoryBar *bar, const UiTouch *touch) {
  if (!touch->down || touch->swipe_axis != UI_GESTURE_AXIS_HORIZONTAL) {
    bar->swipe_active = false;
    return bar->focus;
  }
  if (!bar->swipe_active) {
    bar->swipe_active = true;
    bar->swipe_base = bar->focus;
  }
  const int target = bar->swipe_base + ui_gesture_swipe_steps(-touch->dx, UI_CAT_SWIPE_PX);
  return target < 0 ? 0 : (target >= UI_CAT_COUNT ? UI_CAT_COUNT - 1 : target);
}

UiEvent ui_category_bar_input(UiCategoryBar *bar, const UiInput *in) {
  int target = swipe_target(bar, &in->touch);

  if (target == bar->focus) {
    if ((in->pressed & UI_BTN_L) || (in->repeat & UI_BTN_LEFT)) {
      target--;
    } else if ((in->pressed & UI_BTN_R) || (in->repeat & UI_BTN_RIGHT)) {
      target++;
    } else if (ui_touch_tap(in)) {
      for (int i = 0; i < UI_CAT_COUNT; i++) {
        if (ui_rect_contains(bar->hit[i], in->touch.x, in->touch.y)) {
          target = i;
          break;
        }
      }
    }
  }

  if (target < 0 || target >= UI_CAT_COUNT || target == bar->focus)
    return UI_EVENT_NONE;

  /* Start the slide from where every icon is right now, so a change mid-slide never snaps. */
  const float p = slide_progress(bar);
  for (int i = 0; i < UI_CAT_COUNT; i++) {
    CatPose now = pose_at(bar, i, p);
    bar->from_cx[i] = now.cx;
    bar->from_scale[i] = now.scale;
    bar->from_opacity[i] = now.opacity;
    bar->from_label[i] = now.label;
  }
  bar->focus = target;
  bar->slide_start_us = ui_anim_now_us();
  layout_rects(bar);
  return UI_EVENT_MOVED;
}
