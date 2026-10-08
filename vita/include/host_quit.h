#pragma once

#include <chiaki/session.h>

void host_handle_quit_event(ChiakiEvent *event);

/* Ends hard-fallback recovery (GH #272) after a failure outside the quit handler: clears all
 * recovery state and the Reconnecting overlay, and surfaces "Could not reconnect to console".
 * Safe from any thread. */
void host_recovery_abort(const char *why);

/* User pressed Cancel on the Reconnecting screen: clears recovery state and stops a fallback
 * connect if one is in flight. Call from the UI thread. */
void host_recovery_cancel_by_user(void);
