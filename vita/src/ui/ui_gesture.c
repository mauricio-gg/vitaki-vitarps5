// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
/** @file ui_gesture.c @brief Tap / long-press / swipe rules (see ui_gesture.h). */

#include "ui/ui_gesture.h"

UiGestureKind ui_gesture_classify(const UiGestureTouch *touch) {
  if (!touch)
    return UI_GESTURE_NONE;

  const float drag = (float)UI_TOUCH_DRAG_PX;
  if (touch->swiped || (touch->dx * touch->dx + touch->dy * touch->dy) > drag * drag)
    return UI_GESTURE_SWIPE;

  if (touch->released)
    return touch->consumed ? UI_GESTURE_NONE : UI_GESTURE_TAP;
  if (!touch->long_press_fired && touch->held_ms >= UI_LONG_PRESS_MS)
    return UI_GESTURE_LONG_PRESS;
  return UI_GESTURE_NONE;
}

UiGestureAxis ui_gesture_swipe_axis(float dx, float dy) {
  const float abs_dx = dx < 0.0f ? -dx : dx;
  const float abs_dy = dy < 0.0f ? -dy : dy;
  return abs_dx > abs_dy ? UI_GESTURE_AXIS_HORIZONTAL : UI_GESTURE_AXIS_VERTICAL;
}

int ui_gesture_swipe_steps(float displacement, int step_px) {
  if (step_px <= 0)
    return 0;
  return (int)(displacement / (float)step_px);
}
