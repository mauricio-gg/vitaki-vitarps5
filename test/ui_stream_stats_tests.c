// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) test for the stream stats FPS value rule in vita/src/ui/ui_stream_stats.c
// (tickets #306, #293). `./tools/build.sh test` only cross-compiles, so run it on the host:
//   cc -std=c99 -I vita/include test/ui_stream_stats_tests.c vita/src/ui/ui_stream_stats.c \
//      -o /tmp/ui_stream_stats_tests && /tmp/ui_stream_stats_tests

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui/ui_stream_stats.h"

/** FPS reads "in / target"; the target falls back to the negotiated rate; no target shows "in". */
static void test_fps_shows_incoming_against_target(void) {
  char fps[32];

  ui_stream_stats_format(57, 60, 30, fps, sizeof(fps));
  assert(strcmp(fps, "57 / 60") == 0);

  ui_stream_stats_format(29, 0, 30, fps, sizeof(fps));
  assert(strcmp(fps, "29 / 30") == 0);

  ui_stream_stats_format(29, 0, 0, fps, sizeof(fps));
  assert(strcmp(fps, "29") == 0);
}

/** Nothing coming in reads N/A, even when a target is known. */
static void test_fps_is_na_without_incoming_frames(void) {
  char fps[32];

  ui_stream_stats_format(0, 60, 60, fps, sizeof(fps));
  assert(strcmp(fps, "N/A") == 0);
}

int main(void) {
  test_fps_shows_incoming_against_target();
  test_fps_is_na_without_incoming_frames();
  printf("ui_stream_stats_tests: ok\n");
  return 0;
}
