// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "input_combos.h"

/* Advances one hold. Returns true on the pass it completes. *held_ms is the hold so far, capped,
 * or 0 when @down is false. */
static bool hold_step(InputComboHold *h, bool down, uint64_t now_us, uint32_t *held_ms) {
  if (!down) {
    *h = (InputComboHold){0};
    *held_ms = 0;
    return false;
  }
  if (!h->holding) {
    h->holding = true;
    h->start_us = now_us;
  }
  const uint64_t elapsed_ms = (now_us - h->start_us) / 1000ULL;
  const bool completes = !h->fired && elapsed_ms >= INPUT_COMBO_HOLD_MS;
  if (completes)
    h->fired = true;
  *held_ms = elapsed_ms < INPUT_COMBO_HOLD_MS ? (uint32_t)elapsed_ms : INPUT_COMBO_HOLD_MS;
  return completes;
}

unsigned input_combos_step(InputCombos *c, const InputComboButtons *map, unsigned buttons,
                           uint64_t now_us, bool mute_active, bool panel_enabled,
                           InputComboResult *out) {
  const unsigned mute_buttons = map->l | map->r | map->select;
  const unsigned panel_buttons = map->l | map->r | map->triangle;
  /* With Start also down it is the stream-exit combo; leave that alone. */
  const bool start_down = (buttons & map->start) != 0;

  const bool mute_down = mute_active && !start_down && (buttons & mute_buttons) == mute_buttons;
  const bool panel_down =
      panel_enabled && !start_down && (buttons & panel_buttons) == panel_buttons;

  uint32_t mute_held_ms;
  out->mute_fired = hold_step(&c->mute, mute_down, now_us, &mute_held_ms);
  out->panel_fired = hold_step(&c->panel, panel_down, now_us, &out->panel_held_ms);

  if (mute_down)
    buttons &= ~mute_buttons;
  if (panel_down)
    buttons &= ~panel_buttons;
  return buttons;
}
