// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "sps_rewrite.h"

#include <string.h>

/* Bit writer into a fixed buffer; `over` is sticky. */
typedef struct {
  uint8_t *data;
  uint32_t cap_bits;
  uint32_t pos;
  int over;
} BitOut;

static void put_bit(BitOut *w, unsigned bit) {
  if (w->pos >= w->cap_bits) {
    w->over = 1;
    return;
  }
  if (bit)
    w->data[w->pos >> 3] |= (uint8_t)(0x80u >> (w->pos & 7));
  w->pos++;
}

static unsigned get_bit(const uint8_t *d, uint32_t pos) {
  return (d[pos >> 3] >> (7 - (pos & 7))) & 1u;
}

static void put_ue(BitOut *w, uint32_t v) {
  uint64_t code = (uint64_t)v + 1;
  unsigned n = 0;
  while ((code >> (n + 1)) != 0)
    n++;
  for (unsigned i = 0; i < n; i++)
    put_bit(w, 0);
  for (int i = (int)n; i >= 0; i--)
    put_bit(w, (unsigned)((code >> i) & 1u));
}

static uint32_t ue_len_bits(uint32_t v) {
  uint64_t code = (uint64_t)v + 1;
  unsigned n = 0;
  while ((code >> (n + 1)) != 0)
    n++;
  return 2 * n + 1;
}

static void copy_bits(BitOut *w, const uint8_t *src, uint32_t from, uint32_t to) {
  for (uint32_t p = from; p < to; p++)
    put_bit(w, get_bit(src, p));
}

SpsRewriteStatus sps_rewrite_max_refs(const uint8_t *nal, uint32_t nal_len, uint32_t target_refs,
                                      uint8_t *out, uint32_t out_cap, uint32_t *out_len,
                                      SpsRewriteInfo *info) {
  if (nal == NULL || out == NULL || out_len == NULL || nal_len == 0 || nal_len > SPS_REWRITE_MAX_IN)
    return SPS_REWRITE_UNPARSEABLE;
  SpsFields f;
  if (sps_probe_parse(nal, nal_len, nal_len, &f) != SPS_PARSE_OK)
    return SPS_REWRITE_UNPARSEABLE;
  if (f.max_num_ref_frames <= target_refs)
    return SPS_REWRITE_NOT_NEEDED;

  /* Remove emulation prevention (00 00 03 -> 00 00), the same rule the reader used for the bit
   * offsets in `f`. */
  uint8_t rbsp[SPS_REWRITE_MAX_IN];
  uint32_t rlen = 0;
  unsigned zeros = 0;
  for (uint32_t i = 0; i < nal_len; i++) {
    if (zeros >= 2 && nal[i] == 0x03) {
      zeros = 0;
      continue;
    }
    rbsp[rlen++] = nal[i];
    zeros = nal[i] == 0 ? zeros + 1 : 0;
  }
  /* Position of the rbsp stop bit: the last 1 bit. Everything after it is alignment zeros. */
  uint32_t end_bit = rlen * 8u;
  while (end_bit > 0 && get_bit(rbsp, end_bit - 1) == 0)
    end_bit--;
  if (end_bit == 0)
    return SPS_REWRITE_UNPARSEABLE;
  /* end_bit is now one past the stop bit. */

  uint32_t a = f.max_num_ref_frames_bit;
  uint32_t a_end = a + ue_len_bits(f.max_num_ref_frames);
  int has_dfb = f.vui && f.bitstream_restriction;
  uint32_t b = f.max_dec_frame_buffering_bit;
  uint32_t b_end = has_dfb ? b + ue_len_bits(f.max_dec_frame_buffering) : 0;
  if (a_end > end_bit || (has_dfb && (b < a_end || b_end > end_bit)))
    return SPS_REWRITE_UNPARSEABLE;

  uint8_t nrbsp[SPS_REWRITE_MAX_OUT];
  memset(nrbsp, 0, sizeof(nrbsp));
  BitOut w = {nrbsp, (uint32_t)sizeof(nrbsp) * 8u, 0, 0};
  copy_bits(&w, rbsp, 0, a);
  put_ue(&w, target_refs);
  if (has_dfb) {
    copy_bits(&w, rbsp, a_end, b);
    put_ue(&w, target_refs);
    copy_bits(&w, rbsp, b_end, end_bit);
  } else {
    copy_bits(&w, rbsp, a_end, end_bit);
  }
  if (w.over)
    return SPS_REWRITE_BUFFER_TOO_SMALL;
  uint32_t nlen = (w.pos + 7) / 8;

  /* Re-insert emulation prevention: 00 00 followed by 00..03 gets a 03 in between. */
  uint32_t o = 0;
  zeros = 0;
  for (uint32_t i = 0; i < nlen; i++) {
    if (zeros >= 2 && nrbsp[i] <= 3) {
      if (o >= out_cap)
        return SPS_REWRITE_BUFFER_TOO_SMALL;
      out[o++] = 0x03;
      zeros = 0;
    }
    if (o >= out_cap)
      return SPS_REWRITE_BUFFER_TOO_SMALL;
    out[o++] = nrbsp[i];
    zeros = nrbsp[i] == 0 ? zeros + 1 : 0;
  }
  *out_len = o;
  if (info != NULL) {
    info->refs_old = f.max_num_ref_frames;
    info->refs_new = target_refs;
    info->dfb_old = has_dfb ? f.max_dec_frame_buffering : UINT32_MAX;
    info->dfb_new = has_dfb ? target_refs : UINT32_MAX;
    info->len_old = nal_len;
    info->len_new = o;
  }
  return SPS_REWRITE_DONE;
}

const char *sps_rewrite_status_name(SpsRewriteStatus status) {
  switch (status) {
    case SPS_REWRITE_DONE:
      return "rewritten";
    case SPS_REWRITE_NOT_NEEDED:
      return "not_needed";
    case SPS_REWRITE_UNPARSEABLE:
      return "unparseable";
    case SPS_REWRITE_BUFFER_TOO_SMALL:
      return "buffer_too_small";
  }
  return "unknown";
}
