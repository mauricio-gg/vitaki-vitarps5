// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the microphone frame builder (GH #474). Run with
// ./tools/native_tests.sh, or by hand:
//
//   cc -std=c99 -Wall -Wextra -I vita/include test/mic_frame_tests.c vita/src/mic_frame.c \
//      -o /tmp/mic_frame_tests && /tmp/mic_frame_tests

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mic_frame.h"

#define GRAIN 256 /* what the Vita VOICE port delivers: 768 samples at 48 kHz, not a divisor of 480 */
#define GRAINS 15 /* 11520 output samples = exactly 24 frames, so the last grain ends on a boundary */
#define OUT_SAMPLES (GRAINS * GRAIN * MIC_FRAME_UPSAMPLE)

typedef struct collected_t {
  int16_t left[OUT_SAMPLES];
  size_t n;
  size_t frames;
} Collected;

static void collect(int16_t *pcm, void *user) {
  Collected *c = user;
  for (size_t i = 0; i < MIC_FRAME_SAMPLES; i++) {
    assert(pcm[2 * i] == pcm[2 * i + 1]); /* mono capture is copied into both channels */
    c->left[c->n++] = pcm[2 * i];
  }
  c->frames++;
}

/* Feeds a 16 kHz ramp (steps of 3) in 256-sample grains, as the capture thread does. */
static void feed_ramp(Collected *c, int16_t *in) {
  for (size_t i = 0; i < GRAINS * GRAIN; i++)
    in[i] = (int16_t)(i * 3);

  MicFrame f;
  mic_frame_init(&f);
  size_t emitted = 0;
  for (size_t at = 0; at < GRAINS * GRAIN; at += GRAIN)
    emitted += mic_frame_push(&f, in + at, GRAIN, collect, c);
  assert(emitted == c->frames);
}

/* Grains of 768 output samples against frames of 480: whole frames come out, in the right count,
 * and every input sample lands exactly once at its place, so nothing is dropped, repeated or
 * reordered where a grain ends mid-frame. */
static void test_grain_boundaries_keep_every_sample(void) {
  static int16_t in[GRAINS * GRAIN];
  static Collected c;
  memset(&c, 0, sizeof(c));
  feed_ramp(&c, in);

  assert(c.frames == OUT_SAMPLES / MIC_FRAME_SAMPLES && c.n == OUT_SAMPLES);
  /* Input sample i lands on output 3*i + 2 (the interpolation reaches it on the third step). */
  for (size_t i = 0; i < GRAINS * GRAIN; i++)
    assert(c.left[3 * i + 2] == in[i]);
}

/* The stretch runs on across a grain boundary instead of restarting from the new grain's first
 * sample: a linear ramp in comes out as a linear ramp, with no step at any boundary. */
static void test_upsample_is_continuous_across_grains(void) {
  static int16_t in[GRAINS * GRAIN];
  static Collected c;
  memset(&c, 0, sizeof(c));
  feed_ramp(&c, in);

  /* Steps of 1 between neighbours once past the very first sample (the first input has no previous). */
  for (size_t j = 3; j < OUT_SAMPLES; j++)
    assert(c.left[j] - c.left[j - 1] == 1);
}

int main(void) {
  test_grain_boundaries_keep_every_sample();
  test_upsample_is_continuous_across_grains();
  printf("mic_frame_tests: all passed\n");
  return 0;
}
