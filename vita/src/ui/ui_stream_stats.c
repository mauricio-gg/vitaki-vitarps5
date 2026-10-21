/**
 * @file ui_stream_stats.c
 * @brief FPS value rule of the stream stats panel (SPEC.md C25)
 */

#include "ui/ui_stream_stats.h"

#include <stdio.h>

void ui_stream_stats_format(uint32_t incoming_fps, uint32_t target_fps, uint32_t negotiated_fps,
                            char *fps, size_t fps_size) {
  if (!fps || fps_size == 0)
    return;

  const uint32_t target = target_fps ? target_fps : negotiated_fps;
  if (incoming_fps > 0 && target > 0)
    snprintf(fps, fps_size, "%u / %u", (unsigned)incoming_fps, (unsigned)target);
  else if (incoming_fps > 0)
    snprintf(fps, fps_size, "%u", (unsigned)incoming_fps);
  else
    snprintf(fps, fps_size, "%s", UI_STREAM_STATS_NA);
}
