// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
/**
 * @file mic_frame.h
 * @brief Microphone probe (GH #467): turns capture grains into the 10 ms frames the Opus encoder
 * takes.
 *
 * The encoder (chiaki_opus_encoder_frame) wants exactly 480 samples per channel, 48 kHz, 16 bit,
 * and the header we hand it (as chiaki-ng does) declares 2 channels, so each frame is 960
 * interleaved values with the mono capture copied into both channels. A capture grain is any size
 * (256, 512, 160 ...) and rarely divides 480, so samples carry over from one push to the next.
 * With upsample 3 the 16 kHz VOICE port is stretched to 48 kHz by linear interpolation; the
 * previous sample is kept across pushes so grain boundaries are not audible.
 *
 * No SDK types, so it is checked natively (test/mic_frame_tests.c).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define MIC_FRAME_SAMPLES 480
#define MIC_FRAME_CHANNELS 2

/** Called once per finished frame; @p pcm holds MIC_FRAME_SAMPLES * MIC_FRAME_CHANNELS values and
 * is only valid during the call. */
typedef void (*MicFrameSink)(int16_t *pcm, void *user);

typedef struct mic_frame_t {
  int16_t pcm[MIC_FRAME_SAMPLES * MIC_FRAME_CHANNELS];
  size_t fill;  /* samples (per channel) written so far */
  int upsample; /* 1 or 3 */
  int16_t prev;
  int have_prev;
} MicFrame;

/** @p upsample is 1 for a 48 kHz capture, 3 for a 16 kHz one. */
void mic_frame_init(MicFrame *f, int upsample);

/** Feeds @p n capture samples; calls @p sink for every frame they complete. Returns the number of
 * frames emitted. No allocation. */
size_t mic_frame_push(MicFrame *f, const int16_t *grain, size_t n, MicFrameSink sink, void *user);
