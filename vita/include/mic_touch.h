// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
/**
 * @file mic_touch.h
 * @brief Which front touches belong to the on-screen mic control (GH #474)
 *
 * The control is either the bottom-centre mute button or the small top-left corner icon (the UI
 * decides which, see ui_mic.h). A touch that starts inside its touch area is the control's: it is
 * swallowed for its whole life and never reaches the game, ahead of any touchpad or button zone
 * mapping. A touch that starts outside stays the game's even if it slides in. The area is judged
 * once, when a touch first appears. Present in every build. No SDK types, so it is checked
 * natively (test/mic_touch_tests.c).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Screen is 960 x 544; front touch coordinates (0..1919 x 0..1087) are 2x screen pixels. */
#define MIC_SCREEN_W 960
#define MIC_SCREEN_H 544
#define MIC_TOUCH_PER_SCREEN_PX 2

/** The bottom-centre button: a 64 x 32 pill, 16 px above the bottom edge, centred. */
#define MIC_BTN_W 64
#define MIC_BTN_H 32
#define MIC_BTN_Y (MIC_SCREEN_H - 16 - MIC_BTN_H)
/** The pill's width while its label shows ("Mic on", "Mic muted"). */
#define MIC_BTN_LABEL_ON_W 120
#define MIC_BTN_LABEL_MUTED_W 144
/** Touch area at rest: the pill plus 16 px each side and 8 px above and below (96 x 48). */
#define MIC_BTN_TAP_PAD_X 16
#define MIC_BTN_TAP_PAD_Y 8
/** Touch area while the label shows: the pill plus this on every side. */
#define MIC_BTN_LABEL_TAP_PAD 8

/** The top-left corner icon: a 32 px circle at (32, 32), with an 8 px margin for touch. */
#define MIC_CORNER_X 32
#define MIC_CORNER_Y 32
#define MIC_CORNER_SIZE 32
#define MIC_CORNER_TAP_PAD 8

/** Which control is on screen right now. */
typedef enum mic_touch_mode_t {
  MIC_TOUCH_MODE_NONE = 0, /**< Nothing: every touch is the game's. */
  MIC_TOUCH_MODE_BUTTON,   /**< The bottom-centre button. */
  MIC_TOUCH_MODE_CORNER,   /**< The corner icon. */
} MicTouchMode;

/**
 * mic_touch_hit() - Is the raw front-touch point inside the control's touch area?
 * @mode: The control on screen.
 * @label_w: The button's width while its label shows, or 0 at rest (button mode only).
 * @touch_x, @touch_y: Raw front-touch coordinates.
 */
bool mic_touch_hit(MicTouchMode mode, int label_w, int touch_x, int touch_y);

/** The Vita's front panel reports at most this many touches. */
#define MIC_TOUCH_SLOTS 6

/** One touch the filter has judged. */
typedef struct mic_touch_slot_t {
  bool active;
  bool seen; /**< Reported on the current pass. */
  bool swallowed;
  uint8_t id;
} MicTouchSlot;

/** Filter state across passes; zero-initialise. */
typedef struct mic_touch_tracker_t {
  MicTouchSlot slots[MIC_TOUCH_SLOTS];
} MicTouchTracker;

/** Call at the start of each front-touch pass. */
void mic_touch_pass_begin(MicTouchTracker *t);

/**
 * mic_touch_judge() - Judge one reported touch.
 * @id: The touch id the panel reported.
 * @mode, @label_w: As for mic_touch_hit().
 * @tap_out: Set true on the pass a swallowed touch first appears (the tap), false otherwise.
 * Return: true when the touch belongs to the control and must be hidden from the game.
 */
bool mic_touch_judge(MicTouchTracker *t, uint8_t id, int touch_x, int touch_y, MicTouchMode mode,
                     int label_w, bool *tap_out);

/** Call at the end of each pass: touches not reported on it have lifted. */
void mic_touch_pass_end(MicTouchTracker *t);
