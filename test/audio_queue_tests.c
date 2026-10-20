// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) test for the audio hand-off queue, its catch-up rule and the standing-delay
// drain window in vita/src/audio_queue.c (tickets #416, #413). `./tools/build.sh test` only
// cross-compiles, so run it on the host:
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

/* catches: the standing-delay drain removing the NEWEST frame (the sound would jump to older audio
 * or the freshest audio would be lost) instead of the oldest, frames reordered, or a head advance
 * that does not wrap at the ring end (the head is anywhere in real play). */
static void test_drop_oldest_removes_oldest_in_order_across_wrap(void) {
  Fixture f;
  fixture_init(&f);
  uint8_t scratch[FRAME_BYTES];
  for (int i = 0; i < 9; i++) { /* head at slot 9 of 11: the queued frames wrap past the end */
    push_id(&f, 0xAA);
    assert(audio_queue_pop(&f.queue, scratch, 1));
  }
  for (uint8_t id = 1; id <= 5; id++)
    assert(push_id(&f, id) == 0);
  assert(audio_queue_drop_oldest(&f.queue, 1) == 1);
  assert(audio_queue_count(&f.queue) == 4);
  uint8_t out[4 * FRAME_BYTES];
  assert(audio_queue_pop(&f.queue, out, 4));
  for (size_t i = 0; i < 4; i++) /* 2, 3, 4, 5: frame 1 (the oldest) is gone */
    assert(out[i * FRAME_BYTES] == i + 2);
}

#define WINDOW_US 1000000u
#define CUSHION 3u

/* Feeds one sample per 10 ms for `ms` milliseconds starting at *clock_us, every sample at `level`
 * except that, if touch_at_ms >= 0, the sample at that time is `touch_level`. Returns how many
 * windows asked for a drop. */
static int run_samples(AudioDrainWindow *w, uint64_t *clock_us, int ms, size_t level,
                       int touch_at_ms, size_t touch_level) {
  int drops = 0;
  for (int t = 0; t < ms; t += 10) {
    *clock_us += 10000;
    size_t l = (t == touch_at_ms) ? touch_level : level;
    if (audio_drain_sample(w, l, *clock_us, WINDOW_US, CUSHION))
      drops++;
  }
  return drops;
}

/* catches: sound staying late for the whole session after a hiccup (a queue standing above the
 * cushion for a full window never being drained), and the drain firing more than once per window
 * (a burst of skips instead of one 10 ms step per second). */
static void test_drain_asks_once_per_window_while_level_stays_above_cushion(void) {
  AudioDrainWindow w;
  memset(&w, 0, sizeof(w));
  uint64_t clock_us = 5000000;
  assert(run_samples(&w, &clock_us, 1000, 8, -1, 0) == 0); /* 990 ms in: window still open */
  assert(run_samples(&w, &clock_us, 10, 8, -1, 0) == 1);  /* closes: one drop asked */
  assert(w.closed_min == 8 && w.closed_n == 101);
  assert(run_samples(&w, &clock_us, 1000, 8, -1, 0) == 1); /* the next window: exactly one more */
}

/* catches: the drain eating the cushion in a healthy run (a single sample down at the cushion
 * means the queue was needed there, so nothing may be dropped), including the boundary of exactly
 * the cushion value. */
static void test_drain_does_nothing_when_one_sample_touched_the_cushion(void) {
  for (size_t touch = CUSHION; touch <= CUSHION + 1; touch++) {
    AudioDrainWindow w;
    memset(&w, 0, sizeof(w));
    uint64_t clock_us = 5000000;
    assert(run_samples(&w, &clock_us, 1000, 8, 400, touch) == 0); /* window still open */
    /* the first sample after the window ran its second: it closes the window under test */
    assert(run_samples(&w, &clock_us, 10, 8, -1, 0) == (touch > CUSHION ? 1 : 0));
  }
}

/* catches: a stream stall (no samples for longer than a window) followed by one big post-stall
 * level being mistaken for a standing level and costing a frame. */
static void test_drain_ignores_a_window_that_was_a_stall(void) {
  AudioDrainWindow w;
  memset(&w, 0, sizeof(w));
  uint64_t clock_us = 5000000;
  assert(run_samples(&w, &clock_us, 300, 8, -1, 0) == 0);
  clock_us += 2000000; /* nothing arrives for 2 s */
  assert(!audio_drain_sample(&w, 9, clock_us, WINDOW_US, CUSHION));
  assert(run_samples(&w, &clock_us, 990, 8, -1, 0) == 0); /* a fresh window starts after the stall */
}

int main(void) {
  test_burst_over_bound_keeps_newest_and_counts_drops();
  test_below_bound_drops_nothing_and_keeps_order_across_wrap();
  test_pop_refused_when_too_few_queued_and_queue_unchanged();
  test_new_queue_waits_for_start_level_then_pops_down_to_device_buffer();
  test_drop_oldest_removes_oldest_in_order_across_wrap();
  test_drain_asks_once_per_window_while_level_stays_above_cushion();
  test_drain_does_nothing_when_one_sample_touched_the_cushion();
  test_drain_ignores_a_window_that_was_a_stall();
  printf("audio_queue_tests: all passed\n");
  return 0;
}
