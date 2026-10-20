// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#pragma once

#include <stdbool.h>

#include "latency_mode.h"

/* Version of the one-time "every saved latency_mode becomes Max" migration (GH #420). A config
 * whose stored marker is below this has the migration applied once; fresh installs start at it. */
#define LATENCY_MODE_MIGRATION_CURRENT 1

/** latency_policy_kbps() - The start bitrate in kbps that a latency mode asks the console for
 *  (the single source of truth for the ladder). Unknown modes get the Balanced value. */
unsigned int latency_policy_kbps(VitaChiakiLatencyMode mode);

/**
 * latency_policy_start_kbps() - The bitrate a connection actually starts at: the mode's kbps,
 * and on the Internet/PSN path never more than PSN_REMOTE_BITRATE_CAP_KBPS (lower wins).
 * @uncapped_kbps: if not NULL, receives the mode's kbps when the cap lowered it, else 0.
 */
unsigned int latency_policy_start_kbps(VitaChiakiLatencyMode mode, bool psn_remote,
                                       unsigned int *uncapped_kbps);

/**
 * latency_policy_migrate() - Apply the one-time Max migration.
 * @mode: the loaded latency mode; set to Max when the migration fires.
 * @marker: the persisted migration marker; raised to LATENCY_MODE_MIGRATION_CURRENT.
 * Return: true when the migration fired (caller must persist the config), false when the marker
 * was already current and @mode (the user's own choice) was left alone.
 */
bool latency_policy_migrate(VitaChiakiLatencyMode *mode, int *marker);
