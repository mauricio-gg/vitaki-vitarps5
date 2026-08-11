// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_FEEDBACKSENDER_H
#define CHIAKI_FEEDBACKSENDER_H

#include "controller.h"
#include "takion.h"
#include "thread.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __PSVITA__
// PIPE/INPUT instrumentation window sample cap (Vita-only; see feedbacksender.c
// and vita/src/host_metrics.c). Matches LATENCY_WINDOW_SAMPLE_CAP in
// vita/include/stream_state.h numerically -- intentionally duplicated rather
// than shared, since lib/ must not include vita/-app headers (only raw
// VitaSDK under __PSVITA__, same rule chiaki_thread_create()'s PSVita branch
// already follows in lib/src/thread.c).
#define FEEDBACK_SENDER_INPUT_LATENCY_SAMPLE_CAP 128
#endif

typedef struct chiaki_feedback_sender_t
{
	ChiakiLog *log;
	ChiakiTakion *takion;
	ChiakiThread thread;

	ChiakiSeqNum16 state_seq_num;

	ChiakiSeqNum16 history_seq_num;
	ChiakiFeedbackHistoryBuffer history_buf;

	bool should_stop;
	// Reference for the state-packet send decision
	// (controller_state_equals_for_feedback_state(), sticks/motion fields
	// only): compared against the current controller_state to suppress
	// sending a redundant/no-op state packet when only history-path fields
	// (buttons/touches) changed but sticks/motion didn't. Updated only on an
	// actual send, never on every real change -- this is exactly what lets
	// the FEEDBACK_STATE_TIMEOUT_MIN_MS floor in feedbacksender.c coalesce
	// several rapid changes into one eventual send: while a change sits
	// deferred by the floor, it keeps comparing against this same
	// still-stale reference until the deferred send finally goes out.
	ChiakiControllerState controller_state_prev;
	// Reference for edge-triggered history detection (feedback_sender_send_history()):
	// updated on every real change, evaluated independently of the state-packet
	// send above, so short pulses (e.g. a touchpad click) still produce
	// exactly one press + one release history event.
	ChiakiControllerState controller_state_history_prev;
	ChiakiControllerState controller_state;
	bool controller_state_changed;
	uint32_t controller_seq_counter;
	ChiakiMutex state_mutex;
	ChiakiCond state_cond;
#ifdef __PSVITA__
	// PIPE/INPUT instrumentation (Vita-only; see feedbacksender.c and
	// vita/src/host_metrics.c). origin_us is the sceKernelGetProcessTimeWide()
	// timestamp of the controller poll that produced the currently-pending
	// controller_state -- stamped by chiaki_feedback_sender_set_controller_state_ts(),
	// called only from vita/src/host_input.c (reuses that file's existing
	// start_time_us read, does not add a new clock read to the 500Hz poll loop).
	uint64_t controller_state_origin_us;
	// Per-window ring of send latencies (origin_us -> immediately-after-send),
	// mirrors the existing PIPE/LATENCY ring in vita/include/stream_state.h /
	// vita/src/host_metrics.c but lives here instead: lib/ must not include
	// vita/-app headers (only raw VitaSDK under __PSVITA__, same rule
	// chiaki_thread_create()'s PSVita branch already follows), so this window
	// buffer is drained, percentile-reduced, and reset by
	// host_metrics_update_latency() reaching directly into this struct
	// (mirrors how it already reads stream_connection->av_missing_ref_events
	// etc.), under state_mutex.
	uint32_t input_latency_samples_us[FEEDBACK_SENDER_INPUT_LATENCY_SAMPLE_CAP];
	uint32_t input_latency_sample_count;
	uint32_t input_latency_dropped_count;  // overwrite-oldest count, this window
#endif
} ChiakiFeedbackSender;

CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_sender_init(ChiakiFeedbackSender *feedback_sender, ChiakiTakion *takion);
CHIAKI_EXPORT void chiaki_feedback_sender_fini(ChiakiFeedbackSender *feedback_sender);
CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_sender_set_controller_state(ChiakiFeedbackSender *feedback_sender, ChiakiControllerState *state);
#ifdef __PSVITA__
CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_sender_set_controller_state_ts(ChiakiFeedbackSender *feedback_sender, ChiakiControllerState *state, uint64_t origin_us);
#endif

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_FEEDBACKSENDER_H
