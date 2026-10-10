// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

/**
 * @file psn_background_refresh.h
 * @brief The PSN token refresh and device-list fetch, run off the UI thread (GH #366, #353)
 *
 * The UI keeps drawing while the network work runs. A worker thread does only the network calls;
 * the main thread starts it, then commits the result (tokens, hosts, config). One job runs at a
 * time, whoever started it.
 */

#pragma once

#include <stdbool.h>

/** Who started a job; it only changes the log and whether a valid token starts a job at all. */
typedef enum {
  PSN_REFRESH_STARTUP, /**< the startup step */
  PSN_REFRESH_IDLE,    /**< the once-a-minute idle timer */
  PSN_REFRESH_PROFILE, /**< the Profile "refresh hosts" action */
  PSN_REFRESH_LOGIN,   /**< a phone login just succeeded */
} PsnRefreshOrigin;

/** Called on the main thread when the job is committed; hosts_applied is true when the host list
 *  was replaced. */
typedef void (*PsnRefreshDoneFn)(bool hosts_applied);

/**
 * Start a refresh in the background and return at once. The token is refreshed first only if it
 * needs it; then the device list is fetched. If the worker thread cannot be created it logs an
 * error and runs the refresh synchronously instead (and calls on_done before returning).
 *
 * A valid token starts no job from PSN_REFRESH_IDLE; the other origins fetch the device list.
 * If a job is already running, none is started: on_done (when not NULL) is attached to the
 * running job, replacing an earlier callback, and an idle call does nothing.
 *
 * @param on_done  may be NULL
 * @return true when on_done will be (or has been) called; false when nothing was started or
 *         attached (PSN disabled, nothing refreshable, out of memory, or an idle no-op), in
 *         which case on_done is not called
 * Main thread only.
 */
bool psn_background_refresh_begin(PsnRefreshOrigin origin, PsnRefreshDoneFn on_done);

/**
 * Commit the worker's result if it has finished: join the thread, apply the token, apply the
 * device list, save the config, then call the done callback. Cheap when nothing is pending. Call
 * once per main-loop frame, and not while streaming. Main thread only.
 */
void psn_background_refresh_poll(void);
