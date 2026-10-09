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
  UI_GESTURE_NONE = 0,   /**< still undecided, or the touch was consumed by a screen */
  UI_GESTURE_TAP,        /**< the finger lifted without swiping and without being consumed */
  UI_GESTURE_LONG_PRESS, /**< held still for UI_LONG_PRESS_MS; reported on that one frame only */
  UI_GESTURE_SWIPE, /**< moved past UI_TOUCH_DRAG_PX; stays a swipe for the rest of the touch */
} UiGestureKind;

/** The axis a swipe is locked to for the rest of its touch. */
typedef enum ui_gesture_axis_t {
  UI_GESTURE_AXIS_NONE = 0, /**< the touch has not become a swipe yet */
  UI_GESTURE_AXIS_HORIZONTAL,
  UI_GESTURE_AXIS_VERTICAL,
} UiGestureAxis;

/** One frame of one touch, as the classifier needs it. */
typedef struct ui_gesture_touch_t {
  bool released;    /**< the finger lifted this frame (otherwise it is still down) */
  uint32_t held_ms; /**< time since touch-down */
  float dx;         /**< movement since touch-down */
  float dy;
  bool swiped;           /**< an earlier frame of this touch already became a swipe */
  bool long_press_fired; /**< an earlier frame of this touch already fired its long-press */
  bool consumed; /**< a screen acted on this touch (e.g. opened Options): no tap on release */
} UiGestureTouch;

/**
 * ui_gesture_classify() - Decide what this frame of a touch is.
 * @touch: The touch this frame; NULL gives UI_GESTURE_NONE.
 *
 * Movement strictly over UI_TOUCH_DRAG_PX makes a swipe, and a swipe is never a tap or a
 * long-press. A still finger held UI_LONG_PRESS_MS fires one long-press while it is down. The
 * release of any other touch is a tap, however long it was held, unless a screen consumed the
 * touch (it acted on the long-press), in which case the release is nothing.
 */
UiGestureKind ui_gesture_classify(const UiGestureTouch *touch);

/**
 * ui_gesture_swipe_axis() - Pick the axis a swipe locks to, from its movement since touch-down.
 * @dx: Horizontal movement since touch-down.
 * @dy: Vertical movement since touch-down.
 *
 * Horizontal when |dx| is strictly greater than |dy|, otherwise vertical (a perfect diagonal
 * scrolls the list). Called once, on the frame the touch first becomes a swipe; the caller
 * keeps the answer until the finger lifts, so a diagonal swipe never moves both axes.
 */
UiGestureAxis ui_gesture_swipe_axis(float dx, float dy);

/**
 * ui_gesture_swipe_steps() - Whole swipe steps in a displacement along one axis.
 * @displacement: Pixels moved since touch-down (signed).
 * @step_px:      Pixels per step; zero or less gives 0.
 *
 * Truncates toward zero, so -130 px at 64 px per step is -2.
 */
int ui_gesture_swipe_steps(float displacement, int step_px);
