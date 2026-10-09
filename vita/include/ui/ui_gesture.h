// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
/**
 * @file ui_gesture.h
 * @brief Touch gesture rules with no SDK dependency (SPEC.md 4, 4.1, 2.0)
 *
 * The one place that decides whether a touch is a tap, a long-press or a swipe, and how many
 * whole swipe steps a displacement covers. Kept free of vita2d and psp2 so it can be checked
 * natively (test/ui_gesture_tests.c); ui_input.c feeds it the live touch every frame.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** A finger that moves further than this from touch-down is never also a tap. */
#define UI_TOUCH_DRAG_PX 8

/** A finger held still this long on a console row opens its Options (SPEC C02). */
#define UI_LONG_PRESS_MS 500

/** What a touch is on a given frame. */
typedef enum ui_gesture_kind_t {
  UI_GESTURE_NONE = 0,   /**< still undecided, or already consumed */
  UI_GESTURE_TAP,        /**< the finger lifted without moving or long-pressing */
  UI_GESTURE_LONG_PRESS, /**< held still for UI_LONG_PRESS_MS; reported on that one frame only */
  UI_GESTURE_SWIPE, /**< moved past UI_TOUCH_DRAG_PX; stays a swipe for the rest of the touch */
} UiGestureKind;

/** One frame of one touch, as the classifier needs it. */
typedef struct ui_gesture_touch_t {
  bool released;    /**< the finger lifted this frame (otherwise it is still down) */
  uint32_t held_ms; /**< time since touch-down */
  float dx;         /**< movement since touch-down */
  float dy;
  bool swiped;       /**< an earlier frame of this touch already became a swipe */
  bool long_pressed; /**< an earlier frame of this touch already fired a long-press */
} UiGestureTouch;

/**
 * ui_gesture_classify() - Decide what this frame of a touch is.
 * @touch: The touch this frame; NULL gives UI_GESTURE_NONE.
 *
 * Movement strictly over UI_TOUCH_DRAG_PX makes a swipe, and a swipe is never a tap or a
 * long-press. A still finger held UI_LONG_PRESS_MS fires one long-press while it is down; the
 * release after a long-press is nothing, not a tap.
 */
UiGestureKind ui_gesture_classify(const UiGestureTouch *touch);

/**
 * ui_gesture_swipe_steps() - Whole swipe steps in a displacement along one axis.
 * @displacement: Pixels moved since touch-down (signed).
 * @step_px:      Pixels per step; zero or less gives 0.
 *
 * Truncates toward zero, so -130 px at 64 px per step is -2.
 */
int ui_gesture_swipe_steps(float displacement, int step_px);
