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

/** A code that fits the 160 px art must be drawn as large as it can be in whole pixels, and one
 * that does not fit even at 1 px per module must be refused (0), never drawn clipped. */
static void test_qr_module_size_fits_the_art_or_is_refused(void) {
  assert(ui_qr_module_px(25, 160) == 6);   /* 150 px of 160 */
  assert(ui_qr_module_px(80, 160) == 2);   /* exactly 160 px */
  assert(ui_qr_module_px(81, 160) == 1);   /* one module over the 2 px size */
  assert(ui_qr_module_px(160, 160) == 1);  /* the last code that fits */
  assert(ui_qr_module_px(161, 160) == 0);  /* does not fit: refused */
  assert(ui_qr_module_px(0, 160) == 0);
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

int main(void) {
  test_qr_module_size_fits_the_art_or_is_refused();
  test_keyboard_text_converts_and_never_overruns();
  puts("ui_login_tests: all passed");
  return 0;
}
