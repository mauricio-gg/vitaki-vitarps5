// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the hint-row collapse rule and the banner reason
// shortening in vita/src/ui/ui_chrome_layout.c (issue #300). `./tools/build.sh test`
// only cross-compiles, so run these on the host:
//   cc -std=c99 -I vita/include test/ui_chrome_layout_tests.c \
//      vita/src/ui/ui_chrome_layout.c -o /tmp/ui_chrome_layout_tests && \
//      /tmp/ui_chrome_layout_tests

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui/ui_chrome_layout.h"

/** Test font: every character (not byte) is 10 px wide. */
static int measure_10px_per_char(const char *text, void *ctx) {
  (void)ctx;
  int chars = 0;
  for (; *text; text++) {
    if ((*(const unsigned char *)text & 0xC0) != 0x80)
      chars++;
  }
  return chars * 10;
}

/** Low-priority hints go first, rightmost first, and never past what is needed. */
static void test_hint_collapse_drops_low_priority_from_the_right(void) {
  const int widths[] = {100, 100, 100};
  const bool low[] = {false, true, true};
  bool keep[3];

  /* 100 + 24 + 100 + 24 + 100 = 348 fits exactly: nothing dropped. */
  assert(ui_hint_row_fit(widths, low, 3, 24, 348, keep) == 348);
  assert(keep[0] && keep[1] && keep[2]);

  /* One pixel short: the rightmost low-priority hint goes, the other stays. */
  assert(ui_hint_row_fit(widths, low, 3, 24, 347, keep) == 224);
  assert(keep[0] && keep[1] && !keep[2]);

  /* Too narrow for two: both low-priority hints go. */
  assert(ui_hint_row_fit(widths, low, 3, 24, 223, keep) == 100);
  assert(keep[0] && !keep[1] && !keep[2]);
}

/** Confirm, Cancel and the main action are never dropped, even when they do not fit. */
static void test_hint_collapse_never_drops_required_hints(void) {
  const int widths[] = {100, 100, 100};
  const bool low[] = {false, false, true};
  bool keep[3];

  assert(ui_hint_row_fit(widths, low, 3, 24, 50, keep) == 224);
  assert(keep[0] && keep[1] && !keep[2]);
}

/** A reason that fits is untouched; a long one is cut to fit, ending in the ellipsis. */
static void test_ellipsize_cuts_only_when_needed(void) {
  char out[64];

  ui_ellipsize_to_fit("Console disconnected", 200, measure_10px_per_char, NULL, out, sizeof(out));
  assert(strcmp(out, "Console disconnected") == 0);

  /* 20 chars = 200 px; 100 px allows 7 characters plus the 3-character ellipsis. */
  size_t len = ui_ellipsize_to_fit("Console disconnected", 100, measure_10px_per_char, NULL, out,
                                   sizeof(out));
  assert(strcmp(out, "Console...") == 0);
  assert(len == strlen(out));
  assert(measure_10px_per_char(out, NULL) <= 100);
}

/** The cut never lands inside a multi-byte character, and a tiny slot yields the bare ellipsis. */
static void test_ellipsize_respects_utf8_and_tiny_slots(void) {
  char out[64];

  /* "caf\xC3\xA9 au lait": cutting after 4 characters must keep the whole e-acute. */
  ui_ellipsize_to_fit("caf\xC3\xA9 au lait", 70, measure_10px_per_char, NULL, out, sizeof(out));
  assert(strcmp(out, "caf\xC3\xA9...") == 0);

  ui_ellipsize_to_fit("Console disconnected", 10, measure_10px_per_char, NULL, out, sizeof(out));
  assert(strcmp(out, UI_ELLIPSIS) == 0);
}

int main(void) {
  test_hint_collapse_drops_low_priority_from_the_right();
  test_hint_collapse_never_drops_required_hints();
  test_ellipsize_cuts_only_when_needed();
  test_ellipsize_respects_utf8_and_tiny_slots();
  printf("ui_chrome_layout tests passed\n");
  return 0;
}
