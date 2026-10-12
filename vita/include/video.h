/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2016 Ilya Zhuravlev
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include "press_meter.h"

// Vita's sceVideodecInitLibrary only accept resolution that is multiple of 16 on either dimension,
// and the smallest resolution is 64
// Full supported resolution list can be found at:
// https://github.com/MakiseKurisu/vita-sceVideodecInitLibrary-test/
#define ROUND_UP(x, a) ((((unsigned int)x) + ((a) - 1u)) & (~((a) - 1u)))
#define ROUND_NEAREST_16(x) (round(((double)(x)) / 16) * 16)
#define VITA_DECODER_RESOLUTION_LOWER_BOUND(x) ((x) < 64 ? 64 : (x))
#define VITA_DECODER_RESOLUTION(x) (VITA_DECODER_RESOLUTION_LOWER_BOUND(ROUND_NEAREST_16(x)))
// Reference frames for H.264 decoder
// Original value: 8 (stable, tested)
// Lower values cause decoder freeze/corruption with PS5 streams
#define REF_FRAMES 8

// Decode thread priority: pinned to USER_1, tied with audio (USER_2) and
// Takion recv (USER_0) at the highest user priority tier used in this codebase.
#define VITA_DECODE_THREAD_PRIORITY 64

void vita_h264_start();
void vita_h264_stop();
void vitavideo_show_poor_net_indicator();
void vitavideo_hide_poor_net_indicator();
int vitavideo_initialized();

int vita_h264_setup(int width, int height);
void vita_h264_cleanup();
int vita_h264_decode_frame(uint8_t *buf, size_t buf_size, bool frame_corrupt,
                           uint64_t frame_first_packet_ms);
bool vita_video_render_latest_frame(void);

/* PIPE/PRESENT (#416, docs/design/streaming.md section 9.10): what the UI thread's presentation
 * passes did in one window. Counts buffer swaps, not what the panel displayed. */
#define PRESENT_GAP_BUCKETS 6
typedef struct {
  uint32_t passes;   /* every call of vita_video_render_latest_frame(), including ones that bail at
                        the teardown gate */
  uint32_t misses;   /* of those, returned because no frame was ready */
  uint32_t shown;    /* of those, presented a freshly decoded picture */
  uint32_t back;     /* presents numbered lower than the previous present */
  uint32_t same;     /* presents numbered equal to the previous present */
  uint32_t back_max; /* largest backwards step, in frames */
  uint32_t skip_frames; /* pictures that never reached the screen (forward steps minus one) */
  uint32_t gaps[PRESENT_GAP_BUCKETS]; /* swap-to-swap intervals: <12, 12-21, 21-29, 29-38, 38-55,
                                         >=55 ms */
  uint32_t gap_max_us;
} PresentWindow;

/* Copies the window being accumulated into *out and starts a new one. UI thread only (the same
 * thread as vita_video_render_latest_frame(), so no lock). */
void vita_video_present_take(PresentWindow *out);
uint32_t vita_video_decode_queue_drops(void);
/* PIPE/PRESS (#413): fills *counters with the press meter's running totals (expired, refused,
 * in-burst, unseen and unmatched, plus jumps dropped before being shown). Cheap, lock-free, any
 * thread. */
void vita_video_press_meter_totals(PressCounters *counters);
