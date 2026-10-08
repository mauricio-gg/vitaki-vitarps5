/**
 * @file ui_downsample.h
 * @brief Halving two rows of pixels into one (the popup background freeze, SPEC.md C11)
 *
 * Pure: no SDK dependency, so the averaging can be checked natively.
 */

#pragma once

#include <stdint.h>

/**
 * ui_downsample_row_pair() - Average 2 x 2 blocks of two source rows into one destination row.
 * @row0:      First source row, 2 * @dst_w pixels of 0xAABBGGRR.
 * @row1:      Second source row, 2 * @dst_w pixels.
 * @dst_w:     Pixels in the destination row.
 * @dst:       Destination row, @dst_w pixels.
 * @or_mask:   Bits set in every destination pixel afterwards (pass 0xFF000000 to force opaque).
 *
 * Each of the four channels is the mean of the four source pixels, rounded to nearest.
 */
void ui_downsample_row_pair(const uint32_t *row0, const uint32_t *row1, int dst_w, uint32_t *dst,
                            uint32_t or_mask);
