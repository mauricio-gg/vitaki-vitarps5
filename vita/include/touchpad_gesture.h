// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
/**
 * @file touchpad_gesture.h
 * @brief Timing rules for front-touch taps that click the DualSense touchpad (GH #454)
 *
 * The feedback sender only compares controller states when it wakes, so a click must stay on for
 * tens of milliseconds and must happen while a finger is still on the pad. This module decides,
 * once per input-loop frame, when to press the click and when to let the touches go. It holds no
 * SDK types, so it is checked natively (test/touchpad_gesture_tests.c); host_input.c feeds it the
 * live touches and applies what it answers.
 *
 * Rules:
 *  - single tap: the touch stays active at its last position, the click is held for
 *    TOUCHPAD_CLICK_HOLD_MS, then click and touch are released together;
 *  - two-finger tap: two unmoved touches that went down within TOUCHPAD_TWO_FINGER_DOWN_WINDOW_MS
 *    of each other give ONE click, pressed when the second lifts while both are still active;
 *    the first to lift is held up to TOUCHPAD_TWO_FINGER_LIFT_WINDOW_MS for the second;
 *  - a touch that moved is a drag: it is released at once and never clicks.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Number of touches the gesture tracker follows (Chiaki's touch limit). */
#define TOUCHPAD_GESTURE_SLOTS 2

/** How long the touchpad click stays pressed. */
#define TOUCHPAD_CLICK_HOLD_MS 50

/** Two touches count as put down "together" when the second started within this of the first. */
#define TOUCHPAD_TWO_FINGER_DOWN_WINDOW_MS 150

/** A lifted touch waits this long for its partner to lift; after that it is let go without a click.
 */
#define TOUCHPAD_TWO_FINGER_LIFT_WINDOW_MS 200

/** What the host knows about one touch slot this frame. */
typedef struct touchpad_gesture_input_t {
  bool active; /**< The host still has a Chiaki touch started for this slot. */
  bool seen;   /**< A finger is on the screen for this slot right now. */
  bool moved;  /**< The finger has moved past the tap threshold since it went down. */
} TouchpadGestureInput;

/** What the host must do this frame. */
typedef struct touchpad_gesture_output_t {
  bool click;                           /**< Set the touchpad button bit. */
  bool release[TOUCHPAD_GESTURE_SLOTS]; /**< Stop this slot's Chiaki touch and free the slot. */
} TouchpadGestureOutput;

typedef enum touchpad_gesture_phase_t {
  TOUCHPAD_PHASE_IDLE,
  TOUCHPAD_PHASE_DOWN,               /**< Finger on the screen. */
  TOUCHPAD_PHASE_HELD,               /**< Lifted unmoved, kept active, waiting for its partner. */
  TOUCHPAD_PHASE_RELEASE_AFTER_CLICK /**< Lifted; released when the running click ends. */
} TouchpadGesturePhase;

/** Gesture state; zero-initialise (or call touchpad_gesture_init) before use. */
typedef struct touchpad_gesture_t {
  TouchpadGesturePhase phase[TOUCHPAD_GESTURE_SLOTS];
  uint64_t down_us[TOUCHPAD_GESTURE_SLOTS];
  uint64_t lift_us[TOUCHPAD_GESTURE_SLOTS];
  bool click_running;
  uint64_t click_end_us;
} TouchpadGesture;

/** Resets @p g to the idle state. */
void touchpad_gesture_init(TouchpadGesture *g);

/**
 * Advances the gesture rules by one input-loop frame.
 *
 * @param g      Gesture state, updated in place.
 * @param now_us Monotonic time in microseconds.
 * @param in     One entry per slot, describing this frame's touches.
 * @param out    Receives the click bit and the slots to release; fully overwritten.
 */
void touchpad_gesture_step(TouchpadGesture *g, uint64_t now_us,
                           const TouchpadGestureInput in[TOUCHPAD_GESTURE_SLOTS],
                           TouchpadGestureOutput *out);
