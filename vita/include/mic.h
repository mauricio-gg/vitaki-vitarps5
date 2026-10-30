// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
/**
 * @file mic.h
 * @brief Sends the Vita microphone to the console (GH #474). Present in every build.
 *
 * When the "microphone" setting is on, a capture thread starts by itself once the stream is
 * flowing. It opens the VOICE capture port (sceAudioIn, 16 kHz mono), builds 480-sample frames
 * (mic_frame.h), encodes them with the core's Opus encoder at a constant 32 kbps (40-byte
 * packets, the only size the audio sender accepts) and sends them through the session. When the
 * setting is off no thread exists and nothing is sent. Every line it logs starts with "MIC/".
 *
 * Threads: vitamic_ui_tick and vitamic_set_muted belong to the UI thread (vitamic_state may be read
 * from any);
 * vitamic_on_stream_start runs on the connect thread and vitamic_stop on whichever thread tears
 * the session down.
 */

#pragma once

#include <stdbool.h>

/** What the player should see. OFF covers "off by setting" and "not started yet". FAILED means the
 * mic was meant to run this stream but did not start or died (the log says why); it lasts until
 * the next stream. */
typedef enum mic_state_t {
  MIC_STATE_OFF,
  MIC_STATE_LIVE,
  MIC_STATE_MUTED,
  MIC_STATE_FAILED,
} MicState;

/** Where a mute toggle came from; logged with every MIC/MUTE and MIC/UNMUTE and counted in
 * MIC/STATS. */
typedef enum mic_source_t {
  MIC_SOURCE_BUTTON, /**< a tap on the bottom-centre button */
  MIC_SOURCE_CORNER, /**< a tap on the top-left corner icon */
  MIC_SOURCE_COMBO,  /**< the L + R + Select hold */
  MIC_SOURCE_COUNT,
} MicSource;

/** A stream is about to start (host_stream). Snapshots the "microphone" setting for this stream.
 * @p recovery_reconnect is true for an automatic recovery reconnect: the mute state the player
 * chose carries over. Any other start (the player's own) begins unmuted. */
void vitamic_on_stream_start(bool recovery_reconnect);

/** UI thread, every streaming frame: starts the capture thread once the stream is flowing, if the
 * setting was on when the stream started. Cheap when there is nothing to do. */
void vitamic_ui_tick(void);

/** Mutes or unmutes the mic during play. A mute stops the packets and tells the console the mic
 * is off; an unmute resumes them. Safe to call in any state; the mute is remembered until the
 * next player-started stream. @p source says what the player used (logged and counted). */
void vitamic_set_muted(bool muted, MicSource source);

/** OFF, LIVE, MUTED or FAILED, for the indicator. Reads plain flags with no lock, so any thread may
 * call it. */
MicState vitamic_state(void);

/** Stops and joins the capture thread and frees the encoder. Idempotent; call before
 * chiaki_session_fini(). The mic does not start again until the next vitamic_on_stream_start(). */
void vitamic_stop(void);
