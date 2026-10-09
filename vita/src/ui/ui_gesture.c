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
    return touch->long_pressed ? UI_GESTURE_NONE : UI_GESTURE_TAP;
  if (!touch->long_pressed && touch->held_ms >= UI_LONG_PRESS_MS)
    return UI_GESTURE_LONG_PRESS;
  return UI_GESTURE_NONE;
}

int ui_gesture_swipe_steps(float displacement, int step_px) {
  if (step_px <= 0)
    return 0;
  return (int)(displacement / (float)step_px);
}
