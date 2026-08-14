// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_VIDEORECEIVER_H
#define CHIAKI_VIDEORECEIVER_H

#include "common.h"
#include "log.h"
#include "video.h"
#include "takion.h"
#include "frameprocessor.h"
#include "bitstream.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CHIAKI_VIDEO_PROFILES_MAX 8

// Must match REF_FRAMES in vita/include/video.h (Vita HW H.264 decoder DPB size).
// Receiver window > HW decoder DPB causes silent macroblocking: the receiver
// approves P-frames referencing evicted decoder slots. Keep these in sync.
#define CHIAKI_VIDEO_RECEIVER_REF_SLOTS 8

// Number of inter-frame-gap histogram buckets. Edge values (and the rationale
// for them) live with the sampling code in lib/src/videoreceiver_gap.h -- only
// the count needs to be here, to size gap_hist[] below.
#define CHIAKI_VIDEO_GAP_HIST_BUCKETS 9

typedef struct chiaki_video_receiver_t
{
	struct chiaki_session_t *session;
	ChiakiLog *log;
	ChiakiVideoProfile profiles[CHIAKI_VIDEO_PROFILES_MAX];
	size_t profiles_count;
	int profile_cur; // < 1 if no profile selected yet, else index in profiles

	int32_t frame_index_cur; // frame that is currently being filled
	int32_t frame_index_prev; // last frame that has been at least partially decoded
	int32_t frame_index_prev_complete; // last frame that has been completely decoded
	ChiakiFrameProcessor frame_processor;
	ChiakiPacketStats *packet_stats;

	int32_t frames_lost;
	int32_t reference_frames[CHIAKI_VIDEO_RECEIVER_REF_SLOTS];
	ChiakiBitstream bitstream;
	bool gap_report_pending;
	uint16_t gap_report_start;
	uint16_t gap_report_end;
	uint64_t gap_report_deadline_ms;
	uint64_t last_reported_corrupt_at_ms; // Timestamp of last emitted corrupt-frame report, for re-report cooldown
	uint16_t last_reported_corrupt_start;
	uint16_t last_reported_corrupt_end;
	bool cur_frame_seen_last_unit;
	uint64_t cur_frame_first_packet_ms;
	uint64_t stage_window_start_ms;
	uint64_t stage_assemble_total_ms;
	uint64_t stage_submit_total_ms;
	uint32_t stage_window_frames;
	uint32_t stage_window_drops;
	uint32_t fec_fail_kf_count;      // FEC-failed frames whose expected unit count looked keyframe-sized (GH #251 discriminator, reset each stage window)
	uint32_t drift_submit_streak;       // Consecutive drift-submitted frames since the last I-slice (GH #251 log-suppression gate: only the first of a streak logs PIPE/DRIFT_SUBMIT)
	uint32_t drift_submit_window_count; // Drift-submitted frames this stage window (GH #251 diagnostic, reset each stage window)
	uint32_t units_expected_ewma_x8; // EWMA of frame_processor.units_source_expected, fixed-point x8 (alpha=1/8, see VIDEO_IDR_UNITS_EWMA_SHIFT)
	bool idr_request_pending;            // IDR requested, tracks state (never blocks decode)
	uint64_t idr_request_start_ms;       // Timestamp for timeout detection
	uint32_t old_frame_rejects_window;   // Phase 1: count late-packet rejections per 1s window
	uint64_t last_idr_request_ms;        // Phase 2: cooldown to prevent IDR flooding
	uint32_t consecutive_missing_ref;    // Consecutive unrecovered missing-ref P-frames
	uint32_t cascade_skip_count;         // Frames skipped during cascade (per 1s window, diagnostic only)
	uint32_t cascade_reset_attempts;     // Local decode-chain resets while recovering from cascade

	// --- Diagnostic instrumentation (D2: Frame Cadence Jitter) ---
	uint64_t prev_frame_first_packet_ms;  // Previous frame's first-packet timestamp
	uint64_t cadence_min_ms;              // Min inter-frame gap in current window
	uint64_t cadence_max_ms;              // Max inter-frame gap in current window
	uint64_t cadence_total_ms;            // Sum of inter-frame gaps in current window
	uint32_t cadence_count;               // Number of gaps measured in current window
	uint32_t cadence_max_alarm_streak;    // Consecutive windows with cadence_max > 80ms

	// --- Diagnostic instrumentation (D5: delivery-pattern probe, GH #251) ---
	// Same threading contract as the D2 fields above: written ONLY from the
	// Takion recv thread -- at the existing frame-boundary sample site in
	// chiaki_video_receiver_av_packet() and inside
	// chiaki_video_receiver_flush_frame() -- and read only by the 1s window
	// emit at the end of flush_frame(). No render/UI thread touches them, so
	// no locking is needed.
	//
	// LOAD-BEARING: none of this adds a chiaki_time_now_monotonic_*() call to
	// the per-packet path. Every timestamp reused here
	// (cur_frame_first_packet_ms, flush_start_ms) is one the frame-boundary /
	// flush path already reads. See the forbidding comment on
	// ChiakiTakion::video_jitter_stats (chiaki/takion.h:263-281) before adding
	// anything here that reads a clock.

	// D5-A: inter-frame gap histogram -- distribution SHAPE, which min/max/avg
	// over ~31 samples/window cannot express.
	uint32_t gap_hist[CHIAKI_VIDEO_GAP_HIST_BUCKETS];

	// D5-B: cumulative expected-vs-actual frame arrival drift.
	// Stored SCALED by fps (units: microseconds * fps) so the per-frame path
	// needs multiplies only -- the single divide happens once per window at
	// emit. This is exact (no per-frame truncation): the naive
	// frame_period_us = 1000000/30 = 33333 form loses 0.33us/frame, i.e. 10us/s
	// of pure quantisation error that would swamp the signal within a minute.
	// Range check: 4h elapsed (1.44e10 us) * 120 fps = 1.7e12, far inside int64.
	uint64_t drift_stream_start_ms;      // arrival that (re)based the drift series
	uint64_t drift_cum_frames;           // sum of forward frame-index deltas since the base
	uint64_t drift_prev_arrival_ms;      // previous frame-boundary arrival (discontinuity guard)
	int64_t drift_last_scaled;           // most recent drift sample; + = arriving LATE
	int64_t drift_win_base_scaled;       // drift at the start of the current 1s window
	int64_t drift_win_min_scaled;        // min drift sample this window
	int64_t drift_win_max_scaled;        // max drift sample this window
	uint32_t drift_fps;                  // negotiated max_fps, clamped; the scale factor above
	uint32_t drift_skipped_frames;       // frame indices skipped (delta>1) this window -- so
	                                     // LOST frames are never misread as drift
	uint32_t drift_resync_count;         // discontinuity re-bases this window
	uint16_t drift_prev_frame_index;
	bool drift_has_prev;

	// D5-C: gap attributed to the PREVIOUS completed frame's encoded byte size.
	// Direct test for console-side byte pacing: if a VBR encoder's output is
	// pushed through a constant-byte-rate pacer, a big frame delays the NEXT
	// frame's first packet.
	uint64_t sizegap_small_gap_total_ms;
	uint64_t sizegap_large_gap_total_ms;
	uint64_t frame_bytes_total;
	uint32_t sizegap_small_count;
	uint32_t sizegap_large_count;
	uint32_t sizegap_threshold_bytes;    // "large" cut = PREVIOUS window's mean frame size
	uint32_t prev_completed_frame_bytes; // set at flush success; CONSUMED (zeroed) at next
	                                     // frame boundary, which is what keeps a dropped
	                                     // frame from re-attributing a stale size
	uint32_t frame_bytes_count;
	uint32_t frame_bytes_min;            // 0 = no sample yet this window
	uint32_t frame_bytes_max;
} ChiakiVideoReceiver;

CHIAKI_EXPORT void chiaki_video_receiver_init(ChiakiVideoReceiver *video_receiver, struct chiaki_session_t *session, ChiakiPacketStats *packet_stats);
CHIAKI_EXPORT void chiaki_video_receiver_fini(ChiakiVideoReceiver *video_receiver);

/**
 * Called after receiving the Stream Info Packet.
 *
 * @param video_receiver
 * @param profiles Array of profiles. Ownership of the contained header buffers will be transferred to the ChiakiVideoReceiver!
 * @param profiles_count must be <= CHIAKI_VIDEO_PROFILES_MAX
 */
CHIAKI_EXPORT void chiaki_video_receiver_stream_info(ChiakiVideoReceiver *video_receiver, ChiakiVideoProfile *profiles, size_t profiles_count);

CHIAKI_EXPORT void chiaki_video_receiver_av_packet(ChiakiVideoReceiver *video_receiver, ChiakiTakionAVPacket *packet);

static inline ChiakiVideoReceiver *chiaki_video_receiver_new(struct chiaki_session_t *session, ChiakiPacketStats *packet_stats)
{
	ChiakiVideoReceiver *video_receiver = CHIAKI_NEW(ChiakiVideoReceiver);
	if(!video_receiver)
		return NULL;
	chiaki_video_receiver_init(video_receiver, session, packet_stats);
	return video_receiver;
}

static inline void chiaki_video_receiver_free(ChiakiVideoReceiver *video_receiver)
{
	if(!video_receiver)
		return;
	chiaki_video_receiver_fini(video_receiver);
	free(video_receiver);
}

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_VIDEORECEIVER_H
