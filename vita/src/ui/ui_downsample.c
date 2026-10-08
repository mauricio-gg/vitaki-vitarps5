/**
 * @file ui_downsample.c
 * @brief 2 x 2 box average of pixel rows (see ui_downsample.h)
 */

#include "ui/ui_downsample.h"

#define CHANNEL_COUNT 4
#define CHANNEL_BITS 8
#define CHANNEL_MASK 0xFFu
/** Pixels averaged into one, and half of that for rounding to nearest. */
#define BLOCK_PIXELS 4
#define BLOCK_ROUND 2

void ui_downsample_row_pair(const uint32_t *row0, const uint32_t *row1, int dst_w, uint32_t *dst,
                            uint32_t or_mask) {
  for (int x = 0; x < dst_w; x++) {
    const uint32_t p[BLOCK_PIXELS] = {row0[2 * x], row0[2 * x + 1], row1[2 * x], row1[2 * x + 1]};
    uint32_t out = 0;
    for (int c = 0; c < CHANNEL_COUNT; c++) {
      const int shift = c * CHANNEL_BITS;
      const uint32_t sum = ((p[0] >> shift) & CHANNEL_MASK) + ((p[1] >> shift) & CHANNEL_MASK) +
                           ((p[2] >> shift) & CHANNEL_MASK) + ((p[3] >> shift) & CHANNEL_MASK);
      out |= ((sum + BLOCK_ROUND) / BLOCK_PIXELS) << shift;
    }
    dst[x] = out | or_mask;
  }
}
