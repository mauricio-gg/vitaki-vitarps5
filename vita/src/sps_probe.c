// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "sps_probe.h"

#include <string.h>

#define NAL_TYPE_MASK 0x1F
#define NAL_FORBIDDEN_ZERO_BIT 0x80
#define NAL_TYPE_SPS 7

/* Written only by the receive thread (and by sps_probe_reset() while it is idle). probe_lock is
 * odd while a write is in progress. */
static SpsProbeData probe;
static volatile uint32_t probe_lock;

bool sps_probe_locate(const uint8_t *buf, size_t size, size_t *nal_off, size_t *nal_len) {
  /* Cheapest rejections first: this runs for every video buffer. */
  if (buf == NULL || size < 3 + SPS_PROBE_MIN_NAL_LEN || buf[0] != 0 || buf[1] != 0)
    return false;
  size_t off;
  if (buf[2] == 1)
    off = 3;
  else if (buf[2] == 0 && buf[3] == 1)
    off = 4;
  else
    return false;
  if ((buf[off] & NAL_FORBIDDEN_ZERO_BIT) || (buf[off] & NAL_TYPE_MASK) != NAL_TYPE_SPS)
    return false;

  size_t limit = off + SPS_PROBE_SCAN_MAX < size ? off + SPS_PROBE_SCAN_MAX : size;
  size_t end = limit;
  for (size_t p = off + 1; p + 2 < limit; p++) {
    /* 00 00 00 or 00 00 01 cannot occur inside a NAL unit (emulation prevention), so it is the
     * start of the next NAL (or trailing zero bytes). */
    if (buf[p] == 0 && buf[p + 1] == 0 && buf[p + 2] <= 1) {
      end = p;
      break;
    }
  }
  if (end - off < SPS_PROBE_MIN_NAL_LEN)
    return false;
  *nal_off = off;
  *nal_len = end - off;
  return true;
}

void sps_probe_reset(void) {
  probe_lock = 0;
  memset(&probe, 0, sizeof(probe));
}

/* Bracket every change of `probe` made while frames are flowing, so a reader never copies a
 * half-written update. */
static inline void probe_write_begin(void) {
  probe_lock++;
  __sync_synchronize();
}

static inline void probe_write_end(void) {
  __sync_synchronize();
  probe_lock++;
}

void sps_probe_observe(const uint8_t *buf, size_t size) {
  size_t off;
  size_t len;
  if (!sps_probe_locate(buf, size, &off, &len))
    return;
  const uint8_t *nal = buf + off;
  uint32_t stored = len < SPS_PROBE_CAP ? (uint32_t)len : SPS_PROBE_CAP;

  if (probe.first_len == 0) {
    probe_write_begin();
    memcpy(probe.first, nal, stored);
    probe.first_len = (uint32_t)len;
    probe_write_end();
    return;
  }
  bool same = len == probe.first_len && memcmp(nal, probe.first, stored) == 0;
  probe_write_begin();
  probe.seen_again++;
  if (!same)
    probe.differed++;
  probe_write_end();
}

uint32_t sps_probe_version(void) {
  return probe_lock;
}

bool sps_probe_snapshot(SpsProbeData *out, int attempts) {
  for (int attempt = 0; attempt < attempts; attempt++) {
    uint32_t before = probe_lock;
    if (before & 1)
      continue;
    __sync_synchronize();
    *out = probe;
    __sync_synchronize();
    if (probe_lock == before)
      return true;
  }
  return false;
}

/* ---- Bounded SPS reader (PIPE/SPS) ------------------------------------------------------- */

#define SPS_NAL_HEADER_LEN 1
#define SPS_MAX_UE_LEADING_ZEROS 31 /* a code with 32 or more would not fit the 32-bit result */
#define SPS_MAX_CHROMA_FORMAT_IDC 3
#define SPS_CHROMA_FORMAT_444 3
#define SPS_MAX_POC_TYPE 2
#define SPS_SCALING_LISTS_NON_444 8
#define SPS_SCALING_LISTS_444 12
#define SPS_SCALING_LIST_4X4_COUNT 6
#define SPS_SCALING_LIST_4X4_SIZE 16
#define SPS_SCALING_LIST_8X8_SIZE 64
#define SPS_ASPECT_RATIO_EXTENDED 255
#define SPS_SAR_FIELD_BITS 16
#define SPS_VUI_TIMING_BITS 32
#define SPS_VUI_VIDEO_FORMAT_BITS 3
#define SPS_VUI_COLOUR_DESC_BITS 24
#define SPS_HRD_SCALE_BITS 4
#define SPS_HRD_DELAY_LENGTHS_BITS 20 /* four 5-bit fields */
#define SPS_BYTE_BITS 8
#define SPS_MAX_BITS_AT_ONCE 32

/* Bit reader over a fixed buffer. `err` is sticky: once a read fails every later read returns 0
 * and the first error is kept, so a caller needs to look at it only where a value decides a loop
 * or a branch and once at the end. A value read after a failure is never reported as real. */
typedef struct {
  const uint8_t *data;
  uint32_t nbits;
  uint32_t pos;
  SpsParseStatus err;
} SpsBits;

static void bits_fail(SpsBits *b, SpsParseStatus why) {
  if (b->err == SPS_PARSE_OK)
    b->err = why;
}

/* Reads n bits (0..32), most significant first. */
static uint32_t bits_u(SpsBits *b, unsigned n) {
  if (b->err != SPS_PARSE_OK)
    return 0;
  if (n > SPS_MAX_BITS_AT_ONCE || n > b->nbits - b->pos) {
    bits_fail(b, SPS_PARSE_TRUNCATED);
    return 0;
  }
  uint32_t v = 0;
  for (unsigned i = 0; i < n; i++, b->pos++)
    v = (v << 1) | ((b->data[b->pos >> 3] >> (7 - (b->pos & 7))) & 1u);
  return v;
}

/* Reads an unsigned Exp-Golomb code. */
static uint32_t bits_ue(SpsBits *b) {
  unsigned zeros = 0;
  while (b->err == SPS_PARSE_OK && bits_u(b, 1) == 0) {
    if (++zeros > SPS_MAX_UE_LEADING_ZEROS) {
      bits_fail(b, SPS_PARSE_UE_TOO_LONG);
      return 0;
    }
  }
  if (b->err != SPS_PARSE_OK)
    return 0;
  return ((uint32_t)1 << zeros) - 1 + bits_u(b, zeros);
}

/* Reads and discards a signed Exp-Golomb code (the value is not needed). */
static void bits_skip_se(SpsBits *b) {
  (void)bits_ue(b);
}

/* Walks one scaling list of `size` entries; only the bit count matters, but the end-of-list rule
 * (nextScale reaching 0) decides how many deltas are present. */
static void skip_scaling_list(SpsBits *b, unsigned size) {
  int64_t last = 8;
  int64_t next = 8;
  for (unsigned j = 0; j < size && next != 0 && b->err == SPS_PARSE_OK; j++) {
    uint32_t k = bits_ue(b);
    int64_t delta = (k & 1u) ? (int64_t)((k >> 1) + 1) : -(int64_t)(k >> 1);
    next = (((last + delta) % 256) + 256) % 256;
    last = next == 0 ? last : next;
  }
}

static bool is_high_profile(uint32_t profile_idc) {
  switch (profile_idc) {
    case 100:
    case 110:
    case 122:
    case 244:
    case 44:
    case 83:
    case 86:
    case 118:
    case 128:
    case 138:
    case 139:
    case 134:
    case 135:
      return true;
    default:
      return false;
  }
}

/* Copies the NAL into out (room for `n` bytes) without the emulation-prevention byte of every
 * 00 00 03. Returns the number of bytes written, never more than n. */
static uint32_t remove_emulation_prevention(const uint8_t *in, uint32_t n, uint8_t *out) {
  uint32_t o = 0;
  unsigned zeros = 0;
  for (uint32_t i = 0; i < n; i++) {
    if (zeros >= 2 && in[i] == 0x03) {
      zeros = 0;
      continue;
    }
    out[o++] = in[i];
    zeros = in[i] == 0 ? zeros + 1 : 0;
  }
  return o;
}

/* HRD parameters: only the layout matters. cpb_cnt_minus1 sizes a loop, so it is checked first. */
static void skip_hrd(SpsBits *b) {
  uint32_t cpb_cnt_minus1 = bits_ue(b);
  if (cpb_cnt_minus1 > SPS_MAX_CPB_CNT_MINUS1) {
    bits_fail(b, SPS_PARSE_CPB_CNT_LARGE);
    return;
  }
  (void)bits_u(b, 2 * SPS_HRD_SCALE_BITS);
  for (uint32_t i = 0; i <= cpb_cnt_minus1; i++) {
    (void)bits_ue(b);
    (void)bits_ue(b);
    (void)bits_u(b, 1);
  }
  (void)bits_u(b, SPS_HRD_DELAY_LENGTHS_BITS);
}

/* VUI parameters up to and including the bitstream restriction. */
static void read_vui(SpsBits *b, SpsFields *f) {
  if (bits_u(b, 1)) { /* aspect_ratio_info_present_flag */
    if (bits_u(b, SPS_BYTE_BITS) == SPS_ASPECT_RATIO_EXTENDED)
      (void)bits_u(b, 2 * SPS_SAR_FIELD_BITS);
  }
  if (bits_u(b, 1)) /* overscan_info_present_flag */
    (void)bits_u(b, 1);
  if (bits_u(b, 1)) { /* video_signal_type_present_flag */
    (void)bits_u(b, SPS_VUI_VIDEO_FORMAT_BITS + 1);
    if (bits_u(b, 1)) /* colour_description_present_flag */
      (void)bits_u(b, SPS_VUI_COLOUR_DESC_BITS);
  }
  if (bits_u(b, 1)) { /* chroma_loc_info_present_flag */
    (void)bits_ue(b);
    (void)bits_ue(b);
  }
  if (bits_u(b, 1)) { /* timing_info_present_flag */
    (void)bits_u(b, SPS_VUI_TIMING_BITS);
    (void)bits_u(b, SPS_VUI_TIMING_BITS);
    (void)bits_u(b, 1);
  }
  uint32_t nal_hrd = bits_u(b, 1);
  if (nal_hrd)
    skip_hrd(b);
  uint32_t vcl_hrd = bits_u(b, 1);
  if (vcl_hrd)
    skip_hrd(b);
  if (nal_hrd || vcl_hrd)
    (void)bits_u(b, 1); /* low_delay_hrd_flag */
  (void)bits_u(b, 1);   /* pic_struct_present_flag */
  if (bits_u(b, 1)) {   /* bitstream_restriction_flag */
    (void)bits_u(b, 1);
    for (int i = 0; i < 4; i++) /* max_bytes_per_pic_denom .. log2_max_mv_length_vertical */
      (void)bits_ue(b);
    f->num_reorder_frames = bits_ue(b);
    f->max_dec_frame_buffering_bit = b->pos;
    f->max_dec_frame_buffering = bits_ue(b);
    f->bitstream_restriction = true;
  }
}

SpsParseStatus sps_probe_parse(const uint8_t *nal, uint32_t stored, uint32_t true_len,
                               SpsFields *out) {
  if (nal == NULL || out == NULL)
    return SPS_PARSE_TRUNCATED;
  if (stored > SPS_PROBE_CAP)
    stored = SPS_PROBE_CAP; /* never read or copy more than the buffer a caller can hold */
  if (true_len > stored)
    return SPS_PARSE_LONGER_THAN_CAP;
  if (stored < SPS_NAL_HEADER_LEN)
    return SPS_PARSE_TRUNCATED;
  if ((nal[0] & NAL_FORBIDDEN_ZERO_BIT) || (nal[0] & NAL_TYPE_MASK) != NAL_TYPE_SPS)
    return SPS_PARSE_NOT_AN_SPS;

  uint8_t rbsp[SPS_PROBE_CAP];
  uint32_t rbsp_len = remove_emulation_prevention(nal, stored, rbsp);
  SpsBits b = {rbsp, rbsp_len * 8u, SPS_NAL_HEADER_LEN * 8u, SPS_PARSE_OK};
  SpsFields f;
  memset(&f, 0, sizeof(f));

  f.profile_idc = (uint8_t)bits_u(&b, SPS_BYTE_BITS);
  f.constraint_flags = (uint8_t)bits_u(&b, SPS_BYTE_BITS);
  f.level_idc = (uint8_t)bits_u(&b, SPS_BYTE_BITS);
  if (bits_ue(&b) > SPS_MAX_ID)
    bits_fail(&b, SPS_PARSE_SPS_ID_RANGE);

  if (is_high_profile(f.profile_idc)) {
    uint32_t chroma_format_idc = bits_ue(&b);
    if (chroma_format_idc > SPS_MAX_CHROMA_FORMAT_IDC)
      bits_fail(&b, SPS_PARSE_CHROMA_RANGE);
    if (chroma_format_idc == SPS_CHROMA_FORMAT_444)
      (void)bits_u(&b, 1); /* separate_colour_plane_flag */
    (void)bits_ue(&b);     /* bit_depth_luma_minus8 */
    (void)bits_ue(&b);     /* bit_depth_chroma_minus8 */
    (void)bits_u(&b, 1);   /* qpprime_y_zero_transform_bypass_flag */
    if (bits_u(&b, 1)) {   /* seq_scaling_matrix_present_flag */
      unsigned lists = chroma_format_idc == SPS_CHROMA_FORMAT_444 ? SPS_SCALING_LISTS_444
                                                                  : SPS_SCALING_LISTS_NON_444;
      for (unsigned i = 0; i < lists && b.err == SPS_PARSE_OK; i++) {
        if (bits_u(&b, 1))
          skip_scaling_list(&b, i < SPS_SCALING_LIST_4X4_COUNT ? SPS_SCALING_LIST_4X4_SIZE
                                                               : SPS_SCALING_LIST_8X8_SIZE);
      }
    }
  }

  (void)bits_ue(&b); /* log2_max_frame_num_minus4 */
  f.poc_type = bits_ue(&b);
  if (f.poc_type > SPS_MAX_POC_TYPE) {
    bits_fail(&b, SPS_PARSE_POC_TYPE_RANGE);
  } else if (f.poc_type == 0) {
    (void)bits_ue(&b); /* log2_max_pic_order_cnt_lsb_minus4 */
  } else if (f.poc_type == 1) {
    (void)bits_u(&b, 1); /* delta_pic_order_always_zero_flag */
    bits_skip_se(&b);    /* offset_for_non_ref_pic */
    bits_skip_se(&b);    /* offset_for_top_to_bottom_field */
    uint32_t cycle = bits_ue(&b);
    if (cycle > SPS_MAX_POC_CYCLE) {
      bits_fail(&b, SPS_PARSE_POC_CYCLE_LARGE);
    } else {
      for (uint32_t i = 0; i < cycle && b.err == SPS_PARSE_OK; i++)
        bits_skip_se(&b); /* offset_for_ref_frame[i] */
    }
  }

  f.max_num_ref_frames_bit = b.pos;
  f.max_num_ref_frames = bits_ue(&b);
  (void)bits_u(&b, 1); /* gaps_in_frame_num_value_allowed_flag */
  (void)bits_ue(&b);   /* pic_width_in_mbs_minus1 */
  (void)bits_ue(&b);   /* pic_height_in_map_units_minus1 */
  f.frame_mbs_only = bits_u(&b, 1);
  if (!f.frame_mbs_only)
    (void)bits_u(&b, 1); /* mb_adaptive_frame_field_flag */
  (void)bits_u(&b, 1);   /* direct_8x8_inference_flag */
  if (bits_u(&b, 1)) {   /* frame_cropping_flag */
    for (int i = 0; i < 4; i++)
      (void)bits_ue(&b);
  }
  f.vui = bits_u(&b, 1) != 0;
  if (f.vui)
    read_vui(&b, &f);

  if (b.err != SPS_PARSE_OK)
    return b.err;
  *out = f;
  return SPS_PARSE_OK;
}

const char *sps_probe_status_name(SpsParseStatus status) {
  switch (status) {
    case SPS_PARSE_OK:
      return "ok";
    case SPS_PARSE_LONGER_THAN_CAP:
      return "longer_than_cap";
    case SPS_PARSE_NOT_AN_SPS:
      return "not_an_sps";
    case SPS_PARSE_TRUNCATED:
      return "truncated";
    case SPS_PARSE_UE_TOO_LONG:
      return "ue_too_long";
    case SPS_PARSE_SPS_ID_RANGE:
      return "sps_id_out_of_range";
    case SPS_PARSE_CHROMA_RANGE:
      return "chroma_format_out_of_range";
    case SPS_PARSE_POC_TYPE_RANGE:
      return "poc_type_out_of_range";
    case SPS_PARSE_POC_CYCLE_LARGE:
      return "poc_cycle_too_large";
    case SPS_PARSE_CPB_CNT_LARGE:
      return "cpb_cnt_too_large";
  }
  return "unknown";
}
