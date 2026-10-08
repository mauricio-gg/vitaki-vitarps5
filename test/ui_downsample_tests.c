// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) test for the popup freeze's 2 x 2 averaging in
// vita/src/ui/ui_downsample.c (ticket #303). `./tools/build.sh test` only cross-compiles, so run
// it on the host:
//   cc -std=c99 -I vita/include test/ui_downsample_tests.c vita/src/ui/ui_downsample.c \
//      -o /tmp/ui_downsample_tests && /tmp/ui_downsample_tests

#include <assert.h>
#include <stdio.h>

#include "ui/ui_downsample.h"

/** Each channel is the rounded mean of its four source values, and the channels stay apart. */
static void test_channels_are_averaged_independently_and_rounded(void) {
  /* Pixels are 0xAABBGGRR. Red: 0, 0, 0, 3 -> 0.75 -> 1. Green: 10, 20, 30, 40 -> 25.
   * Blue: 255 in all four -> 255. Alpha: 0, 0, 0, 1 -> 0.25 -> 0. */
  const uint32_t row0[2] = {0x00FF0A00u, 0x00FF1400u};
  const uint32_t row1[2] = {0x00FF1E00u, 0x01FF2803u};
  uint32_t out[1];

  ui_downsample_row_pair(row0, row1, 1, out, 0);
  assert(out[0] == 0x00FF1901u);
}

/** Every output pixel takes its own block, left to right, and the or-mask forces opacity. */
static void test_blocks_are_independent_and_mask_forces_alpha(void) {
  const uint32_t row0[4] = {0x00000000u, 0x00000000u, 0x00646464u, 0x00646464u};
  const uint32_t row1[4] = {0x00000000u, 0x00000000u, 0x00646464u, 0x00646464u};
  uint32_t out[2];

  ui_downsample_row_pair(row0, row1, 2, out, 0xFF000000u);
  assert(out[0] == 0xFF000000u);
  assert(out[1] == 0xFF646464u);
}

int main(void) {
  test_channels_are_averaged_independently_and_rounded();
  test_blocks_are_independent_and_mask_forces_alpha();
  printf("ui_downsample_tests: ok\n");
  return 0;
}
