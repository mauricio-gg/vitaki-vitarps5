// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
/**
 * @file input_combos.h
 * @brief The hold-to-fire button combos the input thread watches (GH #474)
 *
 * Two combos, both "hold three buttons for INPUT_COMBO_HOLD_MS":
 *  - mute: L + R + Select toggles the microphone mute. Watched only while the mic is live or
 *    muted; otherwise L, R and Select go to the game as usual. Present in every build.
 *  - panel: L + R + Triangle toggles the debug Input test panel. Testing builds only.
 * Neither fires while Start is down (L + R + Start is the stream-exit combo), and a hold must be
 * released before it can fire again. While a combo is held its buttons are removed from what the
 * game sees, from the first pass they are down.
 *
 * The caller passes its own button bit masks, so this file has no SDK types and is checked
 * natively (test/input_combos_tests.c).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** How long a combo must be held before it fires. */
#define INPUT_COMBO_HOLD_MS 500

/** The platform's bit for each button the combos use. */
typedef struct input_combo_buttons_t {
  unsigned l, r, select, start, triangle;
} InputComboButtons;

/** One combo's hold. */
typedef struct input_combo_hold_t {
  bool holding;
  bool fired; /**< Already fired during this hold. */
  uint64_t start_us;
} InputComboHold;

/** State across passes; zero-initialise. */
typedef struct input_combos_t {
  InputComboHold mute;
  InputComboHold panel;
} InputCombos;

/** What one pass decided. */
typedef struct input_combo_result_t {
  bool mute_fired;        /**< The mute hold completed on this pass (once per hold). */
  bool panel_fired;       /**< The panel hold completed on this pass (once per hold). */
  uint32_t panel_held_ms; /**< Panel hold so far, capped at INPUT_COMBO_HOLD_MS; 0 when not held. */
} InputComboResult;

/**
 * input_combos_step() - Feed one input pass.
 * @c: State.
 * @map: The platform's button bits.
 * @buttons: Buttons held this pass.
 * @now_us: Time of this pass.
 * @mute_active: The mute combo is watched (the mic is live or muted).
 * @panel_enabled: The panel combo is watched (testing builds).
 * @out: Filled with what fired.
 * Return: @buttons without the buttons of any combo that is being held.
 */
unsigned input_combos_step(InputCombos *c, const InputComboButtons *map, unsigned buttons,
                           uint64_t now_us, bool mute_active, bool panel_enabled,
                           InputComboResult *out);
