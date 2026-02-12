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

#endif // CHIAKI_VIDEORECEIVER_GAP_H
