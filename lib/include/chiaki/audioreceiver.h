// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_AUDIORECEIVER_H
#define CHIAKI_AUDIORECEIVER_H

#include "common.h"
#include "log.h"
#include "audio.h"
#include "takion.h"
#include "thread.h"
#include "packetstats.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ChiakiAudioSinkHeader)(ChiakiAudioHeader *header, void *user);

/**
 * Called once per audio frame, in playback order. buf == NULL with buf_size == 0 means the frame
 * was lost on the network: the sink should conceal it (Opus packet-loss concealment, the same
 * convention libopus uses) and produce one frame of filler. Never passed to the haptics sink.
 */
typedef void (*ChiakiAudioSinkFrame)(uint8_t *buf, size_t buf_size, void *user);

/**
 * Most lost frames concealed for one gap in the frame indexes. 5 frames is the Vita's start level
 * (50 ms at 480 samples / 48 kHz), the cushion concealment exists to keep. A longer jump is a
 * stall, not scattered loss: concealing more would only push the queue toward the 10-frame
 * catch-up bound (AUDIO_CATCHUP_BOUND_MS) once the real burst arrives, where it is thrown away,
 * and Opus concealment fades toward silence over long runs anyway.
 */
#define CHIAKI_AUDIO_RECEIVER_CONCEAL_MAX_FRAMES 5

/**
 * Sink that receives Audio encoded as Opus
 */
typedef struct chiaki_audio_sink_t
{
	void *user;
	ChiakiAudioSinkHeader header_cb;
	ChiakiAudioSinkFrame frame_cb;
} ChiakiAudioSink;

typedef struct chiaki_audio_receiver_t
{
	struct chiaki_session_t *session;
	ChiakiLog *log;
	ChiakiMutex mutex;
	ChiakiSeqNum16 frame_index_prev;
	bool frame_index_startup; // whether frame_index_prev has definitely not wrapped yet
	bool have_prev; // a frame was accepted, so frame_index_prev is a real baseline for gap detection
	ChiakiPacketStats *packet_stats;
} ChiakiAudioReceiver;

CHIAKI_EXPORT ChiakiErrorCode chiaki_audio_receiver_init(ChiakiAudioReceiver *audio_receiver, struct chiaki_session_t *session, ChiakiPacketStats *packet_stats);
CHIAKI_EXPORT void chiaki_audio_receiver_fini(ChiakiAudioReceiver *audio_receiver);
CHIAKI_EXPORT void chiaki_audio_receiver_stream_info(ChiakiAudioReceiver *audio_receiver, ChiakiAudioHeader *audio_header);
CHIAKI_EXPORT void chiaki_audio_receiver_av_packet(ChiakiAudioReceiver *audio_receiver, ChiakiTakionAVPacket *packet);

static inline ChiakiAudioReceiver *chiaki_audio_receiver_new(struct chiaki_session_t *session, ChiakiPacketStats *packet_stats)
{
	ChiakiAudioReceiver *audio_receiver = CHIAKI_NEW(ChiakiAudioReceiver);
	if(!audio_receiver)
		return NULL;
	ChiakiErrorCode err = chiaki_audio_receiver_init(audio_receiver, session, packet_stats);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		free(audio_receiver);
		return NULL;
	}
	return audio_receiver;
}

static inline void chiaki_audio_receiver_free(ChiakiAudioReceiver *audio_receiver)
{
	if(!audio_receiver)
		return;
	chiaki_audio_receiver_fini(audio_receiver);
	free(audio_receiver);
}

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_AUDIORECEIVER_H
