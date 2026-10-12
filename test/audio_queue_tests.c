// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) test for the audio hand-off queue and its catch-up rule in
// vita/src/audio_queue.c (ticket #416). `./tools/build.sh test` only cross-compiles, so run it
// on the host:
//   cc -std=c99 -Wall -Wextra -fsanitize=address,undefined -I vita/include \
//      test/audio_queue_tests.c vita/src/audio_queue.c -o /tmp/audio_queue_tests \
//      && /tmp/audio_queue_tests

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audio_queue.h"

#define FRAME_BYTES 8u
#define BOUND 10u
#define KEEP 3u /* the start level: a device buffer (DEVICE_FRAMES) plus a cushion */
#define DEVICE_FRAMES 2u
#define CAPACITY (BOUND + 1u)

typedef struct {
  AudioQueue queue;
  uint8_t storage[CAPACITY * FRAME_BYTES];
} Fixture;

static void fixture_init(Fixture *f) {
  assert(audio_queue_init(&f->queue, f->storage, FRAME_BYTES, CAPACITY, BOUND, KEEP));
}

/* Pushes a frame whose every byte is `id`; returns the push's drop count. */
static size_t push_id(Fixture *f, uint8_t id) {
  uint8_t frame[FRAME_BYTES];
  memset(frame, id, sizeof(frame));
  return audio_queue_push(&f->queue, frame);
}

/* catches: stale sound played late after a hiccup (the catch-up keeping old frames instead of the
 * newest), a catch-up that leaves playback without its cushion (anything but exactly the start
 * level of newest frames), a wrong drop count that makes PIPE/AUDIO lie, and a head advance that does
 * not wrap at the end of the ring (in real play the head is anywhere, so the burst starts from a
 * ring that has already turned a few times and the drop crosses its end). */
static void test_burst_over_bound_keeps_newest_and_counts_drops(void) {
  Fixture f;
  fixture_init(&f);
  uint8_t scratch[FRAME_BYTES];
  for (int i = 0; i < 7; i++) { /* leaves the head at slot 7 of 11: a burst of 11 must wrap */
    assert(push_id(&f, 0xAA) == 0);
    assert(audio_queue_pop(&f.queue, scratch, 1));
  }
  assert(audio_queue_count(&f.queue) == 0);

  size_t dropped_total = 0;
  for (uint8_t id = 1; id <= BOUND; id++)
    dropped_total += push_id(&f, id);
  assert(dropped_total == 0);
  assert(audio_queue_count(&f.queue) == BOUND);

  size_t dropped = push_id(&f, BOUND + 1); /* the 11th frame crosses the bound */
  assert(dropped == BOUND + 1 - KEEP);
  assert(audio_queue_count(&f.queue) == KEEP);

  uint8_t out[KEEP * FRAME_BYTES];
  assert(audio_queue_pop(&f.queue, out, KEEP));
  for (size_t i = 0; i < KEEP; i++) /* the newest KEEP frames (9, 10, 11), in order */
    assert(out[i * FRAME_BYTES] == BOUND + 1 - (KEEP - 1) + i);
  assert(audio_queue_count(&f.queue) == 0);
}

/* catches: frames lost, repeated or reordered in normal play, including when the ring wraps
 * around its end (the case a plain "first N frames" check misses). */
static void test_below_bound_drops_nothing_and_keeps_order_across_wrap(void) {
  Fixture f;
  fixture_init(&f);
  uint8_t next_push = 1, next_pop = 1; /* wrap at 256: only equality of the sequence matters */
  for (int round = 0; round < 30; round++) { /* 30 * 3 frames walks the 11-slot ring 8 times */
    for (int i = 0; i < 3; i++)
      assert(push_id(&f, next_push++) == 0);
    uint8_t out[3 * FRAME_BYTES];
    assert(audio_queue_pop(&f.queue, out, 3));
    for (size_t i = 0; i < 3; i++, next_pop++)
      for (size_t b = 0; b < FRAME_BYTES; b++)
        assert(out[i * FRAME_BYTES + b] == next_pop);
    assert(audio_queue_count(&f.queue) == 0);
  }
  for (uint8_t id = 1; id <= BOUND; id++) /* filling right up to the bound still drops nothing */
    assert(push_id(&f, id) == 0);
  assert(audio_queue_count(&f.queue) == BOUND);
}

/* catches: the output thread playing stale or uninitialised memory when fewer frames than a
 * device buffer are queued, or a refused pop still consuming frames. */
static void test_pop_refused_when_too_few_queued_and_queue_unchanged(void) {
  Fixture f;
  fixture_init(&f);
  push_id(&f, 7);
  uint8_t out[2 * FRAME_BYTES];
  memset(out, 0xEE, sizeof(out));
  assert(!audio_queue_pop(&f.queue, out, 2));
  assert(audio_queue_count(&f.queue) == 1);
  assert(out[0] == 0xEE); /* nothing written */
  assert(audio_queue_pop(&f.queue, out, 1));
  assert(out[0] == 7);
}

/* catches: playback starting with no cushion (the crackle seen on hardware): a new queue must
 * refuse pops until the start level is queued, then allow them down to one device buffer. */
static void test_new_queue_waits_for_start_level_then_pops_down_to_device_buffer(void) {
  Fixture f;
  fixture_init(&f);
  for (uint8_t id = 1; id < KEEP; id++) {
    push_id(&f, id);
    assert(!audio_queue_may_pop(&f.queue, DEVICE_FRAMES)); /* enough for a buffer, not for the cushion */
  }
  push_id(&f, KEEP);
  assert(audio_queue_may_pop(&f.queue, DEVICE_FRAMES)); /* start level reached: primed */
  uint8_t out[DEVICE_FRAMES * FRAME_BYTES];
  assert(audio_queue_pop(&f.queue, out, DEVICE_FRAMES));
  assert(audio_queue_count(&f.queue) == KEEP - DEVICE_FRAMES);
  push_id(&f, KEEP + 1);
  assert(audio_queue_may_pop(&f.queue, DEVICE_FRAMES)); /* primed: no cushion needed any more */
}

int main(void) {
  test_burst_over_bound_keeps_newest_and_counts_drops();
  test_below_bound_drops_nothing_and_keeps_order_across_wrap();
  test_pop_refused_when_too_few_queued_and_queue_unchanged();
  test_new_queue_waits_for_start_level_then_pops_down_to_device_buffer();
  printf("audio_queue_tests: all passed\n");
  return 0;
}
