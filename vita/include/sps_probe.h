// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

/*
 * SPS probe (#413): notices an H.264 sequence parameter set (SPS) at the very start of a video
 * buffer, keeps the first one of the session and counts later ones (how many, how many differed
 * from the first). It only reads: the buffer handed to the decoder is never touched. It has no SDK
 * dependency so it compiles and runs natively. It also reads the stored SPS with a bounded reader
 * of its own (sps_probe_parse) for the PIPE/SPS log line. See docs/design/streaming.md, 9.5.
 *
 * Threading: sps_probe_observe() is called from one thread only (the Takion receive thread, via
 * vita_h264_decode_frame()). It does a few byte compares per frame and, only for a buffer that
 * begins with an SPS, one bounded copy / compare. Nothing is parsed or printed there. The
 * reporting thread (UI) reads with sps_probe_snapshot(), a sequence lock: the writer makes the
 * lock word odd while it writes. sps_probe_reset() runs from vita_h264_start(), before the
 * receive thread delivers frames.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Bytes of the SPS NAL unit (starting at its NAL header byte, start code not included) that are
 * kept and compared. A typical SPS is 20 to 40 bytes; 128 leaves room for a VUI and scaling lists.
 */
#define SPS_PROBE_CAP 128
/* How far past the NAL header sps_probe_locate() looks for the end of the SPS. Real SPS are far
 * shorter, so a start code is found at once; the bound only stops a damaged buffer from being
 * scanned in full on the receive thread. */
#define SPS_PROBE_SCAN_MAX 512
/* NAL header + profile_idc + constraint flags + level_idc: shorter than this is not an SPS. */
#define SPS_PROBE_MIN_NAL_LEN 4

typedef struct {
  /* True length of the first SPS NAL of the session, 0 until one has been seen. If it is larger
   * than SPS_PROBE_CAP only the first SPS_PROBE_CAP bytes are in `first`. */
  uint32_t first_len;
  uint8_t first[SPS_PROBE_CAP];
  /* SPS seen after the first one, and how many of those differed from it (length, then the bytes
   * up to SPS_PROBE_CAP). */
  uint32_t seen_again;
  uint32_t differed;
} SpsProbeData;

/* Tells whether buf (size bytes) begins with an SPS NAL unit: a 3- or 4-byte start code followed
 * by a NAL header of type 7. On true, *nal_off is where the NAL header byte sits (3 or 4) and
 * *nal_len its length up to the next start code (00 00 00 / 00 00 01), or up to the end of the
 * buffer, or SPS_PROBE_SCAN_MAX bytes, whichever comes first. A buffer that starts with anything
 * else (an access unit delimiter, a slice, a PPS) is not noticed. */
bool sps_probe_locate(const uint8_t *buf, size_t size, size_t *nal_off, size_t *nal_len);

/* Clears everything for a new session. */
void sps_probe_reset(void);

/* Receive thread, once per video buffer: records the buffer's SPS if it begins with one. */
void sps_probe_observe(const uint8_t *buf, size_t size);

/* Changes every time the recorded data changes (odd while a write is in progress). A cheap test
 * for "is there anything new to copy". Any thread. */
uint32_t sps_probe_version(void);

/* Copies the recorded data consistently, trying up to `attempts` times. False if every try
 * overlapped a write. Any thread. */
bool sps_probe_snapshot(SpsProbeData *out, int attempts);

/* Largest values the standard allows for the counts that size a loop in the SPS. A larger value
 * is refused, never used. */
#define SPS_MAX_ID 31
#define SPS_MAX_POC_CYCLE 255
#define SPS_MAX_CPB_CNT_MINUS1 31

/* Why sps_probe_parse() refused an SPS. */
typedef enum {
  SPS_PARSE_OK = 0,
  SPS_PARSE_LONGER_THAN_CAP, /* more bytes than are stored: only a part is available */
  SPS_PARSE_NOT_AN_SPS,      /* first byte is not an SPS NAL header */
  SPS_PARSE_TRUNCATED,       /* ran out of bits before the last field we read */
  SPS_PARSE_UE_TOO_LONG,     /* Exp-Golomb code with 32 or more leading zeros */
  SPS_PARSE_SPS_ID_RANGE,    /* seq_parameter_set_id above 31 */
  SPS_PARSE_CHROMA_RANGE,    /* chroma_format_idc above 3 */
  SPS_PARSE_POC_TYPE_RANGE,  /* pic_order_cnt_type above 2 */
  SPS_PARSE_POC_CYCLE_LARGE, /* num_ref_frames_in_pic_order_cnt_cycle above 255 */
  SPS_PARSE_CPB_CNT_LARGE    /* HRD cpb_cnt_minus1 above 31 */
} SpsParseStatus;

/* The fields PIPE/SPS prints. Valid only when sps_probe_parse() returned SPS_PARSE_OK. */
typedef struct {
  uint8_t profile_idc;
  uint8_t constraint_flags; /* the raw byte after profile_idc */
  uint8_t level_idc;
  uint32_t poc_type;
  uint32_t frame_mbs_only;
  uint32_t max_num_ref_frames;
  bool vui;                         /* VUI parameters present */
  bool bitstream_restriction;       /* only ever true when `vui` is */
  uint32_t num_reorder_frames;      /* valid only when bitstream_restriction */
  uint32_t max_dec_frame_buffering; /* valid only when bitstream_restriction */
  /* Bit offsets of those two Exp-Golomb codes in the NAL with its emulation-prevention bytes
   * removed (NAL header byte included), for sps_rewrite.c (#473). */
  uint32_t max_num_ref_frames_bit;
  uint32_t max_dec_frame_buffering_bit; /* valid only when bitstream_restriction */
} SpsFields;

/* Reads the SPS NAL unit at nal (NAL header byte first, start code not included): `stored` bytes
 * are in memory, `true_len` is the NAL's real length (greater than `stored` when it was cut at
 * SPS_PROBE_CAP; such an SPS is refused). Every read is bounded by `stored`; running out of bits
 * is SPS_PARSE_TRUNCATED, and nothing is written to *out unless the result is SPS_PARSE_OK. Uses
 * a stack buffer of at most SPS_PROBE_CAP bytes, no allocation. Any thread. */
SpsParseStatus sps_probe_parse(const uint8_t *nal, uint32_t stored, uint32_t true_len,
                               SpsFields *out);

/* The `fail=` token for a refusal (for example "truncated"); "ok" for SPS_PARSE_OK. */
const char *sps_probe_status_name(SpsParseStatus status);
