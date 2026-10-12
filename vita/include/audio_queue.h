// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

/*
 * Hand-off queue between the Takion receive thread and the audio output thread (#416).
 *
 * A ring of whole audio frames of one fixed size, plus the catch-up rule: after a push, if more
 * than `bound` frames are queued, the oldest are dropped so exactly `keep` (the newest) remain.
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
  size_t keep;        /* frames left after a catch-up, 1..bound */
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

/* Frames queued now. */
size_t audio_queue_count(const AudioQueue *queue);
