#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <chiaki/session.h>

void host_event_cb(ChiakiEvent *event, void *user);
bool host_video_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered,
                   void *user);

/* GH #262: reset the staleness tracker's persistent state (recv-thread file-statics in
 * host_callbacks.c) for a fresh stream. Must be called wherever other per-stream video
 * state resets -- see host_metrics_reset_stream() in host_metrics.c, its only caller. */
void host_video_cb_reset_stale_tracker(void);
