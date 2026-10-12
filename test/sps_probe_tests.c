// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) test for the SPS detector, repeat counter and bounded SPS reader in
// vita/src/sps_probe.c (ticket #413). `./tools/build.sh test` only cross-compiles, so run it on
// the host, ideally also with -fsanitize=address,undefined -g:
//   cc -std=c99 -Wall -Wextra -I vita/include test/sps_probe_tests.c vita/src/sps_probe.c \
//      -o /tmp/sps_probe_tests && /tmp/sps_probe_tests

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "sps_probe.h"

/* The stream header of the first hardware log (26017450183): a 4-byte start code, a 28-byte SPS
 * (67 ... 54), then a 4-byte start code and the PPS (68 ee 3c 80). */
static const uint8_t HEADER[] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x4d, 0x40, 0x20, 0x91, 0x8a,
                                 0x03, 0xc0, 0x45, 0xfb, 0xc0, 0x5a, 0x80, 0x80, 0x80, 0xa0,
                                 0x00, 0x00, 0xfa, 0x40, 0x00, 0x75, 0x30, 0x11, 0x34, 0x38,
                                 0x74, 0x54, 0x00, 0x00, 0x00, 0x01, 0x68, 0xee, 0x3c, 0x80};
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
  const uint8_t aud_first[] = {0x00, 0x00, 0x00, 0x01, 0x09, 0x10, 0x00, 0x00, 0x00,
                               0x01, 0x67, 0x4d, 0x40, 0x20, 0x91, 0x8a, 0x03, 0xc0};
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

/* Bit writer for building SPS bytes (including malformed ones) in the tests. */
typedef struct {
  uint8_t bytes[SPS_PROBE_CAP * 2];
  uint32_t nbits;
} Writer;

static void put_bits(Writer *w, uint32_t value, unsigned n) {
  for (unsigned i = 0; i < n; i++, w->nbits++) {
    assert(w->nbits < sizeof(w->bytes) * 8);
    if ((value >> (n - 1 - i)) & 1u)
      w->bytes[w->nbits >> 3] |= (uint8_t)(0x80u >> (w->nbits & 7));
  }
}

static void put_ue(Writer *w, uint32_t value) {
  uint64_t code = (uint64_t)value + 1;
  unsigned len = 0;
  while ((code >> len) > 1)
    len++;
  put_bits(w, 0, len);
  for (int i = (int)len; i >= 0; i--)
    put_bits(w, (uint32_t)((code >> i) & 1u), 1);
}

typedef struct {
  uint8_t profile;
  uint32_t poc_type;
  uint32_t poc_cycle;     /* entries written = poc_cycle, so a huge count has data behind it */
  bool scaling;           /* profile 100: scaling lists 0, 2, 4, 6 present, 0 and 4 end early */
  int hrd_cpb_cnt_minus1; /* -1: no VUI; otherwise a VUI with a NAL HRD of this size */
  uint32_t width_minus1;
} Spec;

/* Writes the SPS NAL for `spec` with emulation-prevention bytes inserted; returns its length. */
static uint32_t build_sps(const Spec *spec, uint8_t *out) {
  Writer w;
  memset(&w, 0, sizeof(w));
  put_bits(&w, 0x67, 8);
  put_bits(&w, spec->profile, 8);
  put_bits(&w, 0x40, 8);
  put_bits(&w, 30, 8);
  put_ue(&w, 0);
  if (spec->profile == 100) {
    put_ue(&w, 1);
    put_ue(&w, 0);
    put_ue(&w, 0);
    put_bits(&w, 0, 1);
    put_bits(&w, spec->scaling ? 1 : 0, 1);
    for (int i = 0; spec->scaling && i < 8; i++) {
      put_bits(&w, i % 2 == 0 ? 1 : 0, 1);
      if (i % 2 != 0)
        continue;
      if (i == 0 || i == 4) {
        put_ue(&w, 16); /* se(-8): nextScale 0, the list ends here */
      } else {
        for (int j = 0; j < (i < 6 ? 16 : 64); j++)
          put_ue(&w, 0);
      }
    }
  }
  put_ue(&w, 0);
  put_ue(&w, spec->poc_type);
  if (spec->poc_type == 0) {
    put_ue(&w, 0);
  } else if (spec->poc_type == 1) {
    put_bits(&w, 0, 1);
    put_ue(&w, 0);
    put_ue(&w, 0);
    put_ue(&w, spec->poc_cycle);
    for (uint32_t i = 0; i < spec->poc_cycle; i++)
      put_ue(&w, 0);
  }
  put_ue(&w, 4);
  put_bits(&w, 0, 1);
  put_ue(&w, spec->width_minus1);
  put_ue(&w, 0);
  put_bits(&w, 1, 1);
  put_bits(&w, 1, 1);
  put_bits(&w, 0, 1);
  put_bits(&w, spec->hrd_cpb_cnt_minus1 >= 0 ? 1 : 0, 1);
  if (spec->hrd_cpb_cnt_minus1 >= 0) {
    put_bits(&w, 0, 5);
    put_bits(&w, 1, 1); /* nal_hrd_parameters_present_flag */
    put_ue(&w, (uint32_t)spec->hrd_cpb_cnt_minus1);
    put_bits(&w, 0, 8);
    for (int i = 0; i <= spec->hrd_cpb_cnt_minus1; i++) {
      put_ue(&w, 0);
      put_ue(&w, 0);
      put_bits(&w, 0, 1);
    }
    put_bits(&w, 0, 20);
    put_bits(&w, 0, 1); /* vcl_hrd_parameters_present_flag */
    put_bits(&w, 0, 1); /* low_delay_hrd_flag */
    put_bits(&w, 0, 1); /* pic_struct_present_flag */
    put_bits(&w, 1, 1); /* bitstream_restriction_flag */
    put_bits(&w, 1, 1);
    for (int i = 0; i < 4; i++)
      put_ue(&w, 0);
    put_ue(&w, 2);
    put_ue(&w, 5);
  }
  put_bits(&w, 1, 1); /* rbsp_stop_one_bit; the rest of the byte stays zero */

  uint32_t n = 0;
  unsigned zeros = 0;
  for (uint32_t i = 0; i < (w.nbits + 7) / 8; i++) {
    if (zeros >= 2 && w.bytes[i] <= 3) {
      out[n++] = 0x03;
      zeros = 0;
    }
    out[n++] = w.bytes[i];
    zeros = w.bytes[i] == 0 ? zeros + 1 : 0;
  }
  assert(n <= SPS_PROBE_CAP);
  return n;
}

static void test_hardware_sps_gives_the_values_worked_out_by_hand(void) {
  SpsFields f;
  assert(sps_probe_parse(HEADER + 4, HEADER_SPS_LEN, HEADER_SPS_LEN, &f) == SPS_PARSE_OK);
  assert(f.profile_idc == 77 && f.constraint_flags == 0x40 && f.level_idc == 32);
  assert(f.poc_type == 2 && f.frame_mbs_only == 1 && f.max_num_ref_frames == 9);
  assert(f.vui && f.bitstream_restriction);
  assert(f.num_reorder_frames == 0 && f.max_dec_frame_buffering == 9);
}

static void test_oversized_poc_cycle_is_refused_not_read(void) {
  uint8_t nal[SPS_PROBE_CAP];
  SpsFields f;
  Spec spec = {66, 1, 255, false, -1, 0};
  uint32_t n = build_sps(&spec, nal);
  assert(sps_probe_parse(nal, n, n, &f) == SPS_PARSE_OK); /* the largest legal cycle */
  assert(f.poc_type == 1 && f.max_num_ref_frames == 4);

  spec.poc_cycle = 256; /* data is present behind the count, so only the limit stops the loop */
  n = build_sps(&spec, nal);
  assert(sps_probe_parse(nal, n, n, &f) == SPS_PARSE_POC_CYCLE_LARGE);
}

static void test_oversized_hrd_cpb_count_is_refused_not_read(void) {
  uint8_t nal[SPS_PROBE_CAP];
  SpsFields f;
  Spec spec = {66, 2, 0, false, 31, 0};
  uint32_t n = build_sps(&spec, nal);
  assert(sps_probe_parse(nal, n, n, &f) == SPS_PARSE_OK); /* the largest legal count */
  assert(f.bitstream_restriction && f.num_reorder_frames == 2 && f.max_dec_frame_buffering == 5);

  spec.hrd_cpb_cnt_minus1 = 32;
  n = build_sps(&spec, nal);
  assert(sps_probe_parse(nal, n, n, &f) == SPS_PARSE_CPB_CNT_LARGE);
}

static void test_truncated_sps_is_refused_not_printed_with_made_up_values(void) {
  SpsFields f;
  /* Every cut from nothing up to one byte short of the last field, including cuts inside the VUI
   * (bytes 14 on) and inside the final bitstream-restriction fields. */
  for (uint32_t cut = 0; cut < HEADER_SPS_LEN; cut++) {
    SpsParseStatus s = sps_probe_parse(HEADER + 4, cut, cut, &f);
    assert(s == SPS_PARSE_TRUNCATED);
  }
}

static void test_scaling_lists_and_escaped_bytes_do_not_shift_later_fields(void) {
  uint8_t nal[SPS_PROBE_CAP];
  SpsFields f;
  /* Profile 100 with four scaling lists, two of which end early; a width of 2^28 puts a run of
   * zero bytes in the SPS, so the builder inserts 00 00 03 escapes. */
  Spec spec = {100, 0, 0, true, -1, 1u << 28};
  uint32_t n = build_sps(&spec, nal);
  bool escaped = false;
  for (uint32_t i = 2; i < n; i++)
    escaped = escaped || (nal[i - 2] == 0 && nal[i - 1] == 0 && nal[i] == 3);
  assert(escaped);
  assert(sps_probe_parse(nal, n, n, &f) == SPS_PARSE_OK);
  assert(f.profile_idc == 100 && f.poc_type == 0 && f.max_num_ref_frames == 4);
  assert(f.frame_mbs_only == 1 && !f.vui);
}

int main(void) {
  test_locates_the_sps_in_the_real_header();
  test_three_byte_start_code_and_unterminated_sps();
  test_other_buffers_are_not_an_sps();
  test_first_sps_is_kept_and_repeats_are_counted();
  test_a_new_session_starts_over();
  test_hardware_sps_gives_the_values_worked_out_by_hand();
  test_oversized_poc_cycle_is_refused_not_read();
  test_oversized_hrd_cpb_count_is_refused_not_read();
  test_truncated_sps_is_refused_not_printed_with_made_up_values();
  test_scaling_lists_and_escaped_bytes_do_not_shift_later_fields();
  printf("sps_probe_tests: all passed\n");
  return 0;
}
