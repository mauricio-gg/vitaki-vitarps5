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
 * Everything the receive thread accumulates for PIPE/RECVTIME and PIPE/SOCKQ in one window.
 * Written ONLY by the receive thread: the Takion loop (video, audio, ctrl, sockq_*, cur_class) and,
 * because they run synchronously on that thread, the Vita callbacks vita_audio_cb / host_video_cb
 * (vsubmit, acb, aout, ashort, askip, acatchup) and the log paths (log_*, via recv_probe.c, which
 * checks the caller is the receive thread first). Zeroed by the same memset as ChiakiTakion.recvbuf_acc.
 */
typedef struct chiaki_takion_recvtime_acc_t
{
	ChiakiTakionTimeBucket video, audio, ctrl;   // time in takion_handle_packet, by packet class
	ChiakiTakionTimeBucket vsubmit;              // host_video_cb, inside video
	ChiakiTakionTimeBucket acb, aout;            // vita_audio_cb, and sceAudioOutOutput inside it
	uint32_t ashort, askip, acatchup;            // audio callback decisions
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

/** Once-per-session facts the receive thread establishes before its loop (published with every window). */
typedef struct chiaki_takion_recvsession_t
{
	uint32_t valid;               // 1 once the receive thread has filled this in
	uint32_t sockq_ok;            // 1 only if the self-check passed; sampling is off otherwise
	int32_t sockq_id;             // SceNet socket id behind takion->sock, -1 if not resolved
	int32_t sockq_info_rc;        // return code of the self-check sceNetGetSockInfo call
	int32_t sockq_info_id;        // `s` field it returned
	uint32_t sockq_info_port_raw; // `local_port` field it returned, unconverted
	int32_t sockname_rc;          // return code of getsockname on the descriptor
	uint32_t sockname_port_net;   // sin_port as getsockname stored it (unconverted)
	uint32_t sockname_port_host;  // the same after ntohs
	uint32_t clock_read_ns;       // measured cost of one chiaki_time_now_monotonic_us() read
} ChiakiTakionRecvSession;

/** The receive thread as the system reports it, sampled at each window close. */
typedef struct chiaki_takion_recvthread_t
{
	int32_t info_rc;      // return code of sceKernelGetThreadInfo
	int32_t prio;         // currentPriority
	int32_t affinity;     // currentCpuAffinityMask
	int32_t cpu;          // currentCpuId
	int32_t last_cpu;     // lastExecutedCpuId
} ChiakiTakionRecvThread;

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
