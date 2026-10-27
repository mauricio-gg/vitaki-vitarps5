// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
/**
 * @file front_touch_zone.h
 * @brief Start-zone rule for front touches (GH #460)
 *
 * A front touch belongs to the zone it started in until the finger lifts. The outcome of the
 * touch (buttons pressed, L2/R2 held, touchpad touch or not) is worked out once, from where the
 * finger first appeared, and the same outcome is handed back on every later pass whatever the
 * finger does. Sliding never presses another button, never turns a button touch into a touchpad
 * touch, and never ends a touchpad touch. It holds no SDK types, so it is checked natively
 * (test/front_touch_zone_tests.c); host_input.c applies the outcome.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "controller.h"

/** Number of distinct Vita touch ids tracked at once (SCE_TOUCH_MAX_REPORT; host_input.c asserts
 * it). */
#define FRONT_TOUCH_ZONE_MAX_TOUCHES 8

/* Bits of FrontTouchOutcome.zones: which of the geometric front zones the start position lies in.
 */
#define FRONT_TOUCH_ZONE_RIGHT_HALF 0x01
#define FRONT_TOUCH_ZONE_UR_ARC 0x02
#define FRONT_TOUCH_ZONE_LR_ARC 0x04
#define FRONT_TOUCH_ZONE_LEFT_HALF 0x08
#define FRONT_TOUCH_ZONE_UL_ARC 0x10
#define FRONT_TOUCH_ZONE_LL_ARC 0x20
#define FRONT_TOUCH_ZONE_CENTER 0x40

/** What a front touch does for as long as it is down. */
typedef struct front_touch_outcome_t {
  uint32_t buttons;   /**< Chiaki button bits to press. */
  bool l2;            /**< Hold L2 fully. */
  bool r2;            /**< Hold R2 fully. */
  bool to_touchpad;   /**< The touch is a touchpad touch that follows the finger. */
  int grid;           /**< VitakiCtrlIn of the start grid zone (VITAKI_CTRL_IN_NONE if none). */
  int grid_out;       /**< The mapping of that grid zone, 0 when none. */
  unsigned int zones; /**< FRONT_TOUCH_ZONE_* bits of the start position. */
} FrontTouchOutcome;

/** One tracked finger: the Vita touch id and the outcome decided when it first appeared. */
typedef struct front_touch_entry_t {
  bool active;
  bool seen; /**< Reported during the current pass. */
  uint8_t vita_touch_id;
  FrontTouchOutcome outcome;
} FrontTouchEntry;

/** Outcomes of the fingers currently down; zero-initialise (or call front_touch_zone_init). */
typedef struct front_touch_zones_t {
  FrontTouchEntry entry[FRONT_TOUCH_ZONE_MAX_TOUCHES];
} FrontTouchZones;

/** Resets @p z so no finger is tracked. */
void front_touch_zone_init(FrontTouchZones *z);

/** Starts an input pass: call before the first front touch of the pass. */
void front_touch_zone_begin_pass(FrontTouchZones *z);

/**
 * Returns the outcome for one finger reported this pass.
 *
 * The first time @p vita_touch_id is reported the outcome is resolved from (@p x, @p y) with
 * @p vcmi and kept; later passes return that same outcome and ignore the position.
 *
 * @param max_w  Largest x the panel reports; @p max_h the same for y.
 * @return The latched outcome (valid until the next call), or NULL when the table is full.
 */
const FrontTouchOutcome *front_touch_zone_latch(FrontTouchZones *z, uint8_t vita_touch_id, int x,
                                                int y, int max_w, int max_h,
                                                const VitakiCtrlMapInfo *vcmi);

/** Ends an input pass: forgets every finger that was not reported, so its id is judged fresh. */
void front_touch_zone_end_pass(FrontTouchZones *z);
