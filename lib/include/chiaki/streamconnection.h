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
	 * signaled on change of state_finished or should_stop
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
	bool remote_disconnected;
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
