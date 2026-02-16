#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <chiaki/session.h>

void host_event_cb(ChiakiEvent *event, void *user);
bool host_video_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user);
