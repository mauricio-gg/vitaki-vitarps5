// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/videoreceiver.h>
#include <chiaki/session.h>
#include <chiaki/time.h>

#include <string.h>
#include "videoreceiver_gap.h"

#define RECEIVER_REF_SLOTS CHIAKI_VIDEO_RECEIVER_REF_SLOTS

static ChiakiErrorCode chiaki_video_receiver_flush_frame(ChiakiVideoReceiver *video_receiver);

// Hold tiny gap reports briefly so out-of-order packets can fill them first.
#define VIDEO_GAP_REPORT_HOLD_MS 24
// Force-report larger contiguous spans immediately instead of waiting.
#define VIDEO_GAP_REPORT_FORCE_SPAN 12
// Guard against pathological spans from corrupted sequence state.
#define VIDEO_SPAN_SANITY_MAX 4096U
// Rate-limit re-reports of a growing same-start corrupt-frame range during a
// single stall. Hardware evidence (24402261711_vitarps5-testing.log, lines
// 1719-1853; "StreamConnection reporting corrupt frame(s)" at
// lib/src/streamconnection.c:1292 is the authoritative per-emission log line
// -- it fires exactly once per actual wire send) shows one burst (frames
// 175-222) producing 17 separate emissions over ~1.3s of wall time, several
// less than 20ms apart, as the HOLD_MS batching above was repeatedly
// bypassed by the FORCE_SPAN check once the gap outgrew it. A worse burst
// later in the same log (start=615, from line 3067) produces 21 emissions
// for one stall. Bitrate in this log is steeply asymmetric rather than
// strictly downward-only: large multiplicative drops on loss/corruption
// (5825000 -> 5480000 -> 5184000 -> 4473000 -> 3904000 -> ... -> 3579000)
// interspersed with six +25000bps recovery-crawl steps during quieter
// periods -- report bursts like the ones above are what drive the drops. A
// 500ms cooldown cuts a burst like this from ~17 reports to roughly 3-4
// (the immediate first report plus a few periodic refreshes) while still
// keeping the console's view of the burst reasonably current.
#define VIDEO_CORRUPT_REPORT_COOLDOWN_MS 500
// Bypass the cooldown immediately once the range has grown by this many
// frames since the last report, so a single large jump (e.g. a bigger
// resync gap) isn't silently under-reported for a full cooldown window.
// This is a safety valve for large jumps, not a steady-frame-rate refresh
// path: per-report growth in the cited burst rarely exceeded single digits
// (max 8), and this stream ran at 30fps (session init logs "fps=30";
// PIPE/FPS shows target=30 throughout), where 32 frames is ~1.07s -- well
// longer than the cooldown itself, so it will not trigger from
// steady-frame-rate growth alone. Kept above VIDEO_GAP_REPORT_FORCE_SPAN
// (12) so it doesn't also fire on every initial force-spanned detection.
#define VIDEO_CORRUPT_REPORT_GROWTH_BYPASS_SPAN 32
#define IDR_REQUEST_COOLDOWN_MS 100
#define IDR_REQUEST_TIMEOUT_MS 1000
#define CASCADE_SKIP_THRESHOLD 3

static void add_ref_frame(ChiakiVideoReceiver *video_receiver, int32_t frame)
{
	if(video_receiver->reference_frames[0] != -1)
	{
		memmove(&video_receiver->reference_frames[1], &video_receiver->reference_frames[0],
			sizeof(int32_t) * (RECEIVER_REF_SLOTS - 1));
		video_receiver->reference_frames[0] = frame;
		return;
	}
	for(int i = RECEIVER_REF_SLOTS - 1; i >= 0; i--)
	{
		if(video_receiver->reference_frames[i] == -1)
		{
			video_receiver->reference_frames[i] = frame;
			return;
		}
	}
}

static bool have_ref_frame(ChiakiVideoReceiver *video_receiver, int32_t frame)
{
	for(int i = 0; i < RECEIVER_REF_SLOTS; i++)
		if(video_receiver->reference_frames[i] == frame)
			return true;
	return false;
}

// seq16_inclusive_ge() / seq16_span() and the corrupt-report cooldown
// classifier (chiaki_corrupt_report_classify()) live in videoreceiver_gap.h/.c
// so the rate-limiting policy is a pure, unit-testable function -- see
// test/packet_path_tests.c.

static uint32_t saturating_add_u32(uint32_t lhs, uint32_t rhs)
{
	if(lhs > UINT32_MAX - rhs)
		return UINT32_MAX;
	return lhs + rhs;
}

// Classifies and, if warranted, emits a corrupt-frame report for [start, end].
// Delegates the emit/obsolete/defer decision to chiaki_corrupt_report_classify()
// (videoreceiver_gap.h) so that policy stays pure and unit-testable. Returns
// the disposition so callers that track a "pending" flag for the range know
// whether they can safely clear it (EMIT or OBSOLETE), or must keep it set
// for a later retry (DEFER) so the console eventually learns the burst's
// full extent instead of only ever seeing its earliest, narrowest range.
//
// @param bypass_cooldown Pass true when [start, end] is being retired (its
//   start value will never be offered again) rather than routinely
//   re-reported -- see the call sites in chiaki_video_receiver_av_packet()
//   and chiaki_video_receiver_flush_frame() for why each needs this.
static ChiakiCorruptReportDisposition report_corrupt_frame_range(ChiakiVideoReceiver *video_receiver, ChiakiSeqNum16 start, ChiakiSeqNum16 end, const char *reason, uint64_t now_ms, bool bypass_cooldown)
{
	ChiakiCorruptReportDisposition disposition = chiaki_corrupt_report_classify(
		(ChiakiSeqNum16)video_receiver->last_reported_corrupt_start,
		(ChiakiSeqNum16)video_receiver->last_reported_corrupt_end,
		video_receiver->last_reported_corrupt_at_ms,
		start, end, now_ms, bypass_cooldown,
		VIDEO_CORRUPT_REPORT_COOLDOWN_MS,
		VIDEO_CORRUPT_REPORT_GROWTH_BYPASS_SPAN);
	if(disposition != CHIAKI_CORRUPT_REPORT_EMIT)
		return disposition;
	CHIAKI_LOGW(video_receiver->log, "Detected missing or corrupt frame(s) from %d to %d%s%s",
				(int)start,
			(int)end,
			reason ? " reason=" : "",
			reason ? reason : "");
	stream_connection_send_corrupt_frame(&video_receiver->session->stream_connection, start, end);
	video_receiver->last_reported_corrupt_start = start;
	video_receiver->last_reported_corrupt_end = end;
	video_receiver->last_reported_corrupt_at_ms = now_ms;
	return CHIAKI_CORRUPT_REPORT_EMIT;
}

static void flush_pending_gap_report(ChiakiVideoReceiver *video_receiver, uint64_t now_ms, bool force)
{
	if(!video_receiver->gap_report_pending)
		return;

	uint32_t span = seq16_span((ChiakiSeqNum16)video_receiver->gap_report_start,
		(ChiakiSeqNum16)video_receiver->gap_report_end);
	if(span > VIDEO_SPAN_SANITY_MAX)
	{
		CHIAKI_LOGW(video_receiver->log,
			"Suppressing pathological gap span %u (%d-%d)",
			(unsigned int)span,
			(int)video_receiver->gap_report_start,
			(int)video_receiver->gap_report_end);
		video_receiver->gap_report_pending = false;
		return;
	}
	if(!force && now_ms < video_receiver->gap_report_deadline_ms &&
		span < VIDEO_GAP_REPORT_FORCE_SPAN)
		return;

	// Only leave the pending flag set on DEFER (cooldown-suppressed but still
	// carries new information). This function runs on every incoming AV
	// packet (see chiaki_video_receiver_av_packet), so a deferred report
	// retries and emits as soon as the cooldown elapses or the range grows
	// past the bypass threshold -- without this, a cooldown-skipped report
	// would silently drop the burst's tail forever instead of merely being
	// delayed. EMIT and OBSOLETE both mean there's nothing left to retry (the
	// report went out, or the console was already fully caught up), so both
	// clear pending -- treating OBSOLETE like DEFER would leave a
	// fully-covered range latched as "pending" forever.
	ChiakiCorruptReportDisposition disposition = report_corrupt_frame_range(video_receiver,
		(ChiakiSeqNum16)video_receiver->gap_report_start,
		(ChiakiSeqNum16)video_receiver->gap_report_end,
		force ? "forced" : "held",
		now_ms,
		false /* bypass_cooldown: this is a routine re-report, not a retirement */);
	if(disposition != CHIAKI_CORRUPT_REPORT_DEFER)
		video_receiver->gap_report_pending = false;
}

static void video_receiver_maybe_request_idr(ChiakiVideoReceiver *video_receiver, uint64_t now_ms, const char *reason)
{
	if(video_receiver->idr_request_pending)
	{
		uint64_t idr_age_ms = now_ms - video_receiver->idr_request_start_ms;
		if(idr_age_ms > IDR_REQUEST_TIMEOUT_MS)
		{
			CHIAKI_LOGW(video_receiver->log,
				"IDR request timed out after %llu ms (%s), clearing pending state",
				(unsigned long long)idr_age_ms,
				reason ? reason : "unknown");
			video_receiver->idr_request_pending = false;
			video_receiver->idr_request_start_ms = 0;
		}
	}

	if(!video_receiver->idr_request_pending
		&& (now_ms - video_receiver->last_idr_request_ms >= IDR_REQUEST_COOLDOWN_MS))
	{
		/* PIPE/SEND_CHAIN_BLOCKED: measure recv-thread stall waiting for the send-chain mutex */
		uint64_t idr_t0 = now_ms;
		ChiakiErrorCode idr_err =
			chiaki_stream_connection_request_idr(&video_receiver->session->stream_connection);
		uint64_t idr_dur_ms = chiaki_time_now_monotonic_ms() - idr_t0;
		if(idr_err == CHIAKI_ERR_SUCCESS)
		{
			video_receiver->idr_request_pending = true;
			video_receiver->idr_request_start_ms = now_ms;
			video_receiver->last_idr_request_ms = now_ms;
			CHIAKI_LOGI(video_receiver->log, "Requesting IDR (%s)",
				reason ? reason : "unknown");
			CHIAKI_LOGD(video_receiver->log, "PIPE/SEND_CHAIN_BLOCKED ms=%llu",
						(unsigned long long)idr_dur_ms);
		}
	}
}

static void video_receiver_apply_cascade_reset(ChiakiVideoReceiver *video_receiver)
{
	memset(video_receiver->reference_frames, -1, sizeof(video_receiver->reference_frames));
	video_receiver->consecutive_missing_ref = 0;
	video_receiver->cascade_reset_attempts++;
	CHIAKI_LOGW(video_receiver->log,
		"Cascade recovery reset applied (attempt=%u)",
		video_receiver->cascade_reset_attempts);
}

CHIAKI_EXPORT void chiaki_video_receiver_init(ChiakiVideoReceiver *video_receiver, struct chiaki_session_t *session, ChiakiPacketStats *packet_stats)
{
	video_receiver->session = session;
	video_receiver->log = session->log;
	memset(video_receiver->profiles, 0, sizeof(video_receiver->profiles));
	video_receiver->profiles_count = 0;
	video_receiver->profile_cur = -1;

	video_receiver->frame_index_cur = -1;
	video_receiver->frame_index_prev = -1;
	video_receiver->frame_index_prev_complete = 0;

	chiaki_frame_processor_init(&video_receiver->frame_processor, video_receiver->log);
	video_receiver->packet_stats = packet_stats;

	video_receiver->frames_lost = 0;
	memset(video_receiver->reference_frames, -1, sizeof(video_receiver->reference_frames));
	chiaki_bitstream_init(&video_receiver->bitstream, video_receiver->log, video_receiver->session->connect_info.video_profile.codec);
	video_receiver->gap_report_pending = false;
	video_receiver->gap_report_start = 0;
	video_receiver->gap_report_end = 0;
	video_receiver->gap_report_deadline_ms = 0;
	video_receiver->last_reported_corrupt_start = 0;
	video_receiver->last_reported_corrupt_end = 0;
	video_receiver->last_reported_corrupt_at_ms = 0;
	video_receiver->cur_frame_seen_last_unit = false;
	video_receiver->cur_frame_first_packet_ms = 0;
	video_receiver->stage_window_start_ms = 0;
	video_receiver->stage_assemble_total_ms = 0;
	video_receiver->stage_submit_total_ms = 0;
	video_receiver->stage_window_frames = 0;
	video_receiver->stage_window_drops = 0;
	video_receiver->idr_request_pending = false;
	video_receiver->idr_request_start_ms = 0;
	video_receiver->old_frame_rejects_window = 0;
	video_receiver->last_idr_request_ms = 0;
	video_receiver->consecutive_missing_ref = 0;
	video_receiver->cascade_skip_count = 0;
	video_receiver->cascade_reset_attempts = 0;
	video_receiver->prev_frame_first_packet_ms = 0;
	video_receiver->cadence_min_ms = 0;
	video_receiver->cadence_max_ms = 0;
	video_receiver->cadence_total_ms = 0;
	video_receiver->cadence_count = 0;
	video_receiver->cadence_max_alarm_streak = 0;
	CHIAKI_LOGI(video_receiver->log,
		"Video gap profile: stable_default (hold_ms=%u force_span=%u)",
		VIDEO_GAP_REPORT_HOLD_MS,
		VIDEO_GAP_REPORT_FORCE_SPAN);
}

CHIAKI_EXPORT void chiaki_video_receiver_fini(ChiakiVideoReceiver *video_receiver)
{
	for(size_t i=0; i<video_receiver->profiles_count; i++)
		free(video_receiver->profiles[i].header);
	chiaki_frame_processor_fini(&video_receiver->frame_processor);
}

CHIAKI_EXPORT void chiaki_video_receiver_stream_info(ChiakiVideoReceiver *video_receiver, ChiakiVideoProfile *profiles, size_t profiles_count)
{
	if(video_receiver->profiles_count > 0)
	{
		CHIAKI_LOGE(video_receiver->log, "Video Receiver profiles already set");
		return;
	}

	memcpy(video_receiver->profiles, profiles, profiles_count * sizeof(ChiakiVideoProfile));
	video_receiver->profiles_count = profiles_count;

	CHIAKI_LOGI(video_receiver->log, "Video Profiles:");
	for(size_t i=0; i<video_receiver->profiles_count; i++)
	{
		ChiakiVideoProfile *profile = &video_receiver->profiles[i];
		CHIAKI_LOGI(video_receiver->log, "  %zu: %ux%u", i, profile->width, profile->height);
		chiaki_log_hexdump(video_receiver->log, CHIAKI_LOG_DEBUG, profile->header, profile->header_sz);
	}
}

CHIAKI_EXPORT void chiaki_video_receiver_av_packet(ChiakiVideoReceiver *video_receiver, ChiakiTakionAVPacket *packet)
{
	// Called on the stream/takion receive thread; gap report state is local to
	// this callback path and not mutated from render/UI threads.
	uint64_t now_ms = chiaki_time_now_monotonic_ms();
	flush_pending_gap_report(video_receiver, now_ms, false);

	// old frame?
	ChiakiSeqNum16 frame_index = packet->frame_index;
	if(video_receiver->frame_index_cur >= 0
		&& chiaki_seq_num_16_lt(frame_index, (ChiakiSeqNum16)video_receiver->frame_index_cur))
	{
		CHIAKI_LOGW(video_receiver->log, "Video Receiver received old frame packet");
		video_receiver->old_frame_rejects_window++;
		return;
	}

	// check adaptive stream index
	if(video_receiver->profile_cur < 0 || video_receiver->profile_cur != packet->adaptive_stream_index)
	{
		if(packet->adaptive_stream_index >= video_receiver->profiles_count)
		{
			CHIAKI_LOGE(video_receiver->log, "Packet has invalid adaptive stream index %lu >= %lu",
					(unsigned int)packet->adaptive_stream_index,
					(unsigned int)video_receiver->profiles_count);
			return;
		}
		video_receiver->profile_cur = packet->adaptive_stream_index;

		ChiakiVideoProfile *profile = video_receiver->profiles + video_receiver->profile_cur;
		CHIAKI_LOGI(video_receiver->log, "Switched to profile %d, resolution: %ux%u", video_receiver->profile_cur, profile->width, profile->height);
		if(video_receiver->session->video_sample_cb)
			video_receiver->session->video_sample_cb(profile->header, profile->header_sz, 0, false, video_receiver->session->video_sample_cb_user);
		if(!chiaki_bitstream_header(&video_receiver->bitstream, profile->header, profile->header_sz))
			CHIAKI_LOGE(video_receiver->log, "Failed to parse video header");
	}

	// next frame?
	if(video_receiver->frame_index_cur < 0 ||
		chiaki_seq_num_16_gt(frame_index, (ChiakiSeqNum16)video_receiver->frame_index_cur))
	{
		// last frame not flushed yet?
		if(video_receiver->frame_index_cur >= 0 && video_receiver->frame_index_prev != video_receiver->frame_index_cur)
		{
			chiaki_video_receiver_flush_frame(video_receiver);
		}

		ChiakiSeqNum16 next_frame_expected = (ChiakiSeqNum16)(video_receiver->frame_index_prev_complete + 1);
		if(chiaki_seq_num_16_gt(frame_index, next_frame_expected)
			&& !(frame_index == 1 && video_receiver->frame_index_cur < 0)) // ok for frame 1
		{
			ChiakiSeqNum16 gap_end = (ChiakiSeqNum16)(frame_index - 1);
			ChiakiVideoGapReportState gap_state = {
				.pending = video_receiver->gap_report_pending,
				.start = (ChiakiSeqNum16)video_receiver->gap_report_start,
				.end = (ChiakiSeqNum16)video_receiver->gap_report_end,
				.deadline_ms = video_receiver->gap_report_deadline_ms,
			};
			ChiakiSeqNum16 flush_start = 0;
			ChiakiSeqNum16 flush_end = 0;
			ChiakiVideoGapUpdateAction gap_action = chiaki_video_gap_report_update(
				&gap_state,
				next_frame_expected,
				gap_end,
				now_ms,
				VIDEO_GAP_REPORT_HOLD_MS,
				&flush_start,
				&flush_end);
			if(gap_action == CHIAKI_VIDEO_GAP_UPDATE_FLUSH_PREVIOUS)
			{
				// [flush_start, flush_end] is retiring right now: gap_state was
				// just reset above to track the NEW start (assigned to
				// video_receiver->gap_report_* below), so this is the only
				// remaining chance to tell the console about the old range --
				// it is never offered again after this call. Bypass the
				// cooldown: within a single av_packet call, the top-of-function
				// flush_pending_gap_report() (line ~304) may have already tried
				// and cooldown-suppressed this exact [start, end], and calling
				// the same pure classification again with identical inputs
				// would suppress it identically, permanently losing the range.
				// (Reason was previously "forced", which collided in meaning
				// with flush_pending_gap_report()'s unrelated force-past-HOLD_MS
				// parameter; "burst_retired" names what's actually happening.)
				report_corrupt_frame_range(video_receiver, flush_start, flush_end, "burst_retired", now_ms, true /* bypass_cooldown: this range is retiring */);
			}
			video_receiver->gap_report_pending = gap_state.pending;
			video_receiver->gap_report_start = gap_state.start;
			video_receiver->gap_report_end = gap_state.end;
			video_receiver->gap_report_deadline_ms = gap_state.deadline_ms;
			flush_pending_gap_report(video_receiver, now_ms, false);
		}

		video_receiver->frame_index_cur = frame_index;
		video_receiver->cur_frame_seen_last_unit = false;
		video_receiver->cur_frame_first_packet_ms = chiaki_time_now_monotonic_ms();

		// D2: Measure inter-frame cadence gap
		if (video_receiver->prev_frame_first_packet_ms > 0 &&
			video_receiver->cur_frame_first_packet_ms >= video_receiver->prev_frame_first_packet_ms)
		{
			uint64_t gap_ms = video_receiver->cur_frame_first_packet_ms -
				video_receiver->prev_frame_first_packet_ms;
			if (video_receiver->cadence_count == 0 || gap_ms < video_receiver->cadence_min_ms)
				video_receiver->cadence_min_ms = gap_ms;
			if (gap_ms > video_receiver->cadence_max_ms)
				video_receiver->cadence_max_ms = gap_ms;
			video_receiver->cadence_total_ms += gap_ms;
			video_receiver->cadence_count++;
		}
		video_receiver->prev_frame_first_packet_ms = video_receiver->cur_frame_first_packet_ms;

		chiaki_frame_processor_alloc_frame(&video_receiver->frame_processor, packet);
	}

	chiaki_frame_processor_put_unit(&video_receiver->frame_processor, packet);
	if(packet->units_in_frame_total > 0 &&
		packet->unit_index == packet->units_in_frame_total - 1)
		video_receiver->cur_frame_seen_last_unit = true;

	// if we are currently building up a frame
	if(video_receiver->frame_index_cur != video_receiver->frame_index_prev)
	{
		// Flush only when enough units are present (source + parity) to avoid
		// prematurely finalizing a frame at the "last unit" marker.
		if(chiaki_frame_processor_flush_possible(&video_receiver->frame_processor))
			chiaki_video_receiver_flush_frame(video_receiver);
	}
}

static ChiakiErrorCode chiaki_video_receiver_flush_frame(ChiakiVideoReceiver *video_receiver)
{
	uint8_t *frame;
	size_t frame_size;
	uint64_t flush_start_ms = chiaki_time_now_monotonic_ms();
	uint64_t assemble_ms = 0;
	if(video_receiver->cur_frame_first_packet_ms > 0 &&
		flush_start_ms >= video_receiver->cur_frame_first_packet_ms)
	{
		assemble_ms = flush_start_ms - video_receiver->cur_frame_first_packet_ms;
	}

	// CASCADE SKIP: after 3+ consecutive missing-ref failures, skip the
	// expensive flush+decode cycle. The frame will fail anyway (broken
	// reference chain). Still count as lost and maintain IDR pressure.
	if(video_receiver->consecutive_missing_ref >= CASCADE_SKIP_THRESHOLD)
	{
		video_receiver->frames_lost = saturating_add_u32(video_receiver->frames_lost, 1U);
		video_receiver->stage_window_drops++;
		video_receiver->cascade_skip_count++;

		// Always apply a local reset on cascade entry so we periodically return
		// to normal parse/decode and can catch incoming I-slices.
		video_receiver_apply_cascade_reset(video_receiver);

		uint64_t idr_now_ms = chiaki_time_now_monotonic_ms();
		video_receiver_maybe_request_idr(video_receiver, idr_now_ms, "cascade_skip");

		// Advance bookkeeping
		video_receiver->frame_index_prev = video_receiver->frame_index_cur;
		video_receiver->cur_frame_first_packet_ms = 0;
		video_receiver->cur_frame_seen_last_unit = false;

		// Report once for this abandoned generation before giving up on it.
		if(video_receiver->packet_stats)
			chiaki_frame_processor_report_frame_stats(&video_receiver->frame_processor, CHIAKI_FRAME_OUTCOME_ABANDONED, video_receiver->packet_stats);
		return CHIAKI_ERR_UNKNOWN;
	}

	ChiakiFrameProcessorFlushResult flush_result = chiaki_frame_processor_flush(&video_receiver->frame_processor, &frame, &frame_size);

	if(video_receiver->packet_stats)
	{
		ChiakiFrameProcessorFrameOutcome outcome;
		switch(flush_result)
		{
			case CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_SUCCESS:
				outcome = CHIAKI_FRAME_OUTCOME_DELIVERED;
				break;
			case CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_SUCCESS:
				outcome = CHIAKI_FRAME_OUTCOME_FEC_RECOVERED;
				break;
			case CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED:
				outcome = CHIAKI_FRAME_OUTCOME_FEC_FAILED;
				break;
			case CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FAILED:
			default:
				outcome = CHIAKI_FRAME_OUTCOME_ABANDONED;
				break;
		}
		chiaki_frame_processor_report_frame_stats(&video_receiver->frame_processor, outcome, video_receiver->packet_stats);
	}

	if(flush_result == CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FAILED
		|| flush_result == CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED)
	{
		video_receiver->stage_window_drops++;

		// Request IDR only for hard FEC failures to avoid over-driving keyframe requests.
		if(flush_result == CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED)
		{
			uint64_t idr_now_ms = chiaki_time_now_monotonic_ms();
			video_receiver_maybe_request_idr(video_receiver, idr_now_ms, "fec_failed");
		}

		if(flush_result == CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED)
		{
			chiaki_stream_connection_report_fec_fail(&video_receiver->session->stream_connection);
			ChiakiSeqNum16 next_frame_expected = (ChiakiSeqNum16)(video_receiver->frame_index_prev_complete + 1);
			// Reuse flush_start_ms (captured at function entry) instead of a fresh
			// clock read -- both represent "now" within this same flush call, and
			// avoiding a third chiaki_time_now_monotonic_ms() call here is a small
			// but free win on the latency-sensitive recv thread. This is a
			// routine re-report (not a retirement): the [next_frame_expected, ...]
			// start value stays pinned across repeated FEC failures for the same
			// stall, so the cooldown/growth-bypass policy applies normally here --
			// the retirement hook in the success branch below (search
			// "retiring_start") is what guarantees this range's tail still gets
			// reported once the stall ends and this start value is retired.
			report_corrupt_frame_range(video_receiver, next_frame_expected, (ChiakiSeqNum16)video_receiver->frame_index_cur, "fec_failed", flush_start_ms, false /* bypass_cooldown */);
			uint32_t lost = seq16_span(next_frame_expected, (ChiakiSeqNum16)video_receiver->frame_index_cur);
			if(lost > 0 && lost < 1000U)
				video_receiver->frames_lost = saturating_add_u32(video_receiver->frames_lost, lost);
			else
				CHIAKI_LOGW(video_receiver->log,
					"Ignoring suspicious frame-loss span %u (%d-%d)",
					(unsigned int)lost,
					(int)next_frame_expected,
					(int)video_receiver->frame_index_cur);
		}
		video_receiver->frame_index_prev = video_receiver->frame_index_cur;
		CHIAKI_LOGW(video_receiver->log, "Failed to complete frame %d", (int)video_receiver->frame_index_cur);
		return CHIAKI_ERR_UNKNOWN;
	}

	bool succ = flush_result != CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED;
	bool recovered = false;

	ChiakiBitstreamSlice slice;
	if(chiaki_bitstream_slice(&video_receiver->bitstream, frame, frame_size, &slice))
	{
		if(slice.slice_type == CHIAKI_BITSTREAM_SLICE_I)
		{
			video_receiver->consecutive_missing_ref = 0;
			video_receiver->cascade_reset_attempts = 0;
		}

		if(video_receiver->idr_request_pending)
		{
			if(slice.slice_type == CHIAKI_BITSTREAM_SLICE_I)
			{
				video_receiver->idr_request_pending = false;
				video_receiver->idr_request_start_ms = 0;
				video_receiver->consecutive_missing_ref = 0;
				CHIAKI_LOGI(video_receiver->log, "Received I-slice after IDR request, recovery complete");
			}
			else
			{
				video_receiver_maybe_request_idr(video_receiver, chiaki_time_now_monotonic_ms(), "pending_non_i");
			}
		}
		// P-frames fall through to the existing reference-recovery logic below.
		// Missing refs -> try alternate reference (recovered=true) or mark succ=false.
		// Either way the pipeline advances. Brief visual artifacts, not a blackout.

		if(slice.slice_type == CHIAKI_BITSTREAM_SLICE_P)
		{
			ChiakiSeqNum16 ref_frame_index = video_receiver->frame_index_cur - slice.reference_frame - 1;
			if(slice.reference_frame != 0xff && !have_ref_frame(video_receiver, ref_frame_index))
			{
				for(unsigned i = slice.reference_frame + 1; i < RECEIVER_REF_SLOTS; i++)
				{
					ChiakiSeqNum16 ref_frame_index_new = video_receiver->frame_index_cur - i - 1;
					if(have_ref_frame(video_receiver, ref_frame_index_new))
					{
						if(chiaki_bitstream_slice_set_reference_frame(&video_receiver->bitstream, frame, frame_size, i))
						{
							recovered = true;
							video_receiver->consecutive_missing_ref = 0;
							CHIAKI_LOGW(video_receiver->log, "Missing reference frame %d for decoding frame %d -> changed to %d", (int)ref_frame_index, (int)video_receiver->frame_index_cur, (int)ref_frame_index_new);
						}
						break;
					}
				}
				if(!recovered)
				{
					succ = false;
					video_receiver->frames_lost = saturating_add_u32(video_receiver->frames_lost, 1U);
					chiaki_stream_connection_report_missing_ref(&video_receiver->session->stream_connection);
					video_receiver->consecutive_missing_ref++;
					CHIAKI_LOGW(video_receiver->log, "Missing reference frame %d for decoding frame %d (cascade=%u)",
						(int)ref_frame_index, (int)video_receiver->frame_index_cur,
						video_receiver->consecutive_missing_ref);
					/* Request IDR immediately; cooldown (IDR_REQUEST_COOLDOWN_MS=100ms) prevents
					 * flooding. Cascade skip fires at consecutive_missing_ref >= CASCADE_SKIP_THRESHOLD
					 * as a backstop if the PS5 is slow to respond. */
					uint64_t idr_now_ms = chiaki_time_now_monotonic_ms();
					video_receiver_maybe_request_idr(video_receiver, idr_now_ms, "missing_ref");
				}
			}
		}
	}

	if(succ && video_receiver->session->video_sample_cb)
	{
		uint64_t submit_start_ms = chiaki_time_now_monotonic_ms();
		bool cb_succ = video_receiver->session->video_sample_cb(frame, frame_size, video_receiver->frames_lost, recovered, video_receiver->session->video_sample_cb_user);
		uint64_t submit_end_ms = chiaki_time_now_monotonic_ms();
		video_receiver->frames_lost = 0;
		if(!cb_succ)
		{
			succ = false;
			CHIAKI_LOGW(video_receiver->log, "Video callback did not process frame successfully.");
		}
		else
		{
			add_ref_frame(video_receiver, video_receiver->frame_index_cur);
			video_receiver->consecutive_missing_ref = 0;
			CHIAKI_LOGV(video_receiver->log, "Added reference %c frame %d", slice.slice_type == CHIAKI_BITSTREAM_SLICE_I ? 'I' : 'P', (int)video_receiver->frame_index_cur);
		}
		if(submit_end_ms >= submit_start_ms)
			video_receiver->stage_submit_total_ms += submit_end_ms - submit_start_ms;
	}

	video_receiver->frame_index_prev = video_receiver->frame_index_cur;
	video_receiver->cur_frame_first_packet_ms = 0;
	video_receiver->cur_frame_seen_last_unit = false;

	if(succ) {
		// The fec_failed report above (search "fec_failed") is keyed on a start
		// value of frame_index_prev_complete+1, which stays pinned for as long
		// as no frame completes -- so a cooldown-deferred tail from repeated
		// FEC failures during a stall has no later flush_pending_gap_report()-
		// style retry once frame_index_prev_complete advances past it below:
		// that start value is retired and never offered again. Flush its full
		// known extent unconditionally (bypass_cooldown=true) right before the
		// advance, so the tail isn't permanently dropped. This is a no-op
		// (OBSOLETE) whenever the range was already fully reported via the
		// gap-hold path or an explicit FEC-failure report -- both write the
		// same last_reported_corrupt_* state this check reads.
		ChiakiSeqNum16 retiring_start = (ChiakiSeqNum16)(video_receiver->frame_index_prev_complete + 1);
		ChiakiSeqNum16 completed_frame = (ChiakiSeqNum16)video_receiver->frame_index_cur;
		if(chiaki_seq_num_16_gt(completed_frame, retiring_start))
		{
			ChiakiSeqNum16 retiring_end = (ChiakiSeqNum16)(completed_frame - 1);
			report_corrupt_frame_range(video_receiver, retiring_start, retiring_end, "burst_retired", flush_start_ms, true /* bypass_cooldown */);
		}

		video_receiver->frame_index_prev_complete = video_receiver->frame_index_cur;
		video_receiver->stage_window_frames++;
		video_receiver->stage_assemble_total_ms += assemble_ms;
	}

	uint64_t now_ms = chiaki_time_now_monotonic_ms();
	if(video_receiver->stage_window_start_ms == 0)
		video_receiver->stage_window_start_ms = now_ms;
	if(now_ms - video_receiver->stage_window_start_ms >= 1000)
	{
		uint32_t frames = video_receiver->stage_window_frames;
		uint64_t avg_assemble_ms = frames > 0 ? video_receiver->stage_assemble_total_ms / frames : 0;
		uint64_t avg_submit_ms = frames > 0 ? video_receiver->stage_submit_total_ms / frames : 0;
		uint64_t cadence_avg_ms = video_receiver->cadence_count > 0 ?
			video_receiver->cadence_total_ms / video_receiver->cadence_count : 0;
		CHIAKI_LOGD(video_receiver->log,
			"PIPE/STAGE frames=%u drops=%u skips=%u old_rejects=%u avg_assemble_ms=%llu avg_submit_ms=%llu cadence_min=%llu cadence_max=%llu cadence_avg=%llu",
			frames,
			video_receiver->stage_window_drops,
			video_receiver->cascade_skip_count,
			video_receiver->old_frame_rejects_window,
			(unsigned long long)avg_assemble_ms,
			(unsigned long long)avg_submit_ms,
			(unsigned long long)video_receiver->cadence_min_ms,
			(unsigned long long)video_receiver->cadence_max_ms,
			(unsigned long long)cadence_avg_ms);

		// Cadence max alarm: detect PS5 encoder throttling
		if (video_receiver->cadence_max_ms > 80) {
			video_receiver->cadence_max_alarm_streak++;
			if (video_receiver->cadence_max_alarm_streak >= 3) {
				CHIAKI_LOGD(video_receiver->log,
					"PIPE/CADENCE_ALARM streak=%u cadence_max=%llu",
					video_receiver->cadence_max_alarm_streak,
					(unsigned long long)video_receiver->cadence_max_ms);
			}
		} else {
			video_receiver->cadence_max_alarm_streak = 0;
		}

		video_receiver->stage_window_start_ms = now_ms;
		video_receiver->stage_assemble_total_ms = 0;
		video_receiver->stage_submit_total_ms = 0;
		video_receiver->stage_window_frames = 0;
		video_receiver->stage_window_drops = 0;
		video_receiver->old_frame_rejects_window = 0;
		video_receiver->cascade_skip_count = 0;
		video_receiver->cadence_min_ms = 0;
		video_receiver->cadence_max_ms = 0;
		video_receiver->cadence_total_ms = 0;
		video_receiver->cadence_count = 0;
	}

	return CHIAKI_ERR_SUCCESS;
}
