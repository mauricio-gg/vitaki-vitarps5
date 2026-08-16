// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_STREAMCONNECTION_H
#define CHIAKI_STREAMCONNECTION_H

#include "feedbacksender.h"
#include "takion.h"
#include "log.h"
#include "ecdh.h"
#include "gkcrypt.h"
#include "audioreceiver.h"
#include "videoreceiver.h"
#include "congestioncontrol.h"

#include <stdint.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chiaki_session_t ChiakiSession;

typedef enum {
	CHIAKI_STREAM_CONNECTION_DISCONNECT_NOT_SENT = 0,
	CHIAKI_STREAM_CONNECTION_DISCONNECT_SENT_UNACKED,
	CHIAKI_STREAM_CONNECTION_DISCONNECT_ACKED,
} ChiakiStreamConnectionDisconnectDelivery;

typedef struct chiaki_stream_connection_t
{
	struct chiaki_session_t *session;
	ChiakiLog *log;
	ChiakiTakion takion;
	uint8_t *ecdh_secret;
	ChiakiGKCrypt *gkcrypt_local;
	ChiakiGKCrypt *gkcrypt_remote;

	ChiakiPacketStats packet_stats;
	ChiakiAudioReceiver *audio_receiver;
	ChiakiVideoReceiver *video_receiver;
	ChiakiAudioReceiver *haptics_receiver;

	ChiakiFeedbackSender feedback_sender;
	ChiakiCongestionControl congestion_control;
	/**
	 * whether feedback_sender is initialized
	 * only if this is true, feedback_sender may be accessed!
	 */
	bool feedback_sender_active;
	/**
	 * protects feedback_sender and feedback_sender_active
	 */
	ChiakiMutex feedback_sender_mutex;

	/**
	 * signaled on change of state_finished, should_stop, remote_disconnected
	 * or transport_failed
	 */
	ChiakiCond state_cond;

	/**
	 * protects state, state_finished, state_failed and should_stop
	 */
	ChiakiMutex state_mutex;
	/**
	 * protects diagnostic counters sampled by Vita UI and updated from
	 * Takion/video packet paths.
	 */
	ChiakiMutex diag_mutex;

	int state;
	bool state_finished;
	bool state_failed;
	bool should_stop;
	/**
	 * Ground truth: the CONSOLE told us it disconnected us (a DISCONNECT
	 * protobuf message, e.g. "Server shutting down") or acked our own
	 * DISCONNECT. session.c and the quit-reason mapping in session.c both
	 * treat this as authoritative -- it must never be set for a failure on
	 * OUR side of the transport. See transport_failed below for that case.
	 */
	bool remote_disconnected;
	/**
	 * Set when the Takion transport died on its own (e.g. a persistent recv()
	 * failure/ENOBUFS retry budget exhausted) rather than through the normal
	 * should_stop / remote-disconnect handshake -- i.e. OUR side gave up, the
	 * console never said anything. By the time the DISCONNECT event that sets
	 * this arrives, the takion thread has already torn down its send buffer,
	 * so this flag tells chiaki_stream_connection_run() to skip sending a
	 * graceful disconnect message that would otherwise touch a finalized send
	 * buffer.
	 *
	 * Deliberately kept independent of remote_disconnected (GH transport-vs-
	 * remote-disconnect fix): session.c reads both as ground truth to decide
	 * whether a soft restart is permitted (permitted here, refused when the
	 * console genuinely disconnected us) and to pick the right quit reason.
	 * Included in state_finished_cond_check()'s wait predicate so the main
	 * heartbeat wait still wakes promptly on a transport failure even though
	 * it no longer piggybacks on remote_disconnected to do so.
	 */
	bool transport_failed;
	/**
	 * True only while this run()'s takion is connected and its send path
	 * usable -- i.e. from the moment state moves to STATE_EXPECT_BANG (big
	 * sent, takion connect confirmed) until close_takion or a mid-stream
	 * transport failure. Protected by state_mutex. Exists so
	 * chiaki_stream_connection_request_idr() -- called from the video
	 * receiver thread and from vita UI-thread callers -- can refuse to touch
	 * takion's send path while it is mid-teardown for a restart, instead of
	 * racing chiaki_takion_close().
	 */
	bool takion_active;
	/**
	 * Bounded DISCONNECT-ack tracking, protected by state_mutex like the
	 * other fields above. disconnect_seq_num is the send-buffer sequence
	 * number of the most recently sent DISCONNECT message; disconnect_ack_pending
	 * is true while chiaki_stream_connection_run() is waiting on its ack;
	 * disconnect_delivery records the outcome for callers outside the
	 * session thread (see chiaki_stream_connection_disconnect_delivery()).
	 */
	ChiakiSeqNum32 disconnect_seq_num;
	bool disconnect_ack_pending;
	ChiakiStreamConnectionDisconnectDelivery disconnect_delivery;
	/**
	 * Human-readable reason string, strdup()'d by whichever path set
	 * remote_disconnected or transport_failed (may be NULL under OOM -- see
	 * callers). Despite the name, this now also carries the diagnostic string
	 * for a transport-only failure (e.g. "Transport disconnected"); session.c
	 * disambiguates which case it is via remote_disconnected /
	 * transport_failed before choosing CHIAKI_QUIT_REASON_STREAM_CONNECTION_*,
	 * so the string itself does not need a matching rename.
	 */
	char *remote_disconnect_reason;
	// FIXME ywnico a workaround to deal with bang being called twice
	// I'm not sure what the real problem is...something with the threading implementation on vita...?
	#if defined(__PSVITA__)
	bool streaminfo_called_from_bang;
	#endif

	double measured_bitrate;
	uint32_t magic;
	uint32_t drop_events;
	uint32_t drop_packets;
	uint64_t drop_last_ms;
	uint32_t av_missing_ref_events;
	uint32_t av_corrupt_burst_events;
	uint32_t av_fec_fail_events;
	uint32_t av_sendbuf_overflow_events;
	uint32_t diag_trylock_failures;
	ChiakiSeqNum16 av_last_corrupt_start;
	ChiakiSeqNum16 av_last_corrupt_end;
} ChiakiStreamConnection;

CHIAKI_EXPORT ChiakiErrorCode chiaki_stream_connection_init(ChiakiStreamConnection *stream_connection, ChiakiSession *session);
CHIAKI_EXPORT void chiaki_stream_connection_fini(ChiakiStreamConnection *stream_connection);

/**
 * Run stream_connection synchronously
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_stream_connection_run(ChiakiStreamConnection *stream_connection, chiaki_socket_t *socket);

CHIAKI_EXPORT ChiakiErrorCode stream_connection_send_toggle_mute_direct_message(ChiakiStreamConnection *stream_connection, bool muted);
/**
 * To be called from a thread other than the one chiaki_stream_connection_run() is running on to stop stream_connection
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_stream_connection_stop(ChiakiStreamConnection *stream_connection);

/**
 * Clear should_stop before session.c re-enters chiaki_stream_connection_run() for a soft
 * stream restart (GH #214).
 *
 * CALL THIS ONLY FROM session.c, and ONLY while still holding session->state_mutex in the
 * SAME critical section that just verified "!session->should_stop" true (i.e. the section
 * that computed restart_requested). Do not call it from the per-run reset block in
 * chiaki_stream_connection_run(); see the comment there for why.
 *
 * Why calling this from inside session->state_mutex is safe: session->should_stop is monotonic
 * for the life of a session run. It is set true in exactly three places -- chiaki_session_stop()
 * and regist_cb()'s two early-setup failure paths (the latter fire only before the streaming
 * loop even starts) -- and cleared only once, at session init. It never transitions true->false
 * while a session is actively running. Every writer that can run concurrently with a live
 * session takes session->state_mutex to make its write; the single init-time clear predates any
 * other thread and needs no lock. The "!session->should_stop" read that computes
 * restart_requested in session.c, just before this function is called, is one of those
 * concurrent-capable accesses too, and likewise takes session->state_mutex. That reduces the
 * safety of this function to simple lock ordering: that read and this function's should_stop=false
 * write are one uninterrupted session->state_mutex critical section, so any writer of
 * session->should_stop is either fully ordered before it (in which case restart_requested already
 * observed should_stop==true and this function is never reached) or fully ordered after it (in
 * which case that writer's should_stop=true write -- and its corresponding
 * chiaki_stream_connection_stop() call -- lands strictly after this function's reset and is never
 * clobbered by it). Either way the stop is honored, never swallowed. Moving this call outside that
 * critical section (e.g. after session->state_mutex is unlocked, or into the ecdh_fini/ecdh_init
 * gap in session.c) breaks this proof.
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_stream_connection_prepare_restart(ChiakiStreamConnection *stream_connection);

/**
 * Get the delivery status of the most recent DISCONNECT message.
 * Safe to call after the session thread (chiaki_stream_connection_run) has
 * returned, before chiaki_stream_connection_fini() — locks/unlocks state_mutex internally.
 */
CHIAKI_EXPORT ChiakiStreamConnectionDisconnectDelivery chiaki_stream_connection_disconnect_delivery(ChiakiStreamConnection *stream_connection);
CHIAKI_EXPORT ChiakiErrorCode chiaki_stream_connection_request_idr(ChiakiStreamConnection *stream_connection);

CHIAKI_EXPORT ChiakiErrorCode stream_connection_send_corrupt_frame(ChiakiStreamConnection *stream_connection, ChiakiSeqNum16 start, ChiakiSeqNum16 end);
CHIAKI_EXPORT void chiaki_stream_connection_report_drop(ChiakiStreamConnection *stream_connection, uint32_t dropped_packets);
CHIAKI_EXPORT void chiaki_stream_connection_report_missing_ref(ChiakiStreamConnection *stream_connection);
CHIAKI_EXPORT void chiaki_stream_connection_report_fec_fail(ChiakiStreamConnection *stream_connection);
CHIAKI_EXPORT void chiaki_stream_connection_report_sendbuf_overflow(ChiakiStreamConnection *stream_connection);

#ifdef __cplusplus
}
#endif

#endif //CHIAKI_STREAMCONNECTION_H
