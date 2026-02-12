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
