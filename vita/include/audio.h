#pragma once

// Audio thread priority: pinned to USER_2, tied with decode (USER_1) and
// Takion recv (USER_0) at the highest user priority tier used in this codebase.
#define VITA_AUDIO_THREAD_PRIORITY 64

void vita_audio_init(unsigned int channels, unsigned int rate, void *user);

void vita_audio_cb(int16_t *buf, size_t samples_count, void *user);

void vita_audio_cleanup();
