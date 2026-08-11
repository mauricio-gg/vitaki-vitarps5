// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/feedbacksender.h>
#include <chiaki/time.h>

#ifdef __PSVITA__
#include <psp2/kernel/processmgr.h> // sceKernelGetProcessTimeWide()
#include <psp2/kernel/threadmgr.h>  // sceKernelChangeThreadPriority()
#endif

#define FEEDBACK_STATE_TIMEOUT_MAX_MS 200 // maximum time to wait between sending 2 packets
#define FEEDBACK_STATE_TIMEOUT_MIN_MS 8   // minimum time between 2 non-idle packets being sent

#define FEEDBACK_HISTORY_BUFFER_SIZE 0x10

#ifdef __PSVITA__
/* Thread priority hierarchy on PS Vita (lower number = higher priority).
 * Video decode (video.c), audio (audio.c), and Takion recv (takion.c) are
 * each pinned to 64 on a DISTINCT dedicated USER core (USER_1/USER_2/USER_0
 * respectively) specifically so they never contend with each other. A 4th
 * thread also at 64 with no dedicated core of its own would timeslice
 * against whichever of those three owns the core it happens to land on,
 * waking 80-170x/s to do GKCrypt keystream generation plus a sendto() that
 * can block in the Vita network stack -- a real regression vector for
 * decode/audio scheduling on a project whose other open issues are exactly
 * arrival jitter and decode scheduling. 65 still achieves the whole goal
 * (far above input sampling's 96, vastly above the `0x10000100` default that
 * was the actual original bug being fixed -- see
 * chiaki_thread_create()'s default PSVita priority, lib/src/thread.c:71-72)
 * while guaranteeing it never ties with the three AV threads. */
#define FEEDBACK_SENDER_THREAD_PRIORITY 65
#endif

static void *feedback_sender_thread_func(void *user);

CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_sender_init(ChiakiFeedbackSender *feedback_sender, ChiakiTakion *takion)
{
	feedback_sender->log = takion->log;
	feedback_sender->takion = takion;

	chiaki_controller_state_set_idle(&feedback_sender->controller_state_prev);
	chiaki_controller_state_set_idle(&feedback_sender->controller_state_history_prev);
	chiaki_controller_state_set_idle(&feedback_sender->controller_state);
	feedback_sender->controller_seq_counter = 0;
	feedback_sender->should_stop = false;
	feedback_sender->controller_state_changed = false;

	feedback_sender->state_seq_num = 0;

	feedback_sender->history_seq_num = 0;

#ifdef __PSVITA__
	// PIPE/INPUT instrumentation fields (see struct definition in
	// feedbacksender.h). Explicit reset is required here, not just relied on
	// via the caller's memset: chiaki_stream_connection_run() re-enters fini()
	// + init() on the SAME embedded ChiakiFeedbackSender instance during a
	// soft-restart/reconnect (lib/src/streamconnection.c, lib/src/session.c),
	// with no memset in between -- only the cold-start chiaki_session_init()
	// zeroes the whole struct. Without this, a fast-restart would leak stale
	// latency-window state across the reconnect.
	feedback_sender->controller_state_origin_us = 0;
	feedback_sender->input_latency_sample_count = 0;
	feedback_sender->input_latency_dropped_count = 0;
#endif

	ChiakiErrorCode err = chiaki_feedback_history_buffer_init(&feedback_sender->history_buf, FEEDBACK_HISTORY_BUFFER_SIZE);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	err = chiaki_mutex_init(&feedback_sender->state_mutex, false);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_history_buffer;

	err = chiaki_cond_init(&feedback_sender->state_cond, &feedback_sender->state_mutex);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_mutex;

	err = chiaki_thread_create(&feedback_sender->thread, feedback_sender_thread_func, feedback_sender);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_cond;

	chiaki_thread_set_name(&feedback_sender->thread, "Chiaki Feedback Sender");

	return CHIAKI_ERR_SUCCESS;
error_cond:
	chiaki_cond_fini(&feedback_sender->state_cond);
error_mutex:
	chiaki_mutex_fini(&feedback_sender->state_mutex);
error_history_buffer:
	chiaki_feedback_history_buffer_fini(&feedback_sender->history_buf);
	return err;
}

CHIAKI_EXPORT void chiaki_feedback_sender_fini(ChiakiFeedbackSender *feedback_sender)
{
	chiaki_mutex_lock(&feedback_sender->state_mutex);
	feedback_sender->should_stop = true;
	chiaki_mutex_unlock(&feedback_sender->state_mutex);
	chiaki_cond_signal(&feedback_sender->state_cond);
	chiaki_thread_join(&feedback_sender->thread, NULL);
	chiaki_cond_fini(&feedback_sender->state_cond);
	chiaki_mutex_fini(&feedback_sender->state_mutex);
	chiaki_feedback_history_buffer_fini(&feedback_sender->history_buf);
}

// Cross-platform controller-state setter. NOTE: kept byte-for-byte identical
// to chiaki_feedback_sender_set_controller_state_ts() below (Vita-only) minus
// the origin_us stamp -- if the equals-check or change-detection logic here
// ever changes, mirror it there too. The one exception is the __PSVITA__-guarded
// origin_us invalidation below: it compiles to nothing outside __PSVITA__, so
// it is zero behavior change for every other platform and does not count as
// "changing the cross-platform logic" for the mirroring rule above.
CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_sender_set_controller_state(ChiakiFeedbackSender *feedback_sender, ChiakiControllerState *state)
{
	ChiakiErrorCode err = chiaki_mutex_lock(&feedback_sender->state_mutex);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	if(chiaki_controller_state_equals(&feedback_sender->controller_state, state))
	{
		chiaki_mutex_unlock(&feedback_sender->state_mutex);
		return CHIAKI_ERR_SUCCESS;
	}

	feedback_sender->controller_state = *state;
	feedback_sender->controller_state_changed = true;
#ifdef __PSVITA__
	// This is the non-_ts path (used for priming/restoring controller state,
	// not a real host_input.c poll) -- explicitly invalidate any stale/prior
	// origin so feedback_sender_record_input_latency()'s origin_us==0
	// bail-out (above) correctly skips instrumentation for this pending
	// change instead of misattributing a real timestamp to it.
	feedback_sender->controller_state_origin_us = 0;
#endif

	chiaki_mutex_unlock(&feedback_sender->state_mutex);
	chiaki_cond_signal(&feedback_sender->state_cond);

	return CHIAKI_ERR_SUCCESS;
}

#ifdef __PSVITA__
// Vita-only sibling of chiaki_feedback_sender_set_controller_state() above,
// carrying the controller-poll origin timestamp out-of-band (see the
// controller_state_origin_us field comment in feedbacksender.h for why this
// isn't plumbed through the cross-platform ChiakiControllerState struct
// instead). Body is intentionally a near-duplicate of the function above --
// do NOT modify that cross-platform function to add this behavior; keep the
// two in sync by hand if the equals-check/change-detection logic changes.
CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_sender_set_controller_state_ts(ChiakiFeedbackSender *feedback_sender, ChiakiControllerState *state, uint64_t origin_us)
{
	ChiakiErrorCode err = chiaki_mutex_lock(&feedback_sender->state_mutex);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	if(chiaki_controller_state_equals(&feedback_sender->controller_state, state))
	{
		chiaki_mutex_unlock(&feedback_sender->state_mutex);
		return CHIAKI_ERR_SUCCESS;
	}

	// Stamp origin_us only on the not-pending -> pending transition, so it
	// holds the OLDEST un-sent controller-poll origin rather than the newest.
	// Several 2ms host_input.c polls can land before the sender thread wakes;
	// if we always overwrote origin_us, only the last poll's timestamp would
	// survive, understating the true latency of whatever earlier poll's data
	// effectively got coalesced into this send.
	//
	// Gated on controller_state_origin_us itself, NOT controller_state_changed:
	// controller_state_changed is read-and-cleared to false at the top of
	// EVERY wake in feedback_sender_thread_func() (including a wake where the
	// FEEDBACK_STATE_TIMEOUT_MIN_MS floor merely deferred a send without
	// actually sending), so checking it here would re-latch a fresh (wrong)
	// origin on the very next poll after a deferred wake, losing the true
	// oldest-un-sent timestamp. controller_state_origin_us is guaranteed to be
	// exactly 0 when (and only when) nothing is currently outstanding -- see
	// the reset at the end of the thread-func loop, gated on !floor_pending --
	// so "latch only if currently 0" is the correct condition here.
	if(feedback_sender->controller_state_origin_us == 0)
		feedback_sender->controller_state_origin_us = origin_us;

	feedback_sender->controller_state = *state;
	feedback_sender->controller_state_changed = true;

	chiaki_mutex_unlock(&feedback_sender->state_mutex);
	chiaki_cond_signal(&feedback_sender->state_cond);

	return CHIAKI_ERR_SUCCESS;
}
#endif

static bool controller_state_equals_for_feedback_state(ChiakiControllerState *a, ChiakiControllerState *b)
{
	if(!(a->left_x == b->left_x
		&& a->left_y == b->left_y
		&& a->right_x == b->right_x
		&& a->right_y == b->right_y))
		return false;
#define CHECKF(n) if(a->n < b->n - 0.0000001f || a->n > b->n + 0.0000001f) return false
	CHECKF(gyro_x);
	CHECKF(gyro_y);
	CHECKF(gyro_z);
	CHECKF(accel_x);
	CHECKF(accel_y);
	CHECKF(accel_z);
	CHECKF(orient_x);
	CHECKF(orient_y);
	CHECKF(orient_z);
	CHECKF(orient_w);
#undef CHECKF
	return true;
}

static void feedback_sender_send_state(ChiakiFeedbackSender *feedback_sender)
{
	ChiakiFeedbackState state;
	state.left_x = feedback_sender->controller_state.left_x;
	state.left_y = feedback_sender->controller_state.left_y;
	state.right_x = feedback_sender->controller_state.right_x;
	state.right_y = feedback_sender->controller_state.right_y;
	state.gyro_x = feedback_sender->controller_state.gyro_x;
	state.gyro_y = feedback_sender->controller_state.gyro_y;
	state.gyro_z = feedback_sender->controller_state.gyro_z;
	state.accel_x = feedback_sender->controller_state.accel_x;
	state.accel_y = feedback_sender->controller_state.accel_y;
	state.accel_z = feedback_sender->controller_state.accel_z;

	state.orient_x = feedback_sender->controller_state.orient_x;
	state.orient_y = feedback_sender->controller_state.orient_y;
	state.orient_z = feedback_sender->controller_state.orient_z;
	state.orient_w = feedback_sender->controller_state.orient_w;

	ChiakiErrorCode err = chiaki_takion_send_feedback_state(feedback_sender->takion, feedback_sender->state_seq_num++, &state);
	if(err != CHIAKI_ERR_SUCCESS)
		CHIAKI_LOGE(feedback_sender->log, "FeedbackSender failed to send Feedback State");
}

static bool controller_state_equals_for_feedback_history(ChiakiControllerState *a, ChiakiControllerState *b)
{
	if(!(a->buttons == b->buttons
		&& a->l2_state == b->l2_state
		&& a->r2_state == b->r2_state))
		return false;

	for(size_t i=0; i<CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
	{
		if(a->touches[i].id != b->touches[i].id)
			return false;
		if(a->touches[i].id >= 0 && (a->touches[i].x != b->touches[i].x || a->touches[i].y != b->touches[i].y))
			return false;
	}
	return true;
}

static void feedback_sender_send_history_packet(ChiakiFeedbackSender *feedback_sender)
{
	uint8_t buf[0x300];
	size_t buf_size = sizeof(buf);
	ChiakiErrorCode err = chiaki_feedback_history_buffer_format(&feedback_sender->history_buf, buf, &buf_size);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(feedback_sender->log, "Feedback Sender failed to format history buffer");
		return;
	}

	//CHIAKI_LOGD(feedback_sender->log, "Feedback History:");
	//chiaki_log_hexdump(feedback_sender->log, CHIAKI_LOG_DEBUG, buf, buf_size);
	chiaki_takion_send_feedback_history(feedback_sender->takion, feedback_sender->history_seq_num++, buf, buf_size);
}

static void feedback_sender_send_history(ChiakiFeedbackSender *feedback_sender)
{
	// Uses controller_state_history_prev, not controller_state_prev: history
	// is edge-triggered and evaluated on every real change, independent of
	// the state-packet send -- see the field comments in feedbacksender.h.
	ChiakiControllerState *state_prev = &feedback_sender->controller_state_history_prev;
	ChiakiControllerState *state_now = &feedback_sender->controller_state;
	uint64_t buttons_prev = state_prev->buttons;
	uint64_t buttons_now = state_now->buttons;
	for(uint8_t i=0; i<CHIAKI_CONTROLLER_BUTTONS_COUNT; i++)
	{
		uint64_t button_id = 1 << i;
		bool prev = buttons_prev & button_id;
		bool now = buttons_now & button_id;
		if(prev != now)
		{
			ChiakiFeedbackHistoryEvent event;
			ChiakiErrorCode err = chiaki_feedback_history_event_set_button(&event, button_id, now ? 0xff : 0);
			if(err != CHIAKI_ERR_SUCCESS)
			{
				CHIAKI_LOGE(feedback_sender->log, "Feedback Sender failed to format button history event for button id %llu", (unsigned long long)button_id);
				continue;
			}
			chiaki_feedback_history_buffer_push(&feedback_sender->history_buf, &event);
			feedback_sender_send_history_packet(feedback_sender);
		}
	}

	if(state_prev->l2_state != state_now->l2_state)
	{
		ChiakiFeedbackHistoryEvent event;
		ChiakiErrorCode err = chiaki_feedback_history_event_set_button(&event, CHIAKI_CONTROLLER_ANALOG_BUTTON_L2, state_now->l2_state);
		if(err == CHIAKI_ERR_SUCCESS)
		{
			chiaki_feedback_history_buffer_push(&feedback_sender->history_buf, &event);
			feedback_sender_send_history_packet(feedback_sender);
		}
		else
			CHIAKI_LOGE(feedback_sender->log, "Feedback Sender failed to format button history event for L2");
	}

	if(state_prev->r2_state != state_now->r2_state)
	{
		ChiakiFeedbackHistoryEvent event;
		ChiakiErrorCode err = chiaki_feedback_history_event_set_button(&event, CHIAKI_CONTROLLER_ANALOG_BUTTON_R2, state_now->r2_state);
		if(err == CHIAKI_ERR_SUCCESS)
		{
			chiaki_feedback_history_buffer_push(&feedback_sender->history_buf, &event);
			feedback_sender_send_history_packet(feedback_sender);
		}
		else
			CHIAKI_LOGE(feedback_sender->log, "Feedback Sender failed to format button history event for R2");
	}

	for(size_t i=0; i<CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
	{
		if(state_prev->touches[i].id != state_now->touches[i].id && state_prev->touches[i].id >= 0)
		{
			ChiakiFeedbackHistoryEvent event;
			chiaki_feedback_history_event_set_touchpad(&event, false, (uint8_t)state_prev->touches[i].id,
					state_prev->touches[i].x, state_prev->touches[i].y);
			chiaki_feedback_history_buffer_push(&feedback_sender->history_buf, &event);
			feedback_sender_send_history_packet(feedback_sender);
		}
		else if(state_now->touches[i].id >= 0
				&& (state_prev->touches[i].id != state_now->touches[i].id
					|| state_prev->touches[i].x != state_now->touches[i].x
					|| state_prev->touches[i].y != state_now->touches[i].y))
		{
			ChiakiFeedbackHistoryEvent event;
			chiaki_feedback_history_event_set_touchpad(&event, true, (uint8_t)state_now->touches[i].id,
					state_now->touches[i].x, state_now->touches[i].y);
			chiaki_feedback_history_buffer_push(&feedback_sender->history_buf, &event);
			feedback_sender_send_history_packet(feedback_sender);
		}
	}
}

#ifdef __PSVITA__
// PIPE/INPUT instrumentation: records one origin_us -> wire_us sample into
// the per-window ring. Overwrite-oldest on overflow (same semantics as the
// PIPE/LATENCY ring at vita/src/video.c -- dropping the newest instead would
// hide late-window spikes, which is exactly what this instrumentation exists
// to catch).
static void feedback_sender_record_input_latency(ChiakiFeedbackSender *feedback_sender, uint64_t wire_us)
{
	if(feedback_sender->controller_state_origin_us == 0)
		return; // origin_us == 0 means this pending state was seeded via the non-_ts
	            // setter (streamconnection.c:381/971) or never validly stamped, not
	            // from a real host_input.c poll -- there is no meaningful origin to
	            // measure against, so skip rather than record garbage.
	if(wire_us < feedback_sender->controller_state_origin_us)
		return; // defensive: clock read out of order, discard rather than underflow
	uint64_t elapsed_us = wire_us - feedback_sender->controller_state_origin_us;
	uint32_t clamped_us = (uint32_t)(elapsed_us > UINT32_MAX ? UINT32_MAX : elapsed_us);

	uint32_t count = feedback_sender->input_latency_sample_count;
	if(count < FEEDBACK_SENDER_INPUT_LATENCY_SAMPLE_CAP)
	{
		feedback_sender->input_latency_samples_us[count] = clamped_us;
		feedback_sender->input_latency_sample_count = count + 1;
	}
	else
	{
		// Ring full for this window: overwrite the oldest slot instead of
		// dropping the newest sample (mirrors vita/src/video.c's PIPE/LATENCY
		// ring -- see its comment for why "drop newest" would hide exactly the
		// late-window spike this instrumentation is meant to detect).
		// Why `dropped_count % CAP` is a valid ring write-pointer even though
		// dropped_count isn't a dedicated write index: dropped_count is
		// guaranteed to be 0 the first time sample_count reaches CAP (both
		// start at 0, and dropped_count only ever increments in this branch,
		// which is unreachable until sample_count has already saturated to
		// CAP). host_metrics.c always resets both counters together at the
		// same point each window (true both before and after the snapshot-
		// under-lock restructure). So idx cycles 0,1,2,...,CAP-1,0,1,... in
		// exactly the order a separately-tracked write_idx would.
		uint32_t idx = feedback_sender->input_latency_dropped_count % FEEDBACK_SENDER_INPUT_LATENCY_SAMPLE_CAP;
		feedback_sender->input_latency_samples_us[idx] = clamped_us;
		feedback_sender->input_latency_dropped_count++;
	}
}
#endif

static bool state_cond_check(void *user)
{
	ChiakiFeedbackSender *feedback_sender = user;
	return feedback_sender->should_stop || feedback_sender->controller_state_changed;
}

static void *feedback_sender_thread_func(void *user)
{
	ChiakiFeedbackSender *feedback_sender = user;

#ifdef __PSVITA__
	/* See the priority-hierarchy comment above FEEDBACK_SENDER_THREAD_PRIORITY's
	 * definition near the top of this file for rationale. No CPU affinity mask
	 * is set here -- there is no spare USER core to dedicate to this thread
	 * (Vita has exactly 3 usable user cores, USER_0/1/2, already claimed one
	 * each by recv/decode/audio -- see docs/ai/REMOTE_PLAY_SMOOTHNESS_PLAN.md),
	 * so it stays unrestricted, same as host_input.c's
	 * sceKernelChangeThreadCpuAffinityMask(..., 0) precedent. */
	sceKernelChangeThreadPriority(SCE_KERNEL_THREAD_ID_SELF, FEEDBACK_SENDER_THREAD_PRIORITY);
#endif

	ChiakiErrorCode err = chiaki_mutex_lock(&feedback_sender->state_mutex);
	if(err != CHIAKI_ERR_SUCCESS)
		return NULL;

	uint64_t next_timeout = FEEDBACK_STATE_TIMEOUT_MAX_MS;
	uint64_t last_send_ms = 0;
	// Set (and left set) when a real change had to be deferred by the
	// MIN_MS floor below; a plain bool because it must NOT re-trip
	// state_cond_check's predicate (that would spin chiaki_cond_timedwait_pred
	// instead of actually sleeping out the remaining floor time).
	bool floor_pending = false;
	while(true)
	{
		err = chiaki_cond_timedwait_pred(&feedback_sender->state_cond, &feedback_sender->state_mutex, next_timeout, state_cond_check, feedback_sender);
		if(err != CHIAKI_ERR_SUCCESS && err != CHIAKI_ERR_TIMEOUT)
			break;

		if(feedback_sender->should_stop)
			break;

		bool send_feedback_state = true;
		bool send_feedback_history = false;
		bool real_change = feedback_sender->controller_state_changed;
		feedback_sender->controller_state_changed = false;

		if(real_change)
		{
			// History is edge-triggered off its own controller_state_history_prev
			// and is evaluated/sent on every real wake, independent of the
			// state-packet send below.
			//
			// NOTE: controller_state_history_prev is deliberately NOT updated
			// here. feedback_sender_send_history() below still needs to read it
			// as the OLD (pre-this-wake) reference to compute its own per-field
			// diffs (which button/L2/R2/touch actually changed); updating it
			// eagerly here would make that comparison self-vs-self and always
			// false, silently dropping every history event. It's advanced once,
			// after the send, further down.
			send_feedback_history = !controller_state_equals_for_feedback_history(
					&feedback_sender->controller_state, &feedback_sender->controller_state_history_prev);
		}

		// Minimum-interval floor: caps the ACTIVE (non-idle) state-send rate
		// at 125/s worst case (1000ms / FEEDBACK_STATE_TIMEOUT_MIN_MS). It
		// engages ONLY when consecutive real changes arrive faster than
		// 125/s (since_last_send < MIN_MS) -- an isolated input spaced
		// >= 8ms from the previous send is never delayed by it at all.
		//
		// Why 8ms is safe: the PS5 renders at 60Hz (16.7ms/frame). 8ms is
		// already finer than the console can act on, so the floor's
		// worst-case added latency cannot be perceptible even when it does
		// engage.
		//
		// Why the floor is load-bearing (do not remove it again): removing
		// it uncaps the send rate entirely. STICK_DEADBAND
		// (vita/src/host_input.c) suppresses ADC jitter on a held stick but
		// not genuine sustained movement -- during active aiming, most of
		// host_input.c's ~450Hz-effective poll loop would each trigger a
		// send, i.e. up to ~450 packets/s. That's 2.6x PR #236's pre-fix
		// 170/s and ~5x its post-fix ~80/s. PR #236's P1-P3 fixes
		// (hardware-validated) fixed a real ENOBUFS session-kill chain fed
		// by exactly this kind of send-rate flood; uncapping the rate here
		// would partially undo that validated fix in exchange for removing
		// 8ms that nothing downstream can perceive -- a bad trade.
		//
		// Net expected effect with the floor restored: it caps the active
		// rate at 125/s; STICK_DEADBAND and the idle MAX_MS backoff below
		// pull the average well below that, expected to land near or below
		// PR #236's measured ~80/s.
		if(real_change || floor_pending)
		{
			uint64_t now = chiaki_time_now_monotonic_ms();
			uint64_t since_last_send = now - last_send_ms;
			if(since_last_send < FEEDBACK_STATE_TIMEOUT_MIN_MS)
			{
				floor_pending = true;
				send_feedback_state = false;
				next_timeout = FEEDBACK_STATE_TIMEOUT_MIN_MS - since_last_send;
			}
			else
			{
				floor_pending = false;
				if(controller_state_equals_for_feedback_state(&feedback_sender->controller_state, &feedback_sender->controller_state_prev))
					send_feedback_state = false;
				next_timeout = FEEDBACK_STATE_TIMEOUT_MAX_MS;
			}
		}
		else
		{
			next_timeout = FEEDBACK_STATE_TIMEOUT_MAX_MS;
		}

		if(send_feedback_state) {
			feedback_sender_send_state(feedback_sender);
			last_send_ms = chiaki_time_now_monotonic_ms();
			feedback_sender->controller_state_prev = feedback_sender->controller_state;
			feedback_sender->controller_seq_counter++;
			if((feedback_sender->controller_seq_counter % 500) == 0) {
				CHIAKI_LOGI(feedback_sender->log,
						"Controller send seq %u (Chiaki)", feedback_sender->controller_seq_counter);
			}
		}

		if(send_feedback_history)
		{
			feedback_sender_send_history(feedback_sender);
		}

#ifdef __PSVITA__
		// Record at most one PIPE/INPUT sample per wake, covering whichever of
		// the state and/or history send(s) happened this wake -- a single wake
		// can trigger both (e.g. a button press that also nudges accel/orient
		// past its deadband), and recording each separately would double-weight
		// one input event in the percentiles and turn n= into a packet count
		// instead of an event count.
		//
		// Gated on controller_state_origin_us != 0, NOT real_change: this
		// must also fire on a wake where the floor finally clears via a
		// TIMEOUT (real_change == false this wake, floor_pending was true
		// from an earlier wake) and the deferred state send actually goes
		// out. That's precisely the send whose latency -- including the
		// floor's own contribution -- this instrumentation exists to show;
		// gating on real_change would silently miss it.
		if(feedback_sender->controller_state_origin_us != 0 && (send_feedback_state || send_feedback_history))
		{
			uint64_t wire_us = sceKernelGetProcessTimeWide();
			feedback_sender_record_input_latency(feedback_sender, wire_us);
		}

		// Clear the latch once nothing remains deferred, so the next real
		// change re-latches a fresh origin instead of reusing a stale one
		// from an already-resolved batch. Gated on !floor_pending, not on
		// whether a record fired above and not on real_change: a wake can
		// clear the floor via a TIMEOUT with nothing new polled, or resolve
		// it to "no send needed" (state now equals controller_state_prev
		// again) with nothing to record -- origin_us must still be reset in
		// both cases, or it goes stale and gets misattributed to a future,
		// unrelated change.
		//
		// Known, accepted imprecision (pre-existing, not new to this fix):
		// if a history event (e.g. a button) fires on the same wake as an
		// unrelated state change that's still floor-deferred, both share
		// this one origin_us, so the history event's recorded latency is
		// inflated by however long the unrelated state change had already
		// been deferred. Self-corrects every window and doesn't corrupt
		// anything -- just a consequence of one shared origin field serving
		// both paths.
		if(!floor_pending)
			feedback_sender->controller_state_origin_us = 0;
#endif

		// Advance the history reference only after send_history() has read it
		// (see the NOTE above); gated on real_change so an idle/timeout wake
		// (nothing changed) never touches it.
		if(real_change)
			feedback_sender->controller_state_history_prev = feedback_sender->controller_state;
	}

	chiaki_mutex_unlock(&feedback_sender->state_mutex);

	return NULL;
}
