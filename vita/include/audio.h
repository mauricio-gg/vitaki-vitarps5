#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Audio output thread ("VitaAudio") priority. The three media threads are each alone on a core at
// the highest user priority tier used in this codebase, so equal priorities never timeslice
// against each other: Takion recv on USER_0, decode on USER_1, audio output on USER_2. The audio
// thread sets its own priority and core when it starts; vita_audio_cb runs on the recv thread and
// changes nothing about it.
#define VITA_AUDIO_THREAD_PRIORITY 64

// Longest the hand-off queue may hold sound before the catch-up rule drops the old part of it.
// Large enough for ordinary arrival jitter: hardware logs show the stream routinely arriving in
// gaps of 50-60 ms and up to 88 ms in otherwise good runs, which queue up to about 9 frames at
// once. Small enough that sound never sits more than about a tenth of a second behind, which is
// about what the picture already waits in the decoder (PIPE/DECHOLD holds 4-5 frames, 65-80 ms).
// Converted to frames in vita/src/audio.c from the real frame size and rate (10 frames for 480
// samples at 48 kHz), and never below one device buffer plus one frame. After a catch-up exactly
// one device buffer of the newest frames is kept (20 ms): the state playback starts from, so
// sound continues from the newest audio with the smallest delay.
#define AUDIO_CATCHUP_BOUND_MS 100

void vita_audio_init(unsigned int channels, unsigned int rate, void *user);

/* Runs on the Takion receive thread: copies the decoded frame into the hand-off queue and
 * returns. Never blocks and never touches the sound hardware. */
void vita_audio_cb(int16_t *buf, size_t samples_count, void *user);

/* Stops and joins the audio thread, then frees its buffers. The receive thread must no longer be
 * able to call vita_audio_cb (the session has quit). Safe to call when audio never started. */
void vita_audio_cleanup();

/* True once vita_audio_cb has run its one-time init successfully (queue, thread and output
 * config in place). Safe to call from any thread. */
bool vita_audio_started(void);
