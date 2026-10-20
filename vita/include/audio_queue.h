// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

/*
 * Hand-off queue between the Takion receive thread and the audio output thread (#416).
 *
 * A ring of whole audio frames of one fixed size, plus the catch-up rule: after a push, if more
 * than `bound` frames are queued, the oldest are dropped so exactly `keep` (the newest) remain.
 * `keep` is also the start level: a queue that is not primed (new) lets the
 * consumer pop only once `keep` frames are queued, so playback always starts with a cushion.
 * It also holds the standing-sound-delay rule (#413): a drain window that decides, from the queue
 * levels seen over one window, whether to drop the single oldest frame (audio_drain_sample).
 * Pure C99: no SDK headers, no locking (the caller holds its mutex around every call) and no
 * allocation (the caller supplies the storage), so it compiles and runs natively
 * (test/audio_queue_tests.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct audio_queue_t {
  uint8_t *storage;   /* capacity * frame_bytes bytes, owned by the caller */
  size_t frame_bytes; /* size of one frame */
  size_t capacity;    /* frames the ring can hold; always >= bound + 1 */
  size_t bound;       /* more than this many queued frames triggers a catch-up */
  size_t keep;        /* frames left after a catch-up and the start level, 1..bound */
  bool primed;        /* set once when the start level is first reached, cleared only by init */
  size_t head;        /* index of the oldest frame */
  size_t count;       /* frames queued now */
} AudioQueue;

/*
 * Sets the queue up over caller-owned storage of capacity * frame_bytes bytes. Returns false and
 * leaves the queue unusable when storage is NULL, frame_bytes is 0, keep is 0 or above bound, or
 * capacity is below bound + 1 (the smallest ring a push can never overflow).
 */
bool audio_queue_init(AudioQueue *queue, uint8_t *storage, size_t frame_bytes, size_t capacity,
                      size_t bound, size_t keep);

/*
 * Queues one frame (frame_bytes bytes read from frame), then applies the catch-up rule. Returns
 * how many of the oldest frames were dropped to catch up: 0 normally.
 */
size_t audio_queue_push(AudioQueue *queue, const void *frame);

/*
 * Moves the n oldest frames, in arrival order, into out (n * frame_bytes bytes). Returns false and
 * changes nothing when fewer than n frames are queued.
 */
bool audio_queue_pop(AudioQueue *queue, void *out, size_t n);

/*
 * The consumer's gate: may n frames be popped now? Not primed: only once `keep` frames are queued,
 * which primes the queue. Primed: whenever n frames are queued. Pure answer plus that one state
 * change, so it can be asked repeatedly (e.g. as a condition-wait predicate).
 */
bool audio_queue_may_pop(AudioQueue *queue, size_t n);

/* Frames queued now. */
size_t audio_queue_count(const AudioQueue *queue);

/*
 * Drops the n oldest frames (or all of them if fewer are queued) without copying. Returns how many
 * were dropped. Does not touch the primed state. Used by the standing-sound-delay rule (#413).
 */
size_t audio_queue_drop_oldest(AudioQueue *queue, size_t n);

/*
 * One window of the standing-sound-delay rule (#413): the lowest queue level seen since the window
 * opened and how many samples it holds. Zero-initialise before first use. closed_min/closed_n keep
 * the figures of the window that closed last, for logging.
 */
typedef struct audio_drain_window_t {
  uint64_t start_us; /* when the open window began, 0 = no window open yet */
  uint64_t last_us;  /* time of the newest sample in the open window */
  size_t min;        /* lowest level in the open window; valid when n > 0 */
  uint32_t n;        /* samples in the open window */
  size_t closed_min; /* lowest level of the window that closed last */
  uint32_t closed_n; /* samples in the window that closed last */
} AudioDrainWindow;

/*
 * Adds one queue-level sample (taken right after a push, at now_us) and, when the window has run
 * for window_us, closes it. Returns true only when the closed window qualifies: its lowest level
 * stayed above cushion, i.e. the queue never came down to the start cushion in a whole window, so
 * the caller should drop one frame. At most one true per window; a new, empty window opens at every
 * close. A window whose newest sample is already window_us old when the next one arrives is a
 * stalled stream, not a standing level: it is discarded and the answer is false.
 */
bool audio_drain_sample(AudioDrainWindow *window, size_t level, uint64_t now_us, uint64_t window_us,
                        size_t cushion);
