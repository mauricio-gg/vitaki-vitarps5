// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "input_test_click.h"

#include <string.h>

#define US_PER_MS 1000ULL

void input_test_click_init(InputTestClick *c) {
  memset(c, 0, sizeof(*c));
}

void input_test_click_step(InputTestClick *c, bool sent_down, uint64_t now_us) {
  if (sent_down == c->down)
    return;
  c->down = sent_down;
  if (sent_down) {
    c->count++;
    c->last_press_us = now_us;
    return;
  }
  c->last_release_us = now_us;
  c->last_hold_ms = (uint32_t)((now_us - c->last_press_us) / US_PER_MS);
  c->hold_valid = true;
}
