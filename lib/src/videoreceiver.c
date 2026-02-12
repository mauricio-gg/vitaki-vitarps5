// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/videoreceiver.h>
#include <chiaki/session.h>
#include <chiaki/time.h>

#include <string.h>

static ChiakiErrorCode chiaki_video_receiver_flush_frame(ChiakiVideoReceiver *video_receiver);

#define VIDEO_GAP_REPORT_HOLD_MS 12
#define VIDEO_GAP_REPORT_FORCE_SPAN 6

static void add_ref_frame(ChiakiVideoReceiver *video_receiver, int32_t frame)
{
	if(video_receiver->reference_frames[0] != -1)
	{
		memmove(&video_receiver->reference_frames[1], &video_receiver->reference_frames[0], sizeof(int32_t) * 15);
		video_receiver->reference_frames[0] = frame;
		return;
	}
	for(int i=15; i>=0; i--)
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
	for(int i=0; i<16; i++)
		if(video_receiver->reference_frames[i] == frame)
			return true;
	return false;
}

static bool seq16_inclusive_ge(ChiakiSeqNum16 a, ChiakiSeqNum16 b)
{
	return a == b || chiaki_seq_num_16_gt(a, b);
}

static uint16_t seq16_span(ChiakiSeqNum16 start, ChiakiSeqNum16 end)
{
	return (uint16_t)(end - start + 1);
}

static bool should_skip_corrupt_report(ChiakiVideoReceiver *video_receiver, ChiakiSeqNum16 start, ChiakiSeqNum16 end)
{
	if(video_receiver->last_reported_corrupt_start != start)
		return false;
	return seq16_inclusive_ge(video_receiver->last_reported_corrupt_end, end);
}

static void report_corrupt_frame_range(ChiakiVideoReceiver *video_receiver, ChiakiSeqNum16 start, ChiakiSeqNum16 end, const char *reason)
{
	if(should_skip_corrupt_report(video_receiver, start, end))
		return;

	CHIAKI_LOGW(video_receiver->log, "Detected missing or corrupt frame(s) from %d to %d%s%s",
			(int)start,
			(int)end,
			reason ? " reason=" : "",
			reason ? reason : "");
	stream_connection_send_corrupt_frame(&video_receiver->session->stream_connection, start, end);
	video_receiver->last_reported_corrupt_start = start;
	video_receiver->last_reported_corrupt_end = end;
}

static void flush_pending_gap_report(ChiakiVideoReceiver *video_receiver, uint64_t now_ms, bool force)
{
	if(!video_receiver->gap_report_pending)
		return;

	uint16_t span = seq16_span((ChiakiSeqNum16)video_receiver->gap_report_start,
		(ChiakiSeqNum16)video_receiver->gap_report_end);
	if(!force && now_ms < video_receiver->gap_report_deadline_ms &&
		span < VIDEO_GAP_REPORT_FORCE_SPAN)
		return;

	report_corrupt_frame_range(video_receiver,
		(ChiakiSeqNum16)video_receiver->gap_report_start,
		(ChiakiSeqNum16)video_receiver->gap_report_end,
		force ? "forced" : "held");
	video_receiver->gap_report_pending = false;
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
	video_receiver->cur_frame_seen_last_unit = false;
	video_receiver->cur_frame_first_packet_ms = 0;
	video_receiver->stage_window_start_ms = 0;
	video_receiver->stage_assemble_total_ms = 0;
	video_receiver->stage_submit_total_ms = 0;
	video_receiver->stage_window_frames = 0;
	video_receiver->stage_window_drops = 0;
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
	uint64_t now_ms = chiaki_time_now_monotonic_ms();
	flush_pending_gap_report(video_receiver, now_ms, false);

	// old frame?
	ChiakiSeqNum16 frame_index = packet->frame_index;
	if(video_receiver->frame_index_cur >= 0
		&& chiaki_seq_num_16_lt(frame_index, (ChiakiSeqNum16)video_receiver->frame_index_cur))
	{
		CHIAKI_LOGW(video_receiver->log, "Video Receiver received old frame packet");
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
		if(video_receiver->packet_stats)
			chiaki_frame_processor_report_packet_stats(&video_receiver->frame_processor, video_receiver->packet_stats);

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
			if(!video_receiver->gap_report_pending ||
				video_receiver->gap_report_start != next_frame_expected)
			{
				video_receiver->gap_report_pending = true;
				video_receiver->gap_report_start = next_frame_expected;
				video_receiver->gap_report_end = gap_end;
				video_receiver->gap_report_deadline_ms = now_ms + VIDEO_GAP_REPORT_HOLD_MS;
			}
			else if(chiaki_seq_num_16_gt(gap_end, (ChiakiSeqNum16)video_receiver->gap_report_end))
			{
				video_receiver->gap_report_end = gap_end;
			}
			flush_pending_gap_report(video_receiver, now_ms, false);
		}

		video_receiver->frame_index_cur = frame_index;
		video_receiver->cur_frame_seen_last_unit = false;
		video_receiver->cur_frame_first_packet_ms = chiaki_time_now_monotonic_ms();
		chiaki_frame_processor_alloc_frame(&video_receiver->frame_processor, packet);
	}

	chiaki_frame_processor_put_unit(&video_receiver->frame_processor, packet);
	if(packet->unit_index == packet->units_in_frame_total - 1)
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
	ChiakiFrameProcessorFlushResult flush_result = chiaki_frame_processor_flush(&video_receiver->frame_processor, &frame, &frame_size);

	if(flush_result == CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FAILED
		|| flush_result == CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED)
	{
		video_receiver->stage_window_drops++;
			if (flush_result == CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED)
			{
				chiaki_stream_connection_report_fec_fail(&video_receiver->session->stream_connection);
				ChiakiSeqNum16 next_frame_expected = (ChiakiSeqNum16)(video_receiver->frame_index_prev_complete + 1);
				report_corrupt_frame_range(video_receiver, next_frame_expected, (ChiakiSeqNum16)video_receiver->frame_index_cur, "fec_failed");
				video_receiver->frames_lost += video_receiver->frame_index_cur - next_frame_expected + 1;
				video_receiver->frame_index_prev = video_receiver->frame_index_cur;
			}
		CHIAKI_LOGW(video_receiver->log, "Failed to complete frame %d", (int)video_receiver->frame_index_cur);
		return CHIAKI_ERR_UNKNOWN;
	}

	bool succ = flush_result != CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED;
	bool recovered = false;

	ChiakiBitstreamSlice slice;
	if(chiaki_bitstream_slice(&video_receiver->bitstream, frame, frame_size, &slice))
	{
		if(slice.slice_type == CHIAKI_BITSTREAM_SLICE_P)
		{
			ChiakiSeqNum16 ref_frame_index = video_receiver->frame_index_cur - slice.reference_frame - 1;
			if(slice.reference_frame != 0xff && !have_ref_frame(video_receiver, ref_frame_index))
			{
				for(unsigned i=slice.reference_frame+1; i<16; i++)
				{
					ChiakiSeqNum16 ref_frame_index_new = video_receiver->frame_index_cur - i - 1;
					if(have_ref_frame(video_receiver, ref_frame_index_new))
					{
						if(chiaki_bitstream_slice_set_reference_frame(&video_receiver->bitstream, frame, frame_size, i))
						{
							recovered = true;
							CHIAKI_LOGW(video_receiver->log, "Missing reference frame %d for decoding frame %d -> changed to %d", (int)ref_frame_index, (int)video_receiver->frame_index_cur, (int)ref_frame_index_new);
						}
						break;
					}
				}
				if(!recovered)
				{
					succ = false;
					video_receiver->frames_lost++;
					chiaki_stream_connection_report_missing_ref(&video_receiver->session->stream_connection);
					CHIAKI_LOGW(video_receiver->log, "Missing reference frame %d for decoding frame %d", (int)ref_frame_index, (int)video_receiver->frame_index_cur);
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
			CHIAKI_LOGV(video_receiver->log, "Added reference %c frame %d", slice.slice_type == CHIAKI_BITSTREAM_SLICE_I ? 'I' : 'P', (int)video_receiver->frame_index_cur);
		}
		if(submit_end_ms >= submit_start_ms)
			video_receiver->stage_submit_total_ms += submit_end_ms - submit_start_ms;
	}

	video_receiver->frame_index_prev = video_receiver->frame_index_cur;
	video_receiver->cur_frame_first_packet_ms = 0;
	video_receiver->cur_frame_seen_last_unit = false;

	if(succ) {
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
		CHIAKI_LOGD(video_receiver->log,
			"PIPE/STAGE frames=%u drops=%u avg_assemble_ms=%llu avg_submit_ms=%llu",
			frames,
			video_receiver->stage_window_drops,
			(unsigned long long)avg_assemble_ms,
			(unsigned long long)avg_submit_ms);
		video_receiver->stage_window_start_ms = now_ms;
		video_receiver->stage_assemble_total_ms = 0;
		video_receiver->stage_submit_total_ms = 0;
		video_receiver->stage_window_frames = 0;
		video_receiver->stage_window_drops = 0;
	}

	return CHIAKI_ERR_SUCCESS;
}
