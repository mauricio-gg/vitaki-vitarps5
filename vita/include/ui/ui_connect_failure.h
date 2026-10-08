/**
 * @file ui_connect_failure.h
 * @brief The hand-off of a connection failure from the thread that found it to Home's
 *        "Could not connect" popup (SPEC.md C14, section 3.3)
 *
 * host_set_hint() runs on session and connection threads; the popup is drawn on the UI thread.
 * A failure is posted into one slot guarded by a lock; the UI thread takes it. The slot holds
 * copies only (the console's name and the reason), never pointers into discovery or registered
 * state. A newer failure replaces an unread older one.
 */

#pragma once

#include <stdbool.h>

#include <chiaki/session.h>

#include "host.h"

/** Size of the reason text: the longest status hint. */
#define UI_CONNECT_FAILURE_REASON_LEN 96

/** One failed connection, as it was when it was reported. */
typedef struct ui_connect_failure_t {
  VitaChiakiHost *host;  ///< identity: look it up in the console list before using it
  char name[VITA_HOST_DISPLAY_NAME_LEN];
  char reason[UI_CONNECT_FAILURE_REASON_LEN];
} UiConnectFailure;

/** ui_connect_failure_init() - Create the lock and empty the slot. Call once at start-up. */
void ui_connect_failure_init(void);

/**
 * ui_connect_failure_post() - Record that connecting to @host failed. Any thread.
 * @host:   The console; NULL is ignored.
 * @reason: The raw reason (the hint text); NULL or empty is ignored.
 *
 * Copies the console's display name (hostname when it has none) and the reason.
 */
void ui_connect_failure_post(VitaChiakiHost *host, const char *reason);

/** ui_connect_failure_clear() - Drop an unread failure for @host (its error hint was cleared).
 * Any thread. */
void ui_connect_failure_clear(const VitaChiakiHost *host);

/** ui_connect_failure_take() - Move the pending failure into @out and empty the slot. Returns
 * false, leaving @out alone, when nothing is pending. */
bool ui_connect_failure_take(UiConnectFailure *out);
