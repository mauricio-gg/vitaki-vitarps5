// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_VIDEORECEIVER_GAP_H
#define CHIAKI_VIDEORECEIVER_GAP_H

#include <chiaki/seqnum.h>
#include <chiaki/common.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct chiaki_video_gap_report_state_t
{
	bool pending;
	ChiakiSeqNum16 start;
	ChiakiSeqNum16 end;
	uint64_t deadline_ms;
} ChiakiVideoGapReportState;

typedef enum chiaki_video_gap_update_action_t
{
	CHIAKI_VIDEO_GAP_UPDATE_NONE = 0,
	CHIAKI_VIDEO_GAP_UPDATE_SET_PENDING = 1,
	CHIAKI_VIDEO_GAP_UPDATE_FLUSH_PREVIOUS = 2,
	CHIAKI_VIDEO_GAP_UPDATE_EXTEND_PENDING = 3,
} ChiakiVideoGapUpdateAction;

CHIAKI_EXPORT ChiakiVideoGapUpdateAction chiaki_video_gap_report_update(
		ChiakiVideoGapReportState *state,
		ChiakiSeqNum16 expected_start,
		ChiakiSeqNum16 gap_end,
		uint64_t now_ms,
		uint64_t hold_ms,
		ChiakiSeqNum16 *flush_start,
		ChiakiSeqNum16 *flush_end);

// Shared 16-bit sequence-number helpers used by both the gap-hold state
// machine above and the corrupt-report cooldown classifier below. Mirrors
// the RFC 1982-style wraparound comparisons in chiaki/seqnum.h. Prefixed
// (unlike most statics in this internal header) because they're generic
// enough that a same-named local in an including .c file is plausible.
static inline bool chiaki_seq16_inclusive_ge(ChiakiSeqNum16 a, ChiakiSeqNum16 b)
{
	return a == b || chiaki_seq_num_16_gt(a, b);
}

static inline uint32_t chiaki_seq16_span(ChiakiSeqNum16 start, ChiakiSeqNum16 end)
{
	// Compute inclusive span in 16-bit sequence space. This handles wrap-around
	// (e.g. start=65535,end=0 => span=2) and the full-range case
	// (start=0,end=65535 => span=65536) without truncation.
	// For start==end, inclusive semantics intentionally return 1.
	uint32_t start_u16 = (uint32_t)(uint16_t)start;
	uint32_t end_u16 = (uint32_t)(uint16_t)end;
	return ((end_u16 - start_u16) & 0xFFFFU) + 1U;
}

// Forward distance in 16-bit sequence space: how many frame indices advanced
// from `prev` to `cur`. Wrap-safe (65535 -> 2 yields 3). Returns 0 when
// cur == prev. chiaki/seqnum.h exposes only _lt()/_gt() -- no distance helper
// -- so this is derived from chiaki_seq16_span()'s inclusive span, which is
// already unit-tested for wrap and full-range behaviour. There is no distinct
// "backwards" case: unsigned wraparound arithmetic only knows "the long way
// around", so a `cur` that is RFC-1982-behind `prev` yields a large delta
// close to 65536 rather than a negative one. This codebase's only caller
// (chiaki_video_receiver_av_packet()) already gates on chiaki_seq_num_16_gt()
// before reaching here, so that case is unreached in practice.
static inline uint32_t chiaki_seq16_forward_delta(ChiakiSeqNum16 prev, ChiakiSeqNum16 cur)
{
	if(prev == cur)
		return 0U;
	return chiaki_seq16_span(prev, cur) - 1U;
}

// --- D5: console->Vita video delivery-pattern probe (GH #251) --------------
// Inter-frame gap histogram edges, ms, upper-exclusive. Deliberately ABSOLUTE
// rather than fps-scaled: the behaviours under test (console-side byte pacing,
// Wi-Fi batching, client radio power-save) live in wall-clock time, not frame
// time. Tuned around the 33ms/30fps nominal so the two modes hardware log
// 86770605157 shows (cadence_min clustered 19-23ms in 83/92 windows,
// cadence_max clustered 53-61ms in 88/92) each get their OWN bucket instead of
// being smeared into the nominal bucket -- min/max/avg over ~31 samples cannot
// tell "one outlier per second" from "a repeating bimodal pattern", which is
// the single blind spot this histogram exists to close. Note bucket 4
// ([40,52)) is the discriminator: a pacer/frame-clock quantises INTO bucket 5
// and leaves 4 nearly empty; a congested link smears continuously through 4.
// At 60fps the 16.7ms nominal lands in bucket 1 -- read nom_ms= on the
// PIPE/DELIVERY_INIT line before interpreting.
//
// Both videoreceiver_gap.c (the bucketing function below) and videoreceiver.c
// (the one-time PIPE/DELIVERY_INIT log line) need these edges, so they live
// here rather than in videoreceiver.c, to avoid duplicating the values.
#define VIDEO_GAP_HIST_EDGE_0_MS    10U  // [0,10)    burst / catch-up release
#define VIDEO_GAP_HIST_EDGE_1_MS    20U  // [10,20)   sub-half-period
#define VIDEO_GAP_HIST_EDGE_2_MS    28U  // [20,28)   observed "early" mode
#define VIDEO_GAP_HIST_EDGE_3_MS    40U  // [28,40)   nominal 30fps period (33.3)
#define VIDEO_GAP_HIST_EDGE_4_MS    52U  // [40,52)   DISCRIMINATOR band
#define VIDEO_GAP_HIST_EDGE_5_MS    70U  // [52,70)   observed "late" mode
#define VIDEO_GAP_HIST_EDGE_6_MS   100U  // [70,100)  ~3 frame periods
#define VIDEO_GAP_HIST_EDGE_7_MS   300U  // [100,300) stall
                                         // [300,inf) hard stall (top bucket)
// Number of edges above; the top (catch-all) bucket is index
// VIDEO_GAP_HIST_EDGE_COUNT itself, so the bucketing function returns
// [0, VIDEO_GAP_HIST_EDGE_COUNT] inclusive -- VIDEO_GAP_HIST_EDGE_COUNT + 1
// values, which must equal CHIAKI_VIDEO_GAP_HIST_BUCKETS
// (chiaki/videoreceiver.h) since that constant sizes gap_hist[]. Kept as a
// literal count here rather than including the public header (heavy: pulls in
// video.h/takion.h/frameprocessor.h/bitstream.h) into this lightweight
// internal header -- mirrors the existing CHIAKI_VIDEO_RECEIVER_REF_SLOTS /
// RECEIVER_REF_SLOTS aliasing pattern's reliance on the two staying in sync
// by convention rather than by a shared symbol.
#define VIDEO_GAP_HIST_EDGE_COUNT 8U

// Buckets gap_ms into [0, VIDEO_GAP_HIST_EDGE_COUNT] using the edges above
// (upper-exclusive; the last bucket catches everything >= EDGE_7). Pure
// function of its argument, unit-tested in test/packet_path_tests.c.
CHIAKI_EXPORT uint32_t chiaki_video_gap_hist_bucket(uint64_t gap_ms);

typedef enum chiaki_video_corrupt_report_disposition_t
{
	// 0 is the safest default for an accidentally-zero-initialized/uninitialized
	// value: "nothing to do", not "send it" -- so EMIT is deliberately nonzero.
	CHIAKI_VIDEO_CORRUPT_REPORT_OBSOLETE = 0, // no new info beyond the last report (or a pathological span) -- safe to drop pending state
	CHIAKI_VIDEO_CORRUPT_REPORT_DEFER = 1,    // new info exists but is rate-limited -- caller must retry later
	CHIAKI_VIDEO_CORRUPT_REPORT_EMIT = 2,     // send it now
} ChiakiVideoCorruptReportDisposition;

/**
 * Classify whether a corrupt-frame report [start, end] should be sent to the
 * console, given the range actually last reported ([last_start, last_end] at
 * last_at_ms). Pure function of its arguments -- no I/O, no session/log
 * access -- so the cooldown/growth-bypass/finalization policy can be unit
 * tested directly (see test/packet_path_tests.c) without a full
 * ChiakiVideoReceiver.
 *
 * @param bypass_cooldown Set when the caller is retiring [last_start, last_end]
 *   for good (its start value will never be offered again -- e.g. the gap-hold
 *   state machine above is about to start tracking a new range, or
 *   frame_index_prev_complete is about to advance past it). Rate limiting
 *   exists to reduce re-report volume on a range that will keep being
 *   re-offered; it must not cost the console its last chance to learn a
 *   range's full extent. Does NOT bypass span_sanity_max below -- a
 *   pathological span must never be reported regardless of the reason.
 * @param cooldown_ms Minimum time between re-reports of a still-expanding
 *   same-start range (see VIDEO_CORRUPT_REPORT_COOLDOWN_MS in videoreceiver.c).
 * @param growth_bypass_span Growth (in frames) since the last report that
 *   forces an immediate refresh regardless of cooldown_ms (see
 *   VIDEO_CORRUPT_REPORT_GROWTH_BYPASS_SPAN in videoreceiver.c).
 * @param span_sanity_max Absolute ceiling on [start, end]'s inclusive span
 *   (see VIDEO_SPAN_SANITY_MAX in videoreceiver.c). Checked first, ahead of
 *   every other branch: a span this large means corrupted sequence state or
 *   a multi-thousand-frame recv-thread freeze/reconnect arc, not a genuine
 *   reportable gap, and must never reach the console even via
 *   bypass_cooldown=true -- doing so would hand the PS5's bitrate ratchet
 *   the strongest possible false loss signal.
 */
CHIAKI_EXPORT ChiakiVideoCorruptReportDisposition chiaki_video_corrupt_report_classify(
		ChiakiSeqNum16 last_start,
		ChiakiSeqNum16 last_end,
		uint64_t last_at_ms,
		ChiakiSeqNum16 start,
		ChiakiSeqNum16 end,
		uint64_t now_ms,
		bool bypass_cooldown,
		uint64_t cooldown_ms,
		uint32_t growth_bypass_span,
		uint32_t span_sanity_max);

#endif // CHIAKI_VIDEORECEIVER_GAP_H
