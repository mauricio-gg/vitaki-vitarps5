// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

/**
 * @file psn_startup_refresh.h
 * @brief The startup PSN token refresh and device-list fetch, run off the UI thread (GH #366)
 *
 * The splash and Home keep drawing while the network work runs. A worker thread does only the
 * network calls; the main thread starts it, then commits the result (tokens, hosts, config).
 */

#pragma once

/**
 * Start the startup refresh in the background and return at once. A no-op when PSN internet mode
 * is off or nothing can be refreshed. If the worker thread cannot be created it logs an error
 * and runs the refresh synchronously instead.
 * Main thread only.
 */
void psn_startup_refresh_begin(void);

/**
 * Commit the worker's result if it has finished: join the thread, apply the token, apply the
 * device list, save the config. Cheap when nothing is pending. Call once per main-loop frame,
 * and not while streaming. Main thread only.
 */
void psn_startup_refresh_poll(void);
