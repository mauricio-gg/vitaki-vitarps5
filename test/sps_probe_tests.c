// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) test for the SPS detector and repeat counter in vita/src/sps_probe.c
// (ticket #413). `./tools/build.sh test` only cross-compiles, so run it on the host:
//   cc -std=c99 -Wall -Wextra -I vita/include test/sps_probe_tests.c vita/src/sps_probe.c \
//      -o /tmp/sps_probe_tests && /tmp/sps_probe_tests

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "sps_probe.h"

/* The stream header of the first hardware log (26017450183): a 4-byte start code, a 28-byte SPS
 * (67 ... 54), then a 4-byte start code and the PPS (68 ee 3c 80). */
static const uint8_t HEADER[] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x4d, 0x40, 0x20, 0x91, 0x8a, 0x03,
                                 0xc0, 0x45, 0xfb, 0xc0, 0x5a, 0x80, 0x80, 0x80, 0xa0, 0x00, 0x00,
                                 0xfa, 0x40, 0x00, 0x75, 0x30, 0x11, 0x34, 0x38, 0x74, 0x54, 0x00,
                                 0x00, 0x00, 0x01, 0x68, 0xee, 0x3c, 0x80};
#define HEADER_SPS_LEN 28u

static void test_locates_the_sps_in_the_real_header(void) {
  size_t off = 0;
  size_t len = 0;
  assert(sps_probe_locate(HEADER, sizeof(HEADER), &off, &len));
  assert(off == 4 && len == HEADER_SPS_LEN);
  assert(HEADER[off] == 0x67 && HEADER[off + len - 1] == 0x54);
}

static void test_three_byte_start_code_and_unterminated_sps(void) {
  const uint8_t buf[] = {0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1e, 0x95, 0xa0};
  size_t off = 0;
  size_t len = 0;
  assert(sps_probe_locate(buf, sizeof(buf), &off, &len));
  assert(off == 3 && len == sizeof(buf) - 3); /* no next start code: runs to the buffer end */
}

static void test_other_buffers_are_not_an_sps(void) {
  size_t off = 0;
  size_t len = 0;
  const uint8_t idr[] = {0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00, 0x10, 0xff};
  const uint8_t pps[] = {0x00, 0x00, 0x00, 0x01, 0x68, 0xee, 0x3c, 0x80, 0x00};
  const uint8_t aud_first[] = {0x00, 0x00, 0x00, 0x01, 0x09, 0x10, 0x00, 0x00, 0x00, 0x01,
                               0x67, 0x4d, 0x40, 0x20, 0x91, 0x8a, 0x03, 0xc0};
  const uint8_t no_start_code[] = {0x67, 0x4d, 0x40, 0x20, 0x91, 0x8a, 0x03, 0xc0, 0x00};
  const uint8_t too_short[] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x4d, 0x40};
  assert(!sps_probe_locate(idr, sizeof(idr), &off, &len));
  assert(!sps_probe_locate(pps, sizeof(pps), &off, &len));
  assert(!sps_probe_locate(aud_first, sizeof(aud_first), &off, &len)); /* documented limit */
  assert(!sps_probe_locate(no_start_code, sizeof(no_start_code), &off, &len));
  assert(!sps_probe_locate(too_short, sizeof(too_short), &off, &len));
  assert(!sps_probe_locate(NULL, 0, &off, &len));
}

static void test_first_sps_is_kept_and_repeats_are_counted(void) {
  SpsProbeData d;
  uint8_t changed[sizeof(HEADER)];
  uint8_t longer[sizeof(HEADER) + 1];

  sps_probe_reset();
  assert(sps_probe_version() == 0);
  assert(sps_probe_snapshot(&d, 1) && d.first_len == 0 && d.seen_again == 0);

  sps_probe_observe(HEADER, sizeof(HEADER));
  assert(sps_probe_snapshot(&d, 1));
  assert(d.first_len == HEADER_SPS_LEN && d.seen_again == 0 && d.differed == 0);
  assert(memcmp(d.first, HEADER + 4, HEADER_SPS_LEN) == 0);
  assert((sps_probe_version() & 1) == 0);

  sps_probe_observe(HEADER, sizeof(HEADER)); /* same SPS again */
  assert(sps_probe_snapshot(&d, 1) && d.seen_again == 1 && d.differed == 0);

  memcpy(changed, HEADER, sizeof(HEADER)); /* same length, one byte different */
  changed[10] ^= 0x01;
  sps_probe_observe(changed, sizeof(changed));
  assert(sps_probe_snapshot(&d, 1) && d.seen_again == 2 && d.differed == 1);

  memcpy(longer, HEADER, 32); /* a longer SPS (extra byte before the next start code) */
  longer[32] = 0x80;
  memcpy(longer + 33, HEADER + 32, sizeof(HEADER) - 32);
  sps_probe_observe(longer, sizeof(longer));
  assert(sps_probe_snapshot(&d, 1) && d.seen_again == 3 && d.differed == 2);

  const uint8_t idr[] = {0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00, 0x10, 0xff};
  sps_probe_observe(idr, sizeof(idr)); /* a frame without an SPS changes nothing */
  assert(sps_probe_snapshot(&d, 1) && d.seen_again == 3 && d.differed == 2);
  assert(memcmp(d.first, HEADER + 4, HEADER_SPS_LEN) == 0); /* still the first one */
}

static void test_a_new_session_starts_over(void) {
  SpsProbeData d;
  uint8_t other[sizeof(HEADER)];
  memcpy(other, HEADER, sizeof(HEADER));
  other[6] = 0x64;

  sps_probe_reset();
  sps_probe_observe(HEADER, sizeof(HEADER));
  sps_probe_observe(HEADER, sizeof(HEADER));
  sps_probe_reset();
  sps_probe_observe(other, sizeof(other));
  assert(sps_probe_snapshot(&d, 1));
  assert(d.first[2] == 0x64 && d.seen_again == 0 && d.differed == 0);
}

int main(void) {
  test_locates_the_sps_in_the_real_header();
  test_three_byte_start_code_and_unterminated_sps();
  test_other_buffers_are_not_an_sps();
  test_first_sps_is_kept_and_repeats_are_counted();
  test_a_new_session_starts_over();
  printf("sps_probe_tests: all passed\n");
  return 0;
}
