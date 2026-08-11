// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/frameprocessor.h>
#include <chiaki/fec.h>
#include <chiaki/video.h>

#include <jerasure.h>

#include <string.h>
#include <assert.h>

#ifndef _WIN32
#include <arpa/inet.h>
#endif

CHIAKI_EXPORT void chiaki_stream_stats_reset(ChiakiStreamStats *stats)
{
	stats->frames = 0;
	stats->bytes = 0;
}

CHIAKI_EXPORT void chiaki_stream_stats_frame(ChiakiStreamStats *stats, uint64_t size)
{
	stats->frames++;
	stats->bytes += size;
	stats->frames_total++;
	stats->bytes_total += size;
	//float br = (float)chiaki_stream_stats_bitrate(stats, 60) / 1000000.0f;
	//CHIAKI_LOGD(NULL, "bitrate: %f", br);
}

CHIAKI_EXPORT uint64_t chiaki_stream_stats_bitrate(ChiakiStreamStats *stats, uint64_t framerate)
{
	if (stats->frames == 0)
		return 0;
	return (stats->bytes * 8 * framerate) / stats->frames;
}

#define UNIT_SLOTS_MAX 256

struct chiaki_frame_unit_t
{
	size_t data_size;
};

CHIAKI_EXPORT void chiaki_frame_processor_init(ChiakiFrameProcessor *frame_processor, ChiakiLog *log)
{
	frame_processor->log = log;
	frame_processor->frame_buf = NULL;
	frame_processor->frame_buf_size = 0;
	frame_processor->buf_size_per_unit = 0;
	frame_processor->buf_stride_per_unit = 0;
	frame_processor->units_source_expected = 0;
	frame_processor->units_fec_expected = 0;
	frame_processor->units_source_received = 0;
	frame_processor->units_fec_received = 0;
	frame_processor->unit_slots = NULL;
	frame_processor->unit_slots_size = 0;
	frame_processor->flushed = true;
	frame_processor->stats_reported = true; // virgin processor: nothing to report
	chiaki_stream_stats_reset(&frame_processor->stream_stats);
	/* CHIAKI_NEW uses plain malloc (not calloc), so explicitly zero the monotonic
	 * counters that chiaki_stream_stats_reset() intentionally leaves untouched. */
	frame_processor->stream_stats.frames_total = 0;
	frame_processor->stream_stats.bytes_total = 0;
}

CHIAKI_EXPORT void chiaki_frame_processor_fini(ChiakiFrameProcessor *frame_processor)
{
	free(frame_processor->frame_buf);
	free(frame_processor->unit_slots);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_frame_processor_alloc_frame(ChiakiFrameProcessor *frame_processor, ChiakiTakionAVPacket *packet)
{
	if(packet->units_in_frame_total < packet->units_in_frame_fec)
	{
		CHIAKI_LOGE(frame_processor->log, "Packet has units_in_frame_total < units_in_frame_fec");
		return CHIAKI_ERR_INVALID_DATA;
	}

	frame_processor->flushed = false;
	frame_processor->stats_reported = false;
	frame_processor->units_source_expected = packet->units_in_frame_total - packet->units_in_frame_fec;
	frame_processor->units_fec_expected = packet->units_in_frame_fec;
	if(frame_processor->units_fec_expected < 1)
		frame_processor->units_fec_expected = 1;

	// Reset received counters here, before any early return below, so a
	// mid-allocation failure (malformed packet) never leaves stale counts from
	// the previous frame in place. That keeps units_source_received <=
	// units_source_expected an unconditional invariant -- the stats reporter
	// relies on it, and a stale received count could otherwise underflow
	// expected - actual into a huge bogus loss value.
	frame_processor->units_source_received = 0;
	frame_processor->units_fec_received = 0;

	frame_processor->buf_size_per_unit = packet->data_size;
	if(packet->is_video && packet->unit_index < frame_processor->units_source_expected)
	{
		if(packet->data_size < 2)
		{
			CHIAKI_LOGE(frame_processor->log, "Packet too small to read buf size extension");
			return CHIAKI_ERR_BUF_TOO_SMALL;
		}
		frame_processor->buf_size_per_unit += ntohs(((chiaki_unaligned_uint16_t *)packet->data)[0]);
	}
	frame_processor->buf_stride_per_unit = ((frame_processor->buf_size_per_unit + 0xf) / 0x10) * 0x10;

	if(frame_processor->buf_size_per_unit == 0)
	{
		CHIAKI_LOGE(frame_processor->log, "Frame Processor doesn't handle empty units");
		return CHIAKI_ERR_BUF_TOO_SMALL;
	}

	size_t unit_slots_size_required = frame_processor->units_source_expected + frame_processor->units_fec_expected;
	if(unit_slots_size_required > UNIT_SLOTS_MAX)
	{
		CHIAKI_LOGE(frame_processor->log, "Packet suggests more than %u unit slots", UNIT_SLOTS_MAX);
		return CHIAKI_ERR_INVALID_DATA;
	}
	if(unit_slots_size_required != frame_processor->unit_slots_size)
	{
		void *new_ptr = NULL;
		if(frame_processor->unit_slots)
		{
			new_ptr = realloc(frame_processor->unit_slots, unit_slots_size_required * sizeof(ChiakiFrameUnit));
			if(!new_ptr)
				free(frame_processor->unit_slots);
		}
		else
			new_ptr = malloc(unit_slots_size_required * sizeof(ChiakiFrameUnit));

		frame_processor->unit_slots = new_ptr;
		if(!new_ptr)
		{
			frame_processor->unit_slots_size = 0;
			return CHIAKI_ERR_MEMORY;
		}
		else
			frame_processor->unit_slots_size = unit_slots_size_required;
	}
	memset(frame_processor->unit_slots, 0, frame_processor->unit_slots_size * sizeof(ChiakiFrameUnit));

	if(frame_processor->unit_slots_size > SIZE_MAX / frame_processor->buf_stride_per_unit)
		return CHIAKI_ERR_OVERFLOW;
	size_t frame_buf_size_required = frame_processor->unit_slots_size * frame_processor->buf_stride_per_unit;
	if(frame_processor->frame_buf_size < frame_buf_size_required)
	{
		free(frame_processor->frame_buf);
		/* PIPE/FRAMEBUF_REALLOC: program-lifetime counter; even across reconnects, growth should stay n=1 */
		{
			static uint32_t framebuf_realloc_n = 0;
			framebuf_realloc_n++;
			CHIAKI_LOGD(frame_processor->log,
				"PIPE/FRAMEBUF_REALLOC n=%u size=%lu",
				framebuf_realloc_n, (unsigned long)frame_buf_size_required);
		}
		frame_processor->frame_buf = malloc(frame_buf_size_required + CHIAKI_VIDEO_BUFFER_PADDING_SIZE);
		if(!frame_processor->frame_buf)
		{
			frame_processor->frame_buf_size = 0;
			return CHIAKI_ERR_MEMORY;
		}
		frame_processor->frame_buf_size = frame_buf_size_required;
	}
	memset(frame_processor->frame_buf, 0, frame_buf_size_required + CHIAKI_VIDEO_BUFFER_PADDING_SIZE);

	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_frame_processor_put_unit(ChiakiFrameProcessor *frame_processor, ChiakiTakionAVPacket *packet)
{
	if(packet->unit_index >= frame_processor->unit_slots_size)
	{
		CHIAKI_LOGE(frame_processor->log, "Packet's unit index is too high");
		return CHIAKI_ERR_INVALID_DATA;
	}

	if(!packet->data_size)
	{
		CHIAKI_LOGW(frame_processor->log, "Unit is empty");
		return CHIAKI_ERR_INVALID_DATA;
	}

	if(packet->data_size > frame_processor->buf_size_per_unit)
	{
		CHIAKI_LOGW(frame_processor->log, "Unit is bigger than pre-calculated size!");
		return CHIAKI_ERR_INVALID_DATA;
	}

	ChiakiFrameUnit *unit = frame_processor->unit_slots + packet->unit_index;
	if(unit->data_size)
	{
		// Duplicates are expected on lossy/reordered UDP paths after retransmit.
		// Accept only identical duplicates to avoid masking corrupted payloads.
		if(unit->data_size != packet->data_size)
		{
			CHIAKI_LOGE(frame_processor->log, "Conflicting duplicate unit size");
			return CHIAKI_ERR_INVALID_DATA;
		}
		if(!frame_processor->flushed)
		{
			uint8_t *existing = frame_processor->frame_buf + packet->unit_index * frame_processor->buf_stride_per_unit;
			if(memcmp(existing, packet->data, packet->data_size) != 0)
			{
				CHIAKI_LOGE(frame_processor->log, "Conflicting duplicate unit payload");
				return CHIAKI_ERR_INVALID_DATA;
			}
		}
		// When flushed==true, frame_buf may already be compacted and no longer
		// mapped 1:1 to per-unit slots. In that state we can't safely compare
		// payload bytes here, so we keep first-arrival data-size validation only.
		CHIAKI_LOGW(frame_processor->log, "Received duplicate unit");
		return CHIAKI_ERR_SUCCESS;
	} else {
		unit->data_size = packet->data_size;
	}

	if(!frame_processor->flushed)
	{
		memcpy(frame_processor->frame_buf + packet->unit_index * frame_processor->buf_stride_per_unit,
				packet->data,
				packet->data_size);
	}

	if(packet->unit_index < frame_processor->units_source_expected)
		frame_processor->units_source_received++;
	else
		frame_processor->units_fec_received++;

	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT void chiaki_frame_processor_report_frame_stats(ChiakiFrameProcessor *frame_processor,
	ChiakiFrameProcessorFrameOutcome outcome, ChiakiPacketStats *packet_stats)
{
	// Report at most once per frame: alloc_frame() clears this guard, and every
	// call site (cascade-skip, post-flush) reports exactly once per frame.
	if(frame_processor->stats_reported)
		return;
	frame_processor->stats_reported = true;

	// Mirrors flush()'s own early-out: an unallocated/reset processor has nothing
	// meaningful to report.
	if(frame_processor->units_source_expected == 0)
		return;

	// Source units only. FEC parity units are deliberately excluded from this
	// accounting: chiaki_frame_processor_flush_possible() gates a flush on
	// units_source_received + units_fec_received >= units_source_expected, so a
	// flush commonly fires the moment the last SOURCE unit lands, before any
	// parity for this frame has necessarily arrived at all. Whether parity
	// happened to show up by flush time is an artifact of arrival timing, not a
	// signal of real loss or real recovery -- folding it into "expected" would
	// report a near-constant phantom count on every frame regardless of actual
	// network health. Source counts don't have this problem: units_source_received
	// <= units_source_expected always holds (put_unit() increments a given unit
	// index at most once, so duplicates can't inflate it), and both counts are
	// fully settled by the time this function runs.
	uint64_t expected = frame_processor->units_source_expected;
	uint64_t actual = frame_processor->units_source_received;

	// GH #221 diagnostic A/B: PS5 may cross-check the `received` value we report
	// against its own sent source+parity count; under-reporting (source-only)
	// would then look like phantom loss and could be driving zero-loss bitrate
	// step-downs. When enabled, fold units_fec_received (parity units that
	// physically arrived, regardless of whether they were needed for recovery)
	// into `received` only. `lost`/`expected` must NOT change here -- see the
	// block comment above for why folding parity into those specifically would
	// reintroduce the zero-loss-under-real-loss bug PR #213 fixed.
	//
	// CAP CONFOUND: this toggle is NOT loss-neutral once congestioncontrol.c's
	// 10% cap engages. congestioncontrol.c's reported_lost cap computes
	// reported_lost = received * CONGESTION_MAX_REPORTED_LOSS / (1 -
	// CONGESTION_MAX_REPORTED_LOSS) (congestioncontrol.c:53-55) -- a larger
	// `received` widens that absolute ceiling, so on cap-bound ticks the ON arm
	// sends a strictly larger absolute lost count than the OFF arm at the exact
	// same true loss. A/B analysis of hardware logs from this toggle must
	// therefore compare the `CONGESTION/LOSS reported_precap=.../reported=...`
	// aggregate fields (congestioncontrol.c's 1Hz summary) across both arms,
	// not just downstream bitrate outcomes -- the cap-confound means outcome
	// differences alone can't distinguish "parity-inclusive fixed the phantom
	// loss" from "parity-inclusive just changed how hard the cap bites".
#ifdef VITARPS5_CONGESTION_PARITY_INCLUSIVE_RECEIVED
	uint64_t received_bonus = frame_processor->units_fec_received;
#else
	uint64_t received_bonus = 0;
#endif

	if(outcome == CHIAKI_FRAME_OUTCOME_FEC_RECOVERED)
	{
		// FEC decode only ever runs when units_source_received < units_source_expected
		// at flush time, and this outcome means it succeeded: the frame was fully
		// delivered, so the source shortfall is genuine FEC recovery, not raw loss.
		chiaki_packet_stats_push_generation(packet_stats, expected + received_bonus, 0, expected - actual);
	}
	else
	{
		// DELIVERED: FEC was never invoked, so actual == expected and this reports
		// (expected, 0, 0) -- a full, lossless generation.
		// FEC_FAILED: FEC ran and could not recover; the source shortfall is real
		// loss the user experienced, reported as raw lost.
		// ABANDONED: cascade-skipped frames are dropped without ever reaching
		// chiaki_frame_processor_flush(), so `actual` reflects whatever source
		// units happened to have arrived before the skip -- usually all of them,
		// since cascades stem from reference-chain damage on EARLIER frames, not
		// loss on this one. Any genuine shortfall here is still reported as raw
		// loss, same as FEC_FAILED; nothing in this branch inflates it beyond
		// what actually failed to arrive.
		chiaki_packet_stats_push_generation(packet_stats, actual + received_bonus, expected - actual, 0);
	}
}

static ChiakiErrorCode chiaki_frame_processor_fec(ChiakiFrameProcessor *frame_processor)
{
	CHIAKI_LOGI(frame_processor->log, "Frame Processor received %u+%u / %u+%u units, attempting FEC",
				frame_processor->units_source_received, frame_processor->units_fec_received,
				frame_processor->units_source_expected, frame_processor->units_fec_expected);


	size_t erasures_count = (frame_processor->units_source_expected + frame_processor->units_fec_expected)
			- (frame_processor->units_source_received + frame_processor->units_fec_received);
	unsigned int *erasures = calloc(erasures_count, sizeof(unsigned int));
	if(!erasures)
		return CHIAKI_ERR_MEMORY;

	size_t erasure_index = 0;
	for(size_t i=0; i<frame_processor->units_source_expected + frame_processor->units_fec_expected; i++)
	{
		ChiakiFrameUnit *slot = frame_processor->unit_slots + i;
		if(!slot->data_size)
		{
			if(erasure_index >= erasures_count)
			{
				// should never happen by design, but too scary not to check
				assert(false);
				free(erasures);
				return CHIAKI_ERR_UNKNOWN;
			}
			erasures[erasure_index++] = (unsigned int)i;
		}
	}
	assert(erasure_index == erasures_count);

	ChiakiErrorCode err = chiaki_fec_decode(frame_processor->frame_buf,
			frame_processor->buf_size_per_unit, frame_processor->buf_stride_per_unit,
			frame_processor->units_source_expected, frame_processor->units_fec_expected,
			erasures, erasures_count);

	if(err != CHIAKI_ERR_SUCCESS)
	{
		err = CHIAKI_ERR_FEC_FAILED;
		CHIAKI_LOGE(frame_processor->log, "FEC failed");
	}
	else
	{
		err = CHIAKI_ERR_SUCCESS;
		CHIAKI_LOGI(frame_processor->log, "FEC successful");

		// restore unit sizes
		for(size_t i=0; i<frame_processor->units_source_expected; i++)
		{
			ChiakiFrameUnit *slot = frame_processor->unit_slots + i;
			uint8_t *buf_ptr = frame_processor->frame_buf + frame_processor->buf_stride_per_unit * i;
			uint16_t padding = ntohs(*((chiaki_unaligned_uint16_t *)buf_ptr));
			if(padding >= frame_processor->buf_size_per_unit)
			{
				CHIAKI_LOGE(frame_processor->log, "Padding in unit (%#x) is larger or equals to the whole unit size (%#llx)",
							(unsigned int)padding, frame_processor->buf_size_per_unit);
				chiaki_log_hexdump(frame_processor->log, CHIAKI_LOG_DEBUG, buf_ptr, 0x50);
				continue;
			}
			slot->data_size = frame_processor->buf_size_per_unit - padding;
		}
	}

	free(erasures);
	return err;
}

CHIAKI_EXPORT ChiakiFrameProcessorFlushResult chiaki_frame_processor_flush(ChiakiFrameProcessor *frame_processor, uint8_t **frame, size_t *frame_size)
{
	if(frame_processor->units_source_expected == 0 || frame_processor->flushed)
		return CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FAILED;

	//CHIAKI_LOGD(NULL, "source: %u, fec: %u",
	//		frame_processor->units_source_expected,
	//		frame_processor->units_fec_expected);

	ChiakiFrameProcessorFlushResult result = CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_SUCCESS;
	if(frame_processor->units_source_received < frame_processor->units_source_expected)
	{
		ChiakiErrorCode err = chiaki_frame_processor_fec(frame_processor);
		if(err == CHIAKI_ERR_SUCCESS)
			result = CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_SUCCESS;
		else
			result = CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED;
	}

	size_t cur = 0;
	for(size_t i=0; i<frame_processor->units_source_expected; i++)
	{
		ChiakiFrameUnit *unit = frame_processor->unit_slots + i;
		if(!unit->data_size)
		{
			CHIAKI_LOGW(frame_processor->log, "Missing unit %#llx", (unsigned long long)i);
			continue;
		}
		if(unit->data_size < 2)
		{
			CHIAKI_LOGE(frame_processor->log, "Saved unit has size < 2");
			chiaki_log_hexdump(frame_processor->log, CHIAKI_LOG_VERBOSE, frame_processor->frame_buf + i*frame_processor->buf_size_per_unit, 0x50);
			continue;
		}
		size_t part_size = unit->data_size - 2;
		uint8_t *buf_ptr = frame_processor->frame_buf + i*frame_processor->buf_stride_per_unit;
		uint8_t *dst_ptr = frame_processor->frame_buf + cur;
		uint8_t *src_ptr = buf_ptr + 2;
		// Frame assembly compacts unit payloads in-place, so overlap is expected.
		memmove(dst_ptr, src_ptr, part_size);
		cur += part_size;
	}

	chiaki_stream_stats_frame(&frame_processor->stream_stats, (uint64_t)cur);

	*frame = frame_processor->frame_buf;
	*frame_size = cur;
	frame_processor->flushed = true;
	return result;
}
