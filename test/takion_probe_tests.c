// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) test for the window arithmetic behind PIPE/RECVTIME and PIPE/SOCKQ
// (lib/include/chiaki/takionprobe.h, ticket #413). `./tools/build.sh test` only cross-compiles,
// so run it on the host:
//   cc -std=c99 -Wall -Wextra -I lib/include test/takion_probe_tests.c -o /tmp/takion_probe_tests \
//      && /tmp/takion_probe_tests

#include <assert.h>
#include <stdio.h>

#include <chiaki/takionprobe.h>

/* catches: a remainder that clamps to 0 or wraps to ~1.8e19 when the packet classes were charged
 * more than the window's work time, which would hide that the buckets overlap or the clock is off. */
static void test_unaccounted_is_signed_and_exact(void) {
  assert(chiaki_takion_probe_unaccounted_us(1000, 300, 200, 100) == 400);
  assert(chiaki_takion_probe_unaccounted_us(600, 300, 200, 100) == 0);
  assert(chiaki_takion_probe_unaccounted_us(500, 300, 200, 100) == -100);
  assert(chiaki_takion_probe_unaccounted_us(0, 7000000000ULL, 0, 1) == -7000000001LL);
  assert(chiaki_takion_probe_unaccounted_us(0, 0, 0, 0) == 0);
}

/* catches: a division by zero (crash on the reporter's first window, or any window without
 * samples) and an average that wraps instead of saturating. */
static void test_avg_handles_no_samples_and_overflow(void) {
  assert(chiaki_takion_probe_avg(0, 0) == 0);
  assert(chiaki_takion_probe_avg(12345, 0) == 0);
  assert(chiaki_takion_probe_avg(10, 4) == 2);
  assert(chiaki_takion_probe_avg(100, 3) == 33);
  assert(chiaki_takion_probe_avg(UINT64_MAX, 1) == UINT32_MAX);
}

/* catches: a longest-span figure that is not the maximum, and a total that wraps to a small
 * number when published as 32 bits (a long stall would then read as a quick one). */
static void test_bucket_tracks_max_and_saturates(void) {
  ChiakiTakionTimeBucket bucket = {0};
  ChiakiTakionTimeWindow window;
  chiaki_takion_time_bucket_add(&bucket, 40);
  chiaki_takion_time_bucket_add(&bucket, 900);
  chiaki_takion_time_bucket_add(&bucket, 60);
  chiaki_takion_time_bucket_reduce(&bucket, &window);
  assert(window.n == 3 && window.us == 1000 && window.max_us == 900);

  chiaki_takion_time_bucket_add(&bucket, 5000000000ULL);
  chiaki_takion_time_bucket_reduce(&bucket, &window);
  assert(window.n == 4 && window.us == UINT32_MAX && window.max_us == UINT32_MAX);
}

/* catches: a wrong 16-bit wrap in the audio frame-gap count: a plain subtraction would report tens
 * of thousands of missing frames across the 0xffff -> 0 wrap, or none for a real gap. */
static void test_seq16_missing_handles_wrap(void) {
  assert(chiaki_takion_probe_seq16_missing(10, 11) == 0);
  assert(chiaki_takion_probe_seq16_missing(10, 14) == 3);
  assert(chiaki_takion_probe_seq16_missing(0xffff, 0) == 0);
  assert(chiaki_takion_probe_seq16_missing(0xfffe, 1) == 2);
  assert(chiaki_takion_probe_seq16_missing(0xffff, 5) == 5);
}

int main(void) {
  test_unaccounted_is_signed_and_exact();
  test_avg_handles_no_samples_and_overflow();
  test_bucket_tracks_max_and_saturates();
  test_seq16_missing_handles_wrap();
  printf("takion_probe_tests: all passed\n");
  return 0;
}
