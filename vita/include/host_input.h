#pragma once

// Input sampling thread priority. Lower priority number = higher scheduling
// priority on Vita, so 96 is below decode/audio/recv (64) and the feedback
// sender (65) in this codebase's priority hierarchy; not pinned to a
// specific USER core.
#define VITA_INPUT_THREAD_PRIORITY 96

void *host_input_thread_func(void *user);
void host_request_stream_stop_from_input(const char *reason);
