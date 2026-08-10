// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "videoreceiver_gap.h"

#include <chiaki/seqnum.h>

CHIAKI_EXPORT ChiakiVideoGapUpdateAction chiaki_video_gap_report_update(
			ChiakiVideoGapReportState *state,
		ChiakiSeqNum16 expected_start,
		ChiakiSeqNum16 gap_end,
		uint64_t now_ms,
		uint64_t hold_ms,
			ChiakiSeqNum16 *flush_start,
			ChiakiSeqNum16 *flush_end)
{
	// Called from the video receiver packet path (single-threaded per receiver).
	// State is intentionally unsynchronized and must not be shared cross-thread.
	if(!state)
		return CHIAKI_VIDEO_GAP_UPDATE_NONE;

	if(!state->pending)
	{
		state->pending = true;
		state->start = expected_start;
		state->end = gap_end;
		state->deadline_ms = now_ms + hold_ms;
		return CHIAKI_VIDEO_GAP_UPDATE_SET_PENDING;
	}

	if(state->start != expected_start)
	{
		if(flush_start)
			*flush_start = state->start;
		if(flush_end)
			*flush_end = state->end;
		state->pending = true;
		state->start = expected_start;
		state->end = gap_end;
		state->deadline_ms = now_ms + hold_ms;
		return CHIAKI_VIDEO_GAP_UPDATE_FLUSH_PREVIOUS;
	}

	if(chiaki_seq_num_16_gt(gap_end, state->end))
	{
		state->end = gap_end;
		return CHIAKI_VIDEO_GAP_UPDATE_EXTEND_PENDING;
	}

	return CHIAKI_VIDEO_GAP_UPDATE_NONE;
}

CHIAKI_EXPORT ChiakiCorruptReportDisposition chiaki_corrupt_report_classify(
		ChiakiSeqNum16 last_start,
		ChiakiSeqNum16 last_end,
		uint64_t last_at_ms,
		ChiakiSeqNum16 start,
		ChiakiSeqNum16 end,
		uint64_t now_ms,
		bool bypass_cooldown,
		uint64_t cooldown_ms,
		uint32_t growth_bypass_span)
{
	if(last_start != start)
		return CHIAKI_CORRUPT_REPORT_EMIT; // first report of a new burst always fires immediately, uncooled

	if(seq16_inclusive_ge(last_end, end))
		return CHIAKI_CORRUPT_REPORT_OBSOLETE; // no new information beyond what was already reported

	if(bypass_cooldown)
		return CHIAKI_CORRUPT_REPORT_EMIT; // finalization: this range retires now and is never offered again

	// Same burst, range has expanded: rate-limit the re-report unless either
	// the growth since the last report or the elapsed time justify refreshing
	// the console's view of the burst's extent now.
	uint32_t growth = seq16_span(last_end, end) - 1U;
	if(growth >= growth_bypass_span)
		return CHIAKI_CORRUPT_REPORT_EMIT;

	uint64_t elapsed_ms = now_ms - last_at_ms;
	if(elapsed_ms >= cooldown_ms)
		return CHIAKI_CORRUPT_REPORT_EMIT;

	return CHIAKI_CORRUPT_REPORT_DEFER;
}
