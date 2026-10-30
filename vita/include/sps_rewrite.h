// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

/*
 * SPS rewrite (#473): lowers max_num_ref_frames and, when the SPS has a VUI
 * bitstream restriction, max_dec_frame_buffering to a target, so a hardware decoder that only
 * accepts a few reference frames does not hold its output waiting for the 9 the console declares.
 * Every other bit of the SPS is kept; emulation-prevention bytes are rebuilt. Pure function, no
 * SDK dependency, no allocation: it compiles and runs natively.
 */
#pragma once

#include <stdint.h>

#include "sps_probe.h"

/* Largest SPS NAL (NAL header byte included, start code not) the rewrite accepts; the same cap
 * as the SPS reader. */
#define SPS_REWRITE_MAX_IN SPS_PROBE_CAP
/* Output buffer size that always fits: input plus the bytes a larger code and new emulation
 * prevention can add. */
#define SPS_REWRITE_MAX_OUT (SPS_REWRITE_MAX_IN + 16)

typedef enum {
  SPS_REWRITE_DONE = 0,        /* out holds the rewritten NAL */
  SPS_REWRITE_NOT_NEEDED,      /* max_num_ref_frames already <= target; out is not written */
  SPS_REWRITE_UNPARSEABLE,     /* the SPS could not be read; out is not written */
  SPS_REWRITE_BUFFER_TOO_SMALL /* the result does not fit out_cap; out is not written */
} SpsRewriteStatus;

typedef struct {
  uint32_t refs_old, refs_new;
  uint32_t dfb_old, dfb_new; /* UINT32_MAX when the SPS has no bitstream restriction (untouched) */
  uint32_t len_old, len_new;
} SpsRewriteInfo;

/* nal: the SPS NAL unit starting at its header byte. On SPS_REWRITE_DONE, out[0..*out_len) is the
 * new NAL. info (may be NULL) is filled for DONE. */
SpsRewriteStatus sps_rewrite_max_refs(const uint8_t *nal, uint32_t nal_len, uint32_t target_refs,
                                      uint8_t *out, uint32_t out_cap, uint32_t *out_len,
                                      SpsRewriteInfo *info);

const char *sps_rewrite_status_name(SpsRewriteStatus status);
