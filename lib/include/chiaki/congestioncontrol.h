// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_CONGESTIONCONTROL_H
#define CHIAKI_CONGESTIONCONTROL_H

#include "takion.h"
#include "thread.h"
#include "packetstats.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chiaki_congestion_control_t
{
	ChiakiTakion *takion;
	ChiakiPacketStats *stats;
	ChiakiLog *log;
	ChiakiThread thread;
	ChiakiBoolPredCond stop_cond;

	// 1Hz CONGESTION/LOSS aggregate log accumulators (see congestioncontrol.c).
	// Instance-scoped (not thread-func statics) so concurrent sessions -- each
	// with their own ChiakiCongestionControl and dedicated thread -- don't share
	// accumulator state.
	uint64_t log_accum_received;
	uint64_t log_accum_raw_lost;
	uint64_t log_accum_recovered;
	uint64_t log_accum_reported_lost_precap; // post-FEC-discount, pre-10%-cap
	uint64_t log_accum_reported_lost;        // what was actually sent to the console
	unsigned int log_accum_ticks;
} ChiakiCongestionControl;

CHIAKI_EXPORT ChiakiErrorCode chiaki_congestion_control_start(ChiakiCongestionControl *control, ChiakiTakion *takion, ChiakiPacketStats *stats, ChiakiLog *log);

/**
 * Stop control and join the thread
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_congestion_control_stop(ChiakiCongestionControl *control);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_CONGESTIONCONTROL_H
