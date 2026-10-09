// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the phone login helpers (issue #304): the QR module size and the
// keyboard text conversion. `./tools/build.sh test` only cross-compiles, so run these on the host:
//   cc -std=c99 -Wall -Wextra -I vita/include test/ui_login_tests.c vita/src/ui/ui_utf16.c \
//      -o /tmp/ui_login_tests && /tmp/ui_login_tests

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui/ui_qr_panel.h"
#include "ui/ui_utf16.h"

/** The module size is what keeps the QR code (with its 2-module quiet zone) inside the slot. A code
 * drawn larger than the slot is clipped and does not scan; one drawn smaller than the whole pixels
 * allow wastes the slot; one that does not fit at 1 px per module must be refused (0). */
static void test_qr_module_size_fills_the_slot_or_is_refused(void) {
  const int quiet = 2;
  const int slot = 216;

  /* The real phone-login URL: 591 characters, a 93 module code (version 19). */
  assert(ui_qr_module_px(93, quiet, slot) == 2); /* 194 px plate; 3 px would be 291 px, clipped */

  for (int size = 21; size <= slot - 2 * quiet; size += 4) {
    const int px = ui_qr_module_px(size, quiet, slot);
    assert(px >= 1);
    assert(px * (size + 2 * quiet) <= slot);         /* never clipped */
    assert((px + 1) * (size + 2 * quiet) > slot);    /* never smaller than it could be */
  }

  assert(ui_qr_module_px(212, quiet, slot) == 1);  /* the last code that fits: 216 px */
  assert(ui_qr_module_px(213, quiet, slot) == 0);  /* 217 px does not fit: refused */
  assert(ui_qr_module_px(0, quiet, slot) == 0);
}

/** The pasted redirect URL must reach psn_auth intact, and a buffer that is too small must lose
 * whole characters from the end, never write past it or end in half a character. */
static void test_keyboard_text_converts_and_never_overruns(void) {
  const uint16_t url[] = {'h', 't', 't', 'p', 0x00E9, 0x20AC, 0};
  char out[16];
  ui_utf16_to_utf8(url, 16, out, sizeof(out));
  assert(strcmp(out, "http\xC3\xA9\xE2\x82\xAC") == 0);

  char guard[8];
  memset(guard, 'X', sizeof(guard));
  ui_utf16_to_utf8(url, 16, guard, 7); /* room for 6 bytes: the euro sign (3 bytes) does not fit */
  assert(strcmp(guard, "http\xC3\xA9") == 0);
  assert(guard[7] == 'X');
}

/** The text a filter keyboard is prefilled with must come back as typed, accents included, and
 * must stop before the buffer ends: a garbled prefill would silently change the user's filter
 * the next time they press Done. */
static void test_prefill_text_round_trips_and_never_overruns(void) {
  uint16_t units[8];
  char back[16];

  ui_utf8_to_utf16("caf\xC3\xA9 \xE2\x82\xAC", units, 8);
  ui_utf16_to_utf8(units, 8, back, sizeof(back));
  assert(strcmp(back, "caf\xC3\xA9 \xE2\x82\xAC") == 0);

  ui_utf8_to_utf16("abcdefghij", units, 4);
  assert(units[3] == 0 && units[2] == 'c');
}

int main(void) {
  test_qr_module_size_fills_the_slot_or_is_refused();
  test_keyboard_text_converts_and_never_overruns();
  test_prefill_text_round_trips_and_never_overruns();
  puts("ui_login_tests: all passed");
  return 0;
}
