// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
/**
 * @file input_test_click.h
 * @brief Click statistics for the debug "Input test" panel (GH #458)
 *
 * The panel shows what is SENT to the console, so a click is measured on the sent button bits:
 * from the frame the touchpad bit goes on to the frame it goes off. The input loop feeds one
 * sample per pass. No SDK types, so it is checked natively (test/input_test_tests.c).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Click statistics; zero-initialise (or call input_test_click_init) before use. */
typedef struct input_test_click_t {
  bool down;                /**< The sent state has the click on right now. */
  uint32_t count;           /**< Presses since the stream started (once per press). */
  bool hold_valid;          /**< A click has finished, so last_hold_ms means something. */
  uint32_t last_hold_ms;    /**< Hold time of the last finished click. */
  uint64_t last_press_us;   /**< When the last press began (valid when count > 0). */
  uint64_t last_release_us; /**< When the last click ended (valid when hold_valid). */
} InputTestClick;

/** Resets @p c to "no click seen yet". */
void input_test_click_init(InputTestClick *c);

/**
 * Feeds one input-loop pass.
 *
 * @param c         Statistics, updated in place.
 * @param sent_down True when the touchpad bit is set in the state handed to Chiaki.
 * @param now_us    Monotonic time in microseconds.
 */
void input_test_click_step(InputTestClick *c, bool sent_down, uint64_t now_us);
