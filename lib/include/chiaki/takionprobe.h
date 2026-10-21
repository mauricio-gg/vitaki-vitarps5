// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

/*
 * Receive-thread time probes (#413, docs/design/streaming.md sections 9.3 to 9.8): the types and
 * the pure arithmetic behind PIPE/RECVTIME, PIPE/SOCKQ and PIPE/RECVTHREAD. Header-only and free
 * of SDK dependencies so the arithmetic compiles and runs natively (test/takion_probe_tests.c).
 */

#ifndef CHIAKI_TAKIONPROBE_H
#define CHIAKI_TAKIONPROBE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Return-code value meaning "that call was not made" (a real return code is never INT32_MIN). */
#define CHIAKI_TAKION_PROBE_RC_NOT_TRIED INT32_MIN

/** Which kind of packet the receive thread is handling right now (log calls are attributed to it). */
typedef enum chiaki_takion_probe_class_t
{
	CHIAKI_TAKION_PROBE_CLASS_NONE = 0, // outside packet handling; must stay 0 (memset resets to it)
	CHIAKI_TAKION_PROBE_CLASS_VIDEO,
	CHIAKI_TAKION_PROBE_CLASS_AUDIO,    // includes haptics: same Takion base type
	CHIAKI_TAKION_PROBE_CLASS_CONTROL
} ChiakiTakionProbeClass;

/** Count, total and longest of one kind of timed span, accumulated by the receive thread. */
typedef struct chiaki_takion_time_bucket_t
{
	uint64_t us;
	uint32_t n;
	uint32_t max_us; // saturated at UINT32_MAX
} ChiakiTakionTimeBucket;

/** A bucket reduced to 32 bits for publication (us saturated). */
typedef struct chiaki_takion_time_window_t
{
	uint32_t n;
	uint32_t us;
	uint32_t max_us;
} ChiakiTakionTimeWindow;

/**
 * PIPE/AUDIO counts of one window (section 9.9). All 32-bit: a window is about a second. Written
 * only by the receive thread: the lib audio receiver (net_pkts .. frames_concealed), vita_audio_cb
 * (the rest). What the audio output thread does is not here: it has its own record (PIPE/AUDIOOUT).
 */
typedef struct chiaki_takion_audio_stats_t
{
	uint32_t net_pkts, haptic_pkts;              // audio / haptics AV packets that reached the audio receiver
	uint32_t frames_new, frames_dup;             // audio frames accepted / discarded as old or repeated
	uint32_t frames_missing, gap_events;         // frame indexes skipped between accepted frames, and the jumps
	uint32_t frames_concealed;                   // lost-frame calls handed to the audio sink (capped per jump)
	uint32_t decoded;                            // frames that reached vita_audio_cb (concealed ones included)
	uint32_t catchup_frames;                     // queued frames thrown away by the catch-up rule
	uint32_t q_n, q_sum;                         // hand-off queue occupancy samples (one per push) and their sum, in frames
	uint32_t q_min, q_max;                       // smallest / largest sample (q_min is meaningless while q_n is 0)
} ChiakiTakionAudioStats;

/**
 * Everything the receive thread accumulates for PIPE/RECVTIME and PIPE/SOCKQ in one window.
 * Written ONLY by the receive thread: the Takion loop (video, audio, ctrl, sockq_*, cur_class) and,
 * because they run synchronously on that thread, the Vita callbacks vita_audio_cb / host_video_cb
 * (vsubmit, acb, acatchup) and the log paths (log_*, via recv_probe.c, which
 * checks the caller is the receive thread first). Zeroed by the same memset as ChiakiTakion.recvbuf_acc.
 */
typedef struct chiaki_takion_recvtime_acc_t
{
	ChiakiTakionTimeBucket video, audio, ctrl;   // time in takion_handle_packet, by packet class
	ChiakiTakionTimeBucket vsubmit;              // host_video_cb, inside video
	ChiakiTakionTimeBucket acb;                  // vita_audio_cb (the hand-off to the audio thread)
	uint32_t acatchup;                           // pushes on which the catch-up rule dropped frames
	ChiakiTakionAudioStats audio_stats;          // PIPE/AUDIO
	ChiakiTakionTimeBucket log;                  // log calls made by the receive thread (a subset)
	uint64_t vlog_us, alog_us, clog_us;          // log_us split by the class being handled
	uint32_t log_block;                          // emitted non-DEBUG calls (blocking mutex path)
	uint8_t cur_class;                           // ChiakiTakionProbeClass being handled, else NONE
	uint32_t sockq_n, sockq_min, sockq_max;      // all socket-queue samples
	uint32_t sockq_wake_n, sockq_wake_max;       // samples taken right after a blocking receive
	uint32_t sockq_err;                          // failed queries
	uint64_t sockq_sum, sockq_wake_sum;
	ChiakiTakionTimeBucket sockq_query;          // time spent in the queries (n counts attempts)
} ChiakiTakionRecvTimeAcc;

/** The receive thread as the system reports it, sampled at each window close. */
typedef struct chiaki_takion_recvthread_t
{
	int32_t info_rc;      // return code of sceKernelGetThreadInfo
	int32_t prio;         // currentPriority
	int32_t affinity;     // currentCpuAffinityMask
	int32_t cpu;          // currentCpuId
	int32_t last_cpu;     // lastExecutedCpuId
} ChiakiTakionRecvThread;

/** Once-per-session facts the receive thread establishes before its loop (published with every window). */
typedef struct chiaki_takion_recvsession_t
{
	uint32_t valid;               // 1 once the receive thread has filled this in
	uint32_t sockq_ok;            // 1 only if the self-check passed; sampling is off otherwise
	int32_t sockq_id;             // SceNet socket id behind takion->sock, -1 if not resolved
	int32_t sockq_flags;          // last argument of sceNetGetSockInfo that passed the check, -1 if none did
	int32_t sockq_info_rc;        // return code of the first self-check call (flags 0)
	int32_t sockq_info_rc_self;   // return code of the second (flags SELF); NOT_TRIED if the first passed
	int32_t sockq_info_id;        // `s` field of the last attempt
	uint32_t sockq_info_port_raw; // `local_port` field of the last attempt, unconverted
	int32_t sockname_rc;          // return code of getsockname on the descriptor
	uint32_t sockname_port_net;   // sin_port as getsockname stored it (unconverted)
	uint32_t sockname_port_host;  // the same after ntohs
	uint32_t clock_read_ns;       // measured cost of one chiaki_time_now_monotonic_us() read
	uint32_t thread_id_ns;        // measured cost of one sceKernelGetThreadId() call (0 off Vita)
	ChiakiTakionRecvThread start; // the thread sample taken at receive-thread start
} ChiakiTakionRecvSession;

/** Adds one timed span to a bucket. */
static inline void chiaki_takion_time_bucket_add(ChiakiTakionTimeBucket *bucket, uint64_t us)
{
	bucket->n++;
	bucket->us += us;
	uint32_t clamped = us > UINT32_MAX ? UINT32_MAX : (uint32_t)us;
	if(clamped > bucket->max_us)
		bucket->max_us = clamped;
}

/** Reduces a bucket to its published 32-bit form, saturating the total. */
static inline void chiaki_takion_time_bucket_reduce(const ChiakiTakionTimeBucket *bucket, ChiakiTakionTimeWindow *out)
{
	out->n = bucket->n;
	out->us = bucket->us > UINT32_MAX ? UINT32_MAX : (uint32_t)bucket->us;
	out->max_us = bucket->max_us;
}

/**
 * unacct_us of PIPE/RECVTIME: work_us minus the three packet classes only. The subset buckets
 * (vsubmit, acb, aout) and the log buckets are already inside a class and must NOT be passed.
 * Signed on purpose: a negative result means the classes were charged more than the window's
 * remainder (clock noise, or work_us clamped at 0) and must stay visible, never clamped or wrapped.
 */
static inline int64_t chiaki_takion_probe_unaccounted_us(uint64_t work_us, uint64_t video_us, uint64_t audio_us, uint64_t ctrl_us)
{
	uint64_t classes_us = video_us + audio_us + ctrl_us;
	return classes_us > work_us ? -(int64_t)(classes_us - work_us) : (int64_t)(work_us - classes_us);
}

/**
 * Frame indexes skipped between two consecutive accepted 16-bit frame indexes: the modular jump
 * minus one, 0 for the next frame (jump 1). Modular, so a wrap from 0xffff to 0 is not a gap.
 * Only meaningful when cur is newer than prev (the receiver's chiaki_seq_num_16_gt check).
 */
static inline uint32_t chiaki_takion_probe_seq16_missing(uint16_t prev, uint16_t cur)
{
	uint16_t jump = (uint16_t)(cur - prev);
	return jump > 1 ? (uint32_t)(jump - 1) : 0;
}

/** Integer average, 0 when there are no samples (never divides by zero). */
static inline uint32_t chiaki_takion_probe_avg(uint64_t sum, uint32_t n)
{
	if(!n)
		return 0;
	uint64_t avg = sum / n;
	return avg > UINT32_MAX ? UINT32_MAX : (uint32_t)avg;
}

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_TAKIONPROBE_H
