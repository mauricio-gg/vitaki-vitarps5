// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) unit test for the H.264 SPS ref-count / frame_num-gap
// parsing added to lib/src/bitstream.c for GH #251. This runs on the host
// via `cc`, not through the Vita cross-compiler: `./tools/build.sh test`
// only compiles for arm-vita-eabi and never executes the resulting ELF, so
// this is the only place these assertions are actually exercised.
//
// Build & run:
//   cc -std=c99 -I lib/include -I lib/src -I test/stubs \
//      test/bitstream_sps_tests.c -o /tmp/bitstream_sps_tests && \
//      /tmp/bitstream_sps_tests

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <chiaki/log.h>

// Minimal chiaki_log stub. bitstream.c's CHIAKI_LOGW/CHIAKI_LOGI macros
// resolve to this symbol. config_vita_tests.c defines an equivalent stub
// for the same reason, but this test links standalone (see build command
// above) rather than joining that translation unit, so it needs its own.
void chiaki_log(ChiakiLog *log, ChiakiLogLevel level, const char *fmt, ...)
{
	(void)log;
	(void)level;
	(void)fmt;
}

#include "bitstream.c"

// Minimal MSB-first bit writer for crafting SPS test vectors.
typedef struct { uint8_t buf[64]; unsigned bitpos; } BitWriter;
static void bw_u(BitWriter *bw, uint32_t val, unsigned bits)
{
	for(int i = (int)bits - 1; i >= 0; i--)
	{
		if((val >> i) & 1)
			bw->buf[bw->bitpos / 8] |= (uint8_t)(0x80 >> (bw->bitpos % 8));
		bw->bitpos++;
	}
}
static void bw_ue(BitWriter *bw, uint32_t val)
{
	unsigned len = 0;
	uint32_t v = val + 1;
	while((v >> len) > 1) len++;
	bw_u(bw, 0, len);       // leading zeros
	bw_u(bw, v, len + 1);   // codeword
}

// Signed exp-golomb writer, the inverse of vl_rbsp_se(): codeNum = 2*val-1
// for val > 0, -2*val for val <= 0.
static void bw_se(BitWriter *bw, int32_t val)
{
	uint32_t code_num = val > 0 ? (uint32_t)(2 * val - 1) : (uint32_t)(-2 * val);
	bw_ue(bw, code_num);
}

// Emits: startcode + NAL header (type 7) + SPS through the gaps flag.
static unsigned make_sps(uint8_t *out, unsigned max_num_ref_frames, unsigned gaps_flag)
{
	BitWriter bw; memset(&bw, 0, sizeof(bw));
	bw_u(&bw, 0x67, 8);  // forbidden_zero(0) + nal_ref_idc(3) + nal_unit_type(7)
	bw_u(&bw, 66, 8);    // profile_idc = baseline (skips high-profile block)
	bw_u(&bw, 0, 8);     // constraint flags + reserved
	bw_u(&bw, 31, 8);    // level_idc
	bw_ue(&bw, 0);       // seq_parameter_set_id
	bw_ue(&bw, 4);       // log2_max_frame_num_minus4
	bw_ue(&bw, 0);       // pic_order_cnt_type = 0
	bw_ue(&bw, 4);       //   log2_max_pic_order_cnt_lsb_minus4
	bw_ue(&bw, max_num_ref_frames);
	bw_u(&bw, gaps_flag, 1);
	bw_u(&bw, 1, 1);     // rbsp_stop_one_bit (padding)
	while(bw.bitpos % 8) bw_u(&bw, 0, 1);
	out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 1;
	memcpy(out + 4, bw.buf, bw.bitpos / 8);
	return 4 + bw.bitpos / 8;
}

// Same as make_sps(), but with pic_order_cnt_type == 1: exercises the
// delta_pic_order_always_zero_flag / offset_for_non_ref_pic /
// offset_for_top_to_bottom_field / offset_for_ref_frame[] conditional-field
// skip path in header_h264().
static unsigned make_sps_poc_type1(uint8_t *out, unsigned num_ref_frames_in_cycle,
	unsigned max_num_ref_frames, unsigned gaps_flag)
{
	BitWriter bw; memset(&bw, 0, sizeof(bw));
	bw_u(&bw, 0x67, 8);
	bw_u(&bw, 66, 8);
	bw_u(&bw, 0, 8);
	bw_u(&bw, 31, 8);
	bw_ue(&bw, 0);       // seq_parameter_set_id
	bw_ue(&bw, 4);       // log2_max_frame_num_minus4
	bw_ue(&bw, 1);       // pic_order_cnt_type = 1
	bw_u(&bw, 0, 1);     //   delta_pic_order_always_zero_flag
	bw_se(&bw, 0);       //   offset_for_non_ref_pic
	bw_se(&bw, 0);       //   offset_for_top_to_bottom_field
	bw_ue(&bw, num_ref_frames_in_cycle); //   num_ref_frames_in_pic_order_cnt_cycle
	for(unsigned i = 0; i < num_ref_frames_in_cycle; i++)
		bw_se(&bw, 0);   //   offset_for_ref_frame[i]
	bw_ue(&bw, max_num_ref_frames);
	bw_u(&bw, gaps_flag, 1);
	bw_u(&bw, 1, 1);     // rbsp_stop_one_bit (padding)
	while(bw.bitpos % 8) bw_u(&bw, 0, 1);
	out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 1;
	memcpy(out + 4, bw.buf, bw.bitpos / 8);
	return 4 + bw.bitpos / 8;
}

// pic_order_cnt_type == 1 with an implausible cycle count (> 255): the
// parser must bail out of the extended fields (valid_ext stays false)
// WITHOUT failing the base parse -- chiaki_bitstream_header() must still
// return true, since the slice parser only depends on the base field
// (log2_max_frame_num_minus4) that already parsed successfully. This is
// an intentional asymmetry: never retroactively fail a base parse that
// already succeeded.
static unsigned make_sps_poc_type1_implausible_cycle(uint8_t *out)
{
	BitWriter bw; memset(&bw, 0, sizeof(bw));
	bw_u(&bw, 0x67, 8);
	bw_u(&bw, 66, 8);
	bw_u(&bw, 0, 8);
	bw_u(&bw, 31, 8);
	bw_ue(&bw, 0);       // seq_parameter_set_id
	bw_ue(&bw, 4);       // log2_max_frame_num_minus4
	bw_ue(&bw, 1);       // pic_order_cnt_type = 1
	bw_u(&bw, 0, 1);     //   delta_pic_order_always_zero_flag
	bw_se(&bw, 0);       //   offset_for_non_ref_pic
	bw_se(&bw, 0);       //   offset_for_top_to_bottom_field
	bw_ue(&bw, 256);     //   num_ref_frames_in_pic_order_cnt_cycle (> 255)
	bw_u(&bw, 1, 1);     // rbsp_stop_one_bit (padding)
	while(bw.bitpos % 8) bw_u(&bw, 0, 1);
	out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 1;
	memcpy(out + 4, bw.buf, bw.bitpos / 8);
	return 4 + bw.bitpos / 8;
}

// pic_order_cnt_type == 5: spec-impossible (only 0-2 are defined). The parser
// must bail out of the extended fields (M3, lib/src/bitstream.c) WITHOUT
// failing the base parse, same asymmetry as the implausible-cycle vector
// above.
static unsigned make_sps_poc_type_invalid(uint8_t *out)
{
	BitWriter bw; memset(&bw, 0, sizeof(bw));
	bw_u(&bw, 0x67, 8);
	bw_u(&bw, 66, 8);
	bw_u(&bw, 0, 8);
	bw_u(&bw, 31, 8);
	bw_ue(&bw, 0);       // seq_parameter_set_id
	bw_ue(&bw, 4);       // log2_max_frame_num_minus4
	bw_ue(&bw, 5);       // pic_order_cnt_type = 5 (invalid; only 0-2 defined)
	bw_u(&bw, 1, 1);     // rbsp_stop_one_bit (padding)
	while(bw.bitpos % 8) bw_u(&bw, 0, 1);
	out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 1;
	memcpy(out + 4, bw.buf, bw.bitpos / 8);
	return 4 + bw.bitpos / 8;
}

// max_num_ref_frames == 20: exceeds the spec's absolute bound of 16 (M3,
// lib/src/bitstream.c). Same never-retroactively-fail asymmetry: base parse
// must still succeed, but valid_ext must stay false so the drift-safe gate
// cannot go green on a mis-parsed field.
static unsigned make_sps_ref_frames_invalid(uint8_t *out)
{
	return make_sps(out, 20, 0);
}

int main(void)
{
	uint8_t sps[80];
	ChiakiBitstream bs;
	memset(&bs, 0, sizeof(bs));
	bs.codec = CHIAKI_CODEC_H264;

	unsigned n = make_sps(sps, 8, 0);
	assert(chiaki_bitstream_header(&bs, sps, n));
	assert(bs.h264.sps.valid_ext);
	assert(bs.h264.sps.max_num_ref_frames == 8);
	assert(bs.h264.sps.gaps_in_frame_num_value_allowed_flag == 0);
	assert(chiaki_bitstream_h264_drift_safe(&bs));

	n = make_sps(sps, 4, 1);
	assert(chiaki_bitstream_header(&bs, sps, n));
	assert(bs.h264.sps.valid_ext);
	assert(bs.h264.sps.gaps_in_frame_num_value_allowed_flag == 1);
	assert(!chiaki_bitstream_h264_drift_safe(&bs));

	// pic_order_cnt_type == 1 exercises the conditional-field skip path.
	n = make_sps_poc_type1(sps, 2, 6, 0);
	assert(chiaki_bitstream_header(&bs, sps, n));
	assert(bs.h264.sps.valid_ext);
	assert(bs.h264.sps.max_num_ref_frames == 6);
	assert(bs.h264.sps.gaps_in_frame_num_value_allowed_flag == 0);
	assert(chiaki_bitstream_h264_drift_safe(&bs));

	// num_ref_frames_in_pic_order_cnt_cycle > 255 is implausible: the base
	// parse (already succeeded) must not be retroactively failed, but the
	// extended fields must be marked unparsed.
	n = make_sps_poc_type1_implausible_cycle(sps);
	assert(chiaki_bitstream_header(&bs, sps, n));
	assert(!bs.h264.sps.valid_ext);
	assert(!chiaki_bitstream_h264_drift_safe(&bs));

	// pic_order_cnt_type == 5 is spec-impossible (only 0-2 are defined). Same
	// never-retroactively-fail asymmetry as above (M3 fix).
	n = make_sps_poc_type_invalid(sps);
	assert(chiaki_bitstream_header(&bs, sps, n));
	assert(!bs.h264.sps.valid_ext);
	assert(!chiaki_bitstream_h264_drift_safe(&bs));

	// max_num_ref_frames == 20 exceeds the spec's absolute bound of 16. Same
	// never-retroactively-fail asymmetry as above (M3 fix).
	n = make_sps_ref_frames_invalid(sps);
	assert(chiaki_bitstream_header(&bs, sps, n));
	assert(!bs.h264.sps.valid_ext);
	assert(!chiaki_bitstream_h264_drift_safe(&bs));

	memset(&bs, 0, sizeof(bs));
	bs.codec = CHIAKI_CODEC_H264;
	assert(!chiaki_bitstream_h264_drift_safe(&bs)); // unparsed -> unsafe

	printf("bitstream_sps_tests: all assertions passed\n");
	return 0;
}
