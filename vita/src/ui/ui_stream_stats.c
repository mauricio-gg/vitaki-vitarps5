/**
 * @file ui_stream_stats.c
 * @brief Latency and FPS value rules of the stream stats panel (SPEC.md C25)
 */

#include "ui/ui_stream_stats.h"

#include <stdio.h>

bool ui_stream_stats_metrics_stale(uint64_t now_us, uint64_t last_update_us) {
  if (last_update_us == 0)
    return true;
  return now_us > last_update_us && now_us - last_update_us > UI_STREAM_STATS_STALE_US;
}

/** Write "N/A" into @out (size @size, not 0). */
static void write_na(char *out, size_t size) {
  snprintf(out, size, "%s", UI_STREAM_STATS_NA);
}

void ui_stream_stats_format(uint64_t now_us, uint64_t last_update_us, uint32_t rtt_ms,
                            uint32_t incoming_fps, uint32_t target_fps, uint32_t negotiated_fps,
                            char *latency, size_t latency_size, char *fps, size_t fps_size) {
  if (latency && latency_size > 0) {
    if (rtt_ms > 0 && !ui_stream_stats_metrics_stale(now_us, last_update_us))
      snprintf(latency, latency_size, "%u ms", (unsigned)rtt_ms);
    else
      write_na(latency, latency_size);
  }

  if (fps && fps_size > 0) {
    const uint32_t target = target_fps ? target_fps : negotiated_fps;
    if (incoming_fps > 0 && target > 0)
      snprintf(fps, fps_size, "%u / %u", (unsigned)incoming_fps, (unsigned)target);
    else if (incoming_fps > 0)
      snprintf(fps, fps_size, "%u", (unsigned)incoming_fps);
    else
      write_na(fps, fps_size);
  }
}
