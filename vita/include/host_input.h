#pragma once

#include <stdbool.h>

#include "debug_tools.h"

// Input sampling thread priority. Lower priority number = higher scheduling
// priority on Vita, so 96 is below decode/audio/recv (64) and the feedback
// sender (65) in this codebase's priority hierarchy; not pinned to a
// specific USER core.
#define VITA_INPUT_THREAD_PRIORITY 96

void *host_input_thread_func(void *user);
void host_request_stream_stop_from_input(const char *reason);

/* GH #275: ends the live session as a user stop would, marked as a resync so host_quit.c
 * reconnects at once. Used by the debug widget and by the 1080p decoder-backlog safety net (#473).
 * Returns false (and changes nothing) if there is no session to stop or a stop is already under
 * way. Safe to call from the decode thread: it only sets flags and signals the session. */
bool host_request_stream_resync(void);
