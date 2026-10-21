/**
 * @file ui_stream_stats.h
 * @brief What the stream stats panel says: the FPS value rule (SPEC.md C25)
 *
 * Pure: plain numbers in, a string out, no SDK dependency, so the rule can be checked natively.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/** Text shown when a value is missing. */
#define UI_STREAM_STATS_NA "N/A"

/**
 * ui_stream_stats_format() - Write the FPS value string of the panel.
 * @incoming_fps:   Frames per second measured coming in; 0 when none.
 * @target_fps:     Requested frame rate; 0 when none.
 * @negotiated_fps: Frame rate agreed with the console; used when @target_fps is 0.
 * @fps:            Out: "in / target", "in" alone with no target, or "N/A" with no incoming.
 * @fps_size:       Size of @fps in bytes.
 * @fps is always NUL-terminated (when its size is not 0).
 */
void ui_stream_stats_format(uint32_t incoming_fps, uint32_t target_fps, uint32_t negotiated_fps,
                            char *fps, size_t fps_size);
