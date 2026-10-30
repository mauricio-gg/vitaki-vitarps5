// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native test for the SPS rewrite in vita/src/sps_rewrite.c (ticket #473). A wrong bit in
// the SPS means the decoder gets no video at all, so the rewritten bytes are read back with the
// project's own SPS reader.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "sps_rewrite.h"

/* The console's SPS: 720p (#456 round 2 hardware log), 540p (round 1) and 1080p at 30 fps (round 4
 * log, PIPE/SPS line of the first 1080p session; level 5.0, 29 bytes). */
static const uint8_t SPS_720[] = {0x67, 0x4d, 0x40, 0x28, 0x91, 0x8a, 0x01, 0x40, 0x16, 0xec,
                                  0x05, 0xa8, 0x08, 0x08, 0x0a, 0x00, 0x00, 0x0f, 0xa4, 0x00,
                                  0x07, 0x53, 0x01, 0x13, 0x43, 0xc7, 0xc5, 0x40};
static const uint8_t SPS_540[] = {0x67, 0x4d, 0x40, 0x20, 0x91, 0x8a, 0x03, 0xc0, 0x45, 0xfb,
                                  0xc0, 0x5a, 0x80, 0x80, 0x80, 0xa0, 0x00, 0x00, 0xfa, 0x40,
                                  0x00, 0x75, 0x30, 0x11, 0x34, 0x38, 0x74, 0x54};

static const uint8_t SPS_1080[] = {0x67, 0x4d, 0x40, 0x32, 0x91, 0x8a, 0x01, 0xe0, 0x08, 0x9f,
                                   0x97, 0x01, 0x6a, 0x02, 0x02, 0x02, 0x80, 0x00, 0x07, 0xd2,
                                   0x00, 0x01, 0xd4, 0xc0, 0x44, 0xd0, 0xf1, 0xf1, 0x50};

static unsigned bit(const uint8_t *d, uint32_t p) {
  return (d[p >> 3] >> (7 - (p & 7))) & 1u;
}

static void parse(const uint8_t *nal, uint32_t len, SpsFields *f) {
  assert(sps_probe_parse(nal, len, len, f) == SPS_PARSE_OK);
}

/* Rewrites sps to `target` (a count whose Exp-Golomb code is 5 bits, 3 to 6; the console's 9 is
 * 7 bits) and checks, by reading the result back with the project's SPS reader, that the two counts
 * are `target` and everything else is unchanged. The SPS must hold no emulation-prevention byte,
 * so bit offsets in the NAL are the offsets the reader reported. Returns the new length. */
static uint32_t rewrite_and_check(const uint8_t *sps, uint32_t len, uint32_t target) {
  uint8_t out[SPS_REWRITE_MAX_OUT];
  uint32_t out_len = 0;
  SpsRewriteInfo info;
  assert(sps_rewrite_max_refs(sps, len, target, out, sizeof(out), &out_len, &info) ==
         SPS_REWRITE_DONE);
  assert(info.refs_old == 9 && info.refs_new == target && info.dfb_old == 9 &&
         info.dfb_new == target);
  assert(info.len_old == len && info.len_new == out_len);

  SpsFields a, b;
  parse(sps, len, &a);
  parse(out, out_len, &b);
  assert(b.max_num_ref_frames == target && b.max_dec_frame_buffering == target);
  assert(a.profile_idc == b.profile_idc && a.constraint_flags == b.constraint_flags);
  assert(a.level_idc == b.level_idc && a.poc_type == b.poc_type);
  assert(a.frame_mbs_only == b.frame_mbs_only && a.vui == b.vui);
  assert(a.bitstream_restriction == b.bitstream_restriction);
  assert(a.num_reorder_frames == b.num_reorder_frames);
  /* Everything outside the two codes is bit-exact. 9 -> target turns two 7-bit codes into two
   * 5-bit codes, so the tail moves 2 bits after the first and 4 after the second. */
  uint32_t a_end = a.max_num_ref_frames_bit + 7, b_end = a.max_dec_frame_buffering_bit + 7;
  assert(b.max_num_ref_frames_bit == a.max_num_ref_frames_bit);
  assert(b.max_dec_frame_buffering_bit == a.max_dec_frame_buffering_bit - 2);
  for (uint32_t p = 0; p < a.max_num_ref_frames_bit; p++)
    assert(bit(sps, p) == bit(out, p));
  for (uint32_t p = a_end; p < a.max_dec_frame_buffering_bit; p++)
    assert(bit(sps, p) == bit(out, p - 2));
  uint32_t stop = len * 8u; /* up to and including the rbsp stop bit */
  while (bit(sps, stop - 1) == 0)
    stop--;
  for (uint32_t p = b_end; p < stop; p++)
    assert(bit(sps, p) == bit(out, p - 4));
  assert(bit(out, stop - 4 - 1) == 1 && out_len <= len);
  for (uint32_t p = stop - 4; p < out_len * 8u; p++) /* alignment after the stop bit is zero */
    assert(bit(out, p) == 0);
  return out_len;
}

/* catches: a wrongly placed or wrongly sized bit edit, which leaves a stream the decoder refuses
 * outright (no video), or one that silently changes another field (size, VUI timing, level). */
static void test_720p_sps_rewritten_to_5_changes_only_the_two_counts(void) {
  rewrite_and_check(SPS_720, sizeof(SPS_720), 5);
}

/* catches: the same, on the exact bytes the PS5 sends for 1080p (level 5.0): the rewrite to 4 is
 * what lets the Internal decoder, created with 4 references, output pictures at all. The
 * hardware log of the same rewrite reports 29 -> 28 bytes. */
static void test_1080p_sps_rewritten_to_4_changes_only_the_two_counts(void) {
  assert(rewrite_and_check(SPS_1080, sizeof(SPS_1080), 4) == 28);
}

/* catches: a rewrite to a count with a shorter or longer code (1 is 1 bit, 9 is 7 bits) shifting
 * the rest of the SPS: the trailing bits and the emulation-prevention bytes must stay right, or
 * the NAL ends mid-field or contains a forbidden 00 00 0x pattern. */
static void test_shorter_code_keeps_rest_of_sps_and_no_forbidden_pattern(void) {
  uint8_t out[SPS_REWRITE_MAX_OUT];
  uint32_t out_len = 0;
  assert(sps_rewrite_max_refs(SPS_720, sizeof(SPS_720), 1, out, sizeof(out), &out_len, NULL) ==
         SPS_REWRITE_DONE);
  SpsFields b;
  parse(out, out_len, &b);
  assert(b.max_num_ref_frames == 1 && b.max_dec_frame_buffering == 1);
  assert(b.num_reorder_frames == 0 && b.bitstream_restriction);
  for (uint32_t i = 2; i < out_len; i++)
    assert(!(out[i - 2] == 0 && out[i - 1] == 0 && out[i] <= 2));
  assert(out[out_len - 1] != 0);
}

/* catches: touching a stream that already fits (540p plays today with 8 against 9 declared, and
 * a target at or above the declared count): it must be reported as not needed, bytes untouched. */
static void test_not_needed_leaves_everything_alone(void) {
  uint8_t out[SPS_REWRITE_MAX_OUT];
  memset(out, 0xAA, sizeof(out));
  uint32_t out_len = 77;
  assert(sps_rewrite_max_refs(SPS_540, sizeof(SPS_540), 9, out, sizeof(out), &out_len, NULL) ==
         SPS_REWRITE_NOT_NEEDED);
  assert(sps_rewrite_max_refs(SPS_540, sizeof(SPS_540), 12, out, sizeof(out), &out_len, NULL) ==
         SPS_REWRITE_NOT_NEEDED);
  assert(out_len == 77 && out[0] == 0xAA && out[SPS_REWRITE_MAX_OUT - 1] == 0xAA);
}

/* catches: a truncated or damaged SPS being "rewritten" from guessed bits (garbage fed to the
 * decoder), or a too-small output buffer being overrun. */
static void test_truncated_refused_and_small_buffer_refused(void) {
  uint8_t out[SPS_REWRITE_MAX_OUT];
  uint32_t out_len = 0;
  assert(sps_rewrite_max_refs(SPS_720, 12, 5, out, sizeof(out), &out_len, NULL) ==
         SPS_REWRITE_UNPARSEABLE);
  assert(sps_rewrite_max_refs(SPS_720, 3, 5, out, sizeof(out), &out_len, NULL) ==
         SPS_REWRITE_UNPARSEABLE);
  assert(sps_rewrite_max_refs(SPS_720, sizeof(SPS_720), 5, out, 10, &out_len, NULL) ==
         SPS_REWRITE_BUFFER_TOO_SMALL);
}

int main(void) {
  test_720p_sps_rewritten_to_5_changes_only_the_two_counts();
  test_1080p_sps_rewritten_to_4_changes_only_the_two_counts();
  test_shorter_code_keeps_rest_of_sps_and_no_forbidden_pattern();
  test_not_needed_leaves_everything_alone();
  test_truncated_refused_and_small_buffer_refused();
  printf("sps_rewrite_tests: all passed\n");
  return 0;
}
