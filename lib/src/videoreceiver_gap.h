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
