// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
/**
 * @file mic_frame.h
 * @brief Microphone (GH #474): turns capture grains into the 10 ms frames the Opus encoder takes.
 *
 * The encoder (chiaki_opus_encoder_frame) wants exactly 480 samples per channel, 48 kHz, 16 bit,
 * and the header we hand it (as chiaki-ng does) declares 2 channels, so each frame is 960
 * interleaved values with the mono capture copied into both channels. The Vita's VOICE capture
 * port delivers 16 kHz mono in 256-sample grains, which is 768 samples at 48 kHz and does not
 * divide 480, so samples carry over from one push to the next. Each input sample is stretched 3x
 * to 48 kHz by linear interpolation; the previous sample is kept across pushes so grain
 * boundaries are not audible.
 *
 * No SDK types, so it is checked natively (test/mic_frame_tests.c).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define MIC_FRAME_SAMPLES 480
#define MIC_FRAME_CHANNELS 2
/** 48 kHz output samples per 16 kHz input sample. */
#define MIC_FRAME_UPSAMPLE 3

/** Called once per finished frame; @p pcm holds MIC_FRAME_SAMPLES * MIC_FRAME_CHANNELS values and
 * is only valid during the call. */
typedef void (*MicFrameSink)(int16_t *pcm, void *user);

typedef struct mic_frame_t {
  int16_t pcm[MIC_FRAME_SAMPLES * MIC_FRAME_CHANNELS];
  size_t fill; /* samples (per channel) written so far */
  int16_t prev;
  int have_prev;
} MicFrame;

/** Resets @p f to an empty builder. */
void mic_frame_init(MicFrame *f);

/** Feeds @p n 16 kHz capture samples; calls @p sink for every frame they complete. Returns the
 * number of frames emitted. No allocation. */
size_t mic_frame_push(MicFrame *f, const int16_t *grain, size_t n, MicFrameSink sink, void *user);
