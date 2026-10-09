/**
 * @file ui_stream_stats.h
 * @brief What the stream stats panel says: the Latency and FPS value rules (SPEC.md C25)
 *
 * Pure: plain numbers in, strings out, no SDK dependency, so the rules can be checked natively.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Text shown when a value is missing or stale. */
#define UI_STREAM_STATS_NA "N/A"

/** Metrics older than this (microseconds) no longer count; the latency reads "N/A". */
#define UI_STREAM_STATS_STALE_US 3000000ULL

/**
 * ui_stream_stats_metrics_stale() - Whether the latency metrics are too old to show.
 * @now_us:         Current time, microseconds.
 * @last_update_us: When the metrics were last updated; 0 when they never were.
 * Return: True when never updated or last updated more than UI_STREAM_STATS_STALE_US ago. Exactly
 * that age is still fresh. A last-update time ahead of @now_us counts as fresh.
 */
bool ui_stream_stats_metrics_stale(uint64_t now_us, uint64_t last_update_us);

/**
 * ui_stream_stats_format() - Write the two value strings of the panel.
 * @now_us:         Current time, microseconds.
 * @last_update_us: When the latency metrics were last updated; 0 when never.
 * @rtt_ms:         Measured round trip in milliseconds; 0 when there is no value.
 * @incoming_fps:   Frames per second measured coming in; 0 when none.
 * @target_fps:     Requested frame rate; 0 when none.
 * @negotiated_fps: Frame rate agreed with the console; used when @target_fps is 0.
 * @latency:        Out: "N ms", or "N/A" when @rtt_ms is 0 or the metrics are stale.
 * @latency_size:   Size of @latency in bytes.
 * @fps:            Out: "in / target", "in" alone with no target, or "N/A" with no incoming.
 * @fps_size:       Size of @fps in bytes.
 * Both buffers are always NUL-terminated (when their size is not 0).
 */
void ui_stream_stats_format(uint64_t now_us, uint64_t last_update_us, uint32_t rtt_ms,
                            uint32_t incoming_fps, uint32_t target_fps, uint32_t negotiated_fps,
                            char *latency, size_t latency_size, char *fps, size_t fps_size);
