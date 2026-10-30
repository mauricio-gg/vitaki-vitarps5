// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "mic_frame.h"

void mic_frame_init(MicFrame *f) {
  f->fill = 0;
  f->prev = 0;
  f->have_prev = 0;
}

static size_t put_sample(MicFrame *f, int16_t v, MicFrameSink sink, void *user) {
  f->pcm[2 * f->fill] = v;
  f->pcm[2 * f->fill + 1] = v;
  if (++f->fill < MIC_FRAME_SAMPLES)
    return 0;
  f->fill = 0;
  sink(f->pcm, user);
  return 1;
}

size_t mic_frame_push(MicFrame *f, const int16_t *grain, size_t n, MicFrameSink sink, void *user) {
  size_t frames = 0;
  for (size_t i = 0; i < n; i++) {
    const int16_t x = grain[i];
    if (!f->have_prev) {
      f->prev = x;
      f->have_prev = 1;
    }
    for (int k = 1; k <= MIC_FRAME_UPSAMPLE; k++) {
      const int32_t v = f->prev + ((int32_t)x - f->prev) * k / MIC_FRAME_UPSAMPLE;
      frames += put_sample(f, (int16_t)v, sink, user);
    }
    f->prev = x;
  }
  return frames;
}
