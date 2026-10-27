// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "touchpad_gesture.h"

#include <string.h>

#define US_PER_MS 1000ULL

/* Starts a click at @p now_us unless one is already running (then the lift joins it). */
static void start_click_if_idle(TouchpadGesture *g, uint64_t now_us) {
  if (g->click_running)
    return;
  g->click_running = true;
  g->click_end_us = now_us + TOUCHPAD_CLICK_HOLD_MS * US_PER_MS;
}

/* True when slot @p other went down within the two-finger window of slot @p slot and is a
 * candidate partner: still on the screen, or lifted no longer than the lift window ago, and not
 * moved (a moved partner is a drag). */
static bool is_tap_partner(const TouchpadGesture *g, int slot, int other, uint64_t now_us,
                           const TouchpadGestureInput in[TOUCHPAD_GESTURE_SLOTS]) {
  TouchpadGesturePhase phase = g->phase[other];
  if (phase != TOUCHPAD_PHASE_DOWN && phase != TOUCHPAD_PHASE_HELD)
    return false;
  if (in[other].moved)
    return false;
  if (phase == TOUCHPAD_PHASE_HELD &&
      now_us - g->lift_us[other] >= TOUCHPAD_TWO_FINGER_LIFT_WINDOW_MS * US_PER_MS)
    return false;
  uint64_t a = g->down_us[slot];
  uint64_t b = g->down_us[other];
  uint64_t apart = a > b ? a - b : b - a;
  return apart <= TOUCHPAD_TWO_FINGER_DOWN_WINDOW_MS * US_PER_MS;
}

/* Handles a finger that has just lifted from @p slot. */
static void handle_lift(TouchpadGesture *g, int slot, uint64_t now_us,
                        const TouchpadGestureInput in[TOUCHPAD_GESTURE_SLOTS],
                        TouchpadGestureOutput *out) {
  int other = 1 - slot;

  if (in[slot].moved) {
    out->release[slot] = true;
    g->phase[slot] = TOUCHPAD_PHASE_IDLE;
    return;
  }

  if (g->click_running) {
    g->phase[slot] = TOUCHPAD_PHASE_RELEASE_AFTER_CLICK;
    return;
  }

  if (is_tap_partner(g, slot, other, now_us, in)) {
    if (g->phase[other] == TOUCHPAD_PHASE_DOWN) {
      /* First of a two-finger tap to lift: hold it for the partner. */
      g->phase[slot] = TOUCHPAD_PHASE_HELD;
      g->lift_us[slot] = now_us;
      return;
    }
    /* Second to lift: one click for both touches. */
    start_click_if_idle(g, now_us);
    g->phase[slot] = TOUCHPAD_PHASE_RELEASE_AFTER_CLICK;
    g->phase[other] = TOUCHPAD_PHASE_RELEASE_AFTER_CLICK;
    return;
  }

  start_click_if_idle(g, now_us);
  g->phase[slot] = TOUCHPAD_PHASE_RELEASE_AFTER_CLICK;
}

/* Lets go of a held touch whose partner moved, lifted as a drag, or took too long. */
static void expire_held(TouchpadGesture *g, int slot, uint64_t now_us,
                        const TouchpadGestureInput in[TOUCHPAD_GESTURE_SLOTS],
                        TouchpadGestureOutput *out) {
  int other = 1 - slot;
  bool partner_down = g->phase[other] == TOUCHPAD_PHASE_DOWN;
  bool partner_moved = partner_down && in[other].moved;
  bool too_late = now_us - g->lift_us[slot] >= TOUCHPAD_TWO_FINGER_LIFT_WINDOW_MS * US_PER_MS;

  if (!partner_down || partner_moved || too_late) {
    out->release[slot] = true;
    g->phase[slot] = TOUCHPAD_PHASE_IDLE;
  }
}

void touchpad_gesture_init(TouchpadGesture *g) {
  memset(g, 0, sizeof(*g));
}

void touchpad_gesture_step(TouchpadGesture *g, uint64_t now_us,
                           const TouchpadGestureInput in[TOUCHPAD_GESTURE_SLOTS],
                           TouchpadGestureOutput *out) {
  memset(out, 0, sizeof(*out));

  /* The host freed a slot (or never had it): forget whatever we tracked for it. */
  for (int i = 0; i < TOUCHPAD_GESTURE_SLOTS; i++) {
    if (!in[i].active)
      g->phase[i] = TOUCHPAD_PHASE_IDLE;
  }

  /* Click over: release the touches that waited for it. */
  if (g->click_running && now_us >= g->click_end_us) {
    g->click_running = false;
    for (int i = 0; i < TOUCHPAD_GESTURE_SLOTS; i++) {
      if (g->phase[i] == TOUCHPAD_PHASE_RELEASE_AFTER_CLICK) {
        out->release[i] = true;
        g->phase[i] = TOUCHPAD_PHASE_IDLE;
      }
    }
  }

  for (int i = 0; i < TOUCHPAD_GESTURE_SLOTS; i++) {
    if (g->phase[i] == TOUCHPAD_PHASE_IDLE && in[i].active && in[i].seen) {
      g->phase[i] = TOUCHPAD_PHASE_DOWN;
      g->down_us[i] = now_us;
    }
  }

  for (int i = 0; i < TOUCHPAD_GESTURE_SLOTS; i++) {
    if (g->phase[i] == TOUCHPAD_PHASE_DOWN && !in[i].seen)
      handle_lift(g, i, now_us, in, out);
  }

  for (int i = 0; i < TOUCHPAD_GESTURE_SLOTS; i++) {
    if (g->phase[i] == TOUCHPAD_PHASE_HELD)
      expire_held(g, i, now_us, in, out);
  }

  out->click = g->click_running;
}
