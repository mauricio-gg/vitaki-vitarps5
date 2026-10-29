// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the microphone probe's frame builder (GH #467). Run natively:
//
//   cc -std=c99 -Wall -Wextra -I vita/include test/mic_frame_tests.c vita/src/mic_frame.c \
//      -o /tmp/mic_frame_tests && /tmp/mic_frame_tests

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mic_frame.h"

#define MAX_OUT_SAMPLES 4800

typedef struct collected_t {
  int16_t left[MAX_OUT_SAMPLES];
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

/* A 48 kHz capture in 256-sample grains (which do not divide 480) comes out as the same samples,
 * in order, in whole 480-sample frames: nothing dropped, repeated or reordered at a grain
 * boundary. */
static void test_grain_boundaries_keep_every_sample(void) {
  int16_t in[1920]; /* 4 frames */
  for (size_t i = 0; i < 1920; i++)
    in[i] = (int16_t)(i + 1);

  MicFrame f;
  Collected c = {.n = 0, .frames = 0};
  mic_frame_init(&f, 1);
  for (size_t at = 0; at < 1920; at += 256) {
    size_t len = 1920 - at < 256 ? 1920 - at : 256;
    mic_frame_push(&f, in + at, len, collect, &c);
  }
  /* 1920 samples in = 4 frames out; 1920 is 7.5 grains, the last 128 are all consumed. */
  assert(c.frames == 4 && c.n == 1920);
  assert(memcmp(c.left, in, sizeof(in)) == 0);
}

/* A 16 kHz capture in 160-sample grains gives 3 output samples per input, and the
 * interpolation runs on across the grain boundary (no restart from the new grain's first
 * sample). A linear ramp in must come out as a linear ramp. */
static void test_upsample_is_continuous_across_grains(void) {
  int16_t in[480]; /* 3 grains of 160 = 1440 out = 3 frames */
  for (size_t i = 0; i < 480; i++)
    in[i] = (int16_t)(i * 30);

  MicFrame f;
  Collected c = {.n = 0, .frames = 0};
  mic_frame_init(&f, 3);
  for (size_t at = 0; at < 480; at += 160)
    mic_frame_push(&f, in + at, 160, collect, &c);

  assert(c.frames == 3 && c.n == 1440);
  /* Input sample i lands on output 3*i + 2 (the interpolation reaches it on the third step). */
  for (size_t i = 0; i < 480; i++)
    assert(c.left[3 * i + 2] == in[i]);
  /* Steps of 10 between neighbours once past the very first sample. */
  for (size_t j = 3; j < 1440; j++)
    assert(c.left[j] - c.left[j - 1] == 10);
}

int main(void) {
  test_grain_boundaries_keep_every_sample();
  test_upsample_is_continuous_across_grains();
  printf("mic_frame_tests: all passed\n");
  return 0;
}
