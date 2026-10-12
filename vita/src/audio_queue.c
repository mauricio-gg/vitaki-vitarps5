// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "audio_queue.h"

#include <string.h>

bool audio_queue_init(AudioQueue *queue, uint8_t *storage, size_t frame_bytes, size_t capacity,
                      size_t bound, size_t keep) {
  if (!queue)
    return false;
  memset(queue, 0, sizeof(*queue));
  if (!storage || frame_bytes == 0 || keep == 0 || keep > bound || capacity < bound + 1)
    return false;
  queue->storage = storage;
  queue->frame_bytes = frame_bytes;
  queue->capacity = capacity;
  queue->bound = bound;
  queue->keep = keep;
  return true;
}

size_t audio_queue_push(AudioQueue *queue, const void *frame) {
  /* Every push leaves at most `bound` frames and every pop only removes, so before a push there
   * are at most `bound` queued and the slot below is free: capacity >= bound + 1 (init). */
  size_t tail = (queue->head + queue->count) % queue->capacity;
  memcpy(queue->storage + tail * queue->frame_bytes, frame, queue->frame_bytes);
  queue->count++;
  if (queue->count <= queue->bound)
    return 0;
  size_t dropped = queue->count - queue->keep;
  queue->head = (queue->head + dropped) % queue->capacity;
  queue->count = queue->keep;
  return dropped;
}

bool audio_queue_pop(AudioQueue *queue, void *out, size_t n) {
  if (n > queue->count)
    return false;
  uint8_t *dst = out;
  for (size_t i = 0; i < n; i++) {
    memcpy(dst + i * queue->frame_bytes, queue->storage + queue->head * queue->frame_bytes,
           queue->frame_bytes);
    queue->head = (queue->head + 1) % queue->capacity;
  }
  queue->count -= n;
  return true;
}

bool audio_queue_may_pop(AudioQueue *queue, size_t n) {
  if (n > queue->count)
    return false;
  if (!queue->primed) {
    if (queue->count < queue->keep)
      return false;
    queue->primed = true;
  }
  return true;
}

void audio_queue_mark_dry(AudioQueue *queue) {
  queue->primed = false;
}

size_t audio_queue_count(const AudioQueue *queue) {
  return queue->count;
}
