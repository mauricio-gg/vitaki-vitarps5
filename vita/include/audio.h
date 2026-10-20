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
// samples at 48 kHz), and never below the start level plus one frame. After a catch-up exactly the
// start level of the newest frames is kept (50 ms, see below): the state playback starts from, so
// sound continues from the newest audio and still has its cushion.
#define AUDIO_CATCHUP_BOUND_MS 100

// Start cushion on top of one device buffer (20 ms) that must be queued before playback starts:
// the start level is one device buffer plus this (50 ms, 5 frames for 480 samples at 48 kHz),
// converted to frames in vita/src/audio.c, at least 1 frame. It is a start cushion only; there is
// no re-arm after the device runs dry (the level rebuilds by itself, see streaming.md section 6).
// Measured on the first #416 hardware log: with about 20 ms of spare sound the device ran a minute
// without a dry output call but never more than 64 samples (about 1.3 ms) from one; 30 ms adds
// one frame of margin. Cost: sound plays about 30 ms later than in build 0.1.1175. Once playing,
// output continues whenever one device buffer is queued, so the cushion absorbs arrival jitter.
#define AUDIO_REFILL_CUSHION_MS 30

// Standing-sound-delay rule (#413). A burst of late sound after a stream hiccup can leave the
// hand-off queue standing at about 7.8 frames instead of about 3 and nothing lowers it unless it
// crosses the catch-up bound above. Once per window, if the lowest level seen in that window
// stayed above the start cushion (AUDIO_REFILL_CUSHION_MS), AUDIO_DRAIN_FRAMES of the oldest
// frames are dropped. One window is long enough that ordinary jitter touches the cushion at least
// once and nothing is dropped; the drop is one frame (10 ms for 480 samples at 48 kHz, taken from
// the real frame size) so each step is a barely audible skip, at most one per window.
#define AUDIO_DRAIN_WINDOW_MS 1000
#define AUDIO_DRAIN_FRAMES 1

// Number of play buffers the audio thread rotates through. A block handed to sceAudioOutOutput is
// not written again until AUDIO_PLAY_BUFFERS - 1 further calls (3 calls, 60 ms at 20 ms per block)
// have gone by: the same distance as the 4-device-buffer ring before #416. Why: the SDK does not
// say the call copies the data, and the device still reports up to 704 of 960 samples outstanding
// when the call returns, so rewriting the same buffer right away could change sound it is playing.
#define AUDIO_PLAY_BUFFERS 4

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
