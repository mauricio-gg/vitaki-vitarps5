// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) test for the stream stats value rules in vita/src/ui/ui_stream_stats.c
// (ticket #306). `./tools/build.sh test` only cross-compiles, so run it on the host:
//   cc -std=c99 -I vita/include test/ui_stream_stats_tests.c vita/src/ui/ui_stream_stats.c \
//      -o /tmp/ui_stream_stats_tests && /tmp/ui_stream_stats_tests

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui/ui_stream_stats.h"

#define SECOND_US 1000000ULL

/** Format with the given inputs and return the two strings through @lat and @fps. */
static void format(uint64_t now, uint64_t last, uint32_t rtt, uint32_t in, uint32_t target,
                   uint32_t negotiated, char lat[32], char fps[32]) {
  ui_stream_stats_format(now, last, rtt, in, target, negotiated, lat, 32, fps, 32);
}

/** Metrics exactly 3.0 s old still show the latency; a moment older reads N/A. */
static void test_latency_goes_stale_just_after_three_seconds(void) {
  char lat[32], fps[32];
  const uint64_t last = 10 * SECOND_US;

  format(last + 3 * SECOND_US, last, 42, 60, 60, 60, lat, fps);
  assert(strcmp(lat, "42 ms") == 0);
  assert(!ui_stream_stats_metrics_stale(last + 3 * SECOND_US, last));

  format(last + 3 * SECOND_US + 1, last, 42, 60, 60, 60, lat, fps);
  assert(strcmp(lat, "N/A") == 0);
  assert(ui_stream_stats_metrics_stale(last + 3 * SECOND_US + 1, last));
}

/** No value (rtt 0) or metrics never updated read N/A, however fresh the clock looks. */
static void test_latency_is_na_without_a_value(void) {
  char lat[32], fps[32];

  format(5 * SECOND_US, 5 * SECOND_US, 0, 60, 60, 60, lat, fps);
  assert(strcmp(lat, "N/A") == 0);

  format(1 * SECOND_US, 0, 42, 60, 60, 60, lat, fps);
  assert(strcmp(lat, "N/A") == 0);
}

/** FPS reads "in / target"; the target falls back to the negotiated rate; no target shows "in". */
static void test_fps_shows_incoming_against_target(void) {
  char lat[32], fps[32];

  format(0, 0, 0, 57, 60, 30, lat, fps);
  assert(strcmp(fps, "57 / 60") == 0);

  format(0, 0, 0, 29, 0, 30, lat, fps);
  assert(strcmp(fps, "29 / 30") == 0);

  format(0, 0, 0, 29, 0, 0, lat, fps);
  assert(strcmp(fps, "29") == 0);
}

/** Nothing coming in reads N/A, even when a target is known. */
static void test_fps_is_na_without_incoming_frames(void) {
  char lat[32], fps[32];

  format(0, 0, 0, 0, 60, 60, lat, fps);
  assert(strcmp(fps, "N/A") == 0);
}

int main(void) {
  test_latency_goes_stale_just_after_three_seconds();
  test_latency_is_na_without_a_value();
  test_fps_shows_incoming_against_target();
  test_fps_is_na_without_incoming_frames();
  printf("ui_stream_stats_tests: ok\n");
  return 0;
}
