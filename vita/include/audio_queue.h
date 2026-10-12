// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

/*
 * Hand-off queue between the Takion receive thread and the audio output thread (#416).
 *
 * A ring of whole audio frames of one fixed size, plus the catch-up rule: after a push, if more
 * than `bound` frames are queued, the oldest are dropped so exactly `keep` (the newest) remain.
 * `keep` is also the start level: a queue that is not primed (new, or marked dry) lets the
 * consumer pop only once `keep` frames are queued, so playback always starts with a cushion.
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
  bool primed;        /* the start level was reached since the queue was new / marked dry */
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

/* The consumer found the device ran dry: un-primes, so the cushion is rebuilt before the next pop.
 */
void audio_queue_mark_dry(AudioQueue *queue);

/* Frames queued now. */
size_t audio_queue_count(const AudioQueue *queue);
