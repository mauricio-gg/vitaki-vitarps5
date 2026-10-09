// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#pragma once

#include <chiaki/session.h>
#include <stdbool.h>

#include "host.h"

/// How long the probe looks for a console before giving up.
#define DISCOVERY_PROBE_DEADLINE_MS 5000
/// How often the SRCH packets are sent again while nothing has answered.
#define DISCOVERY_PROBE_RESEND_MS 1000

typedef enum discovery_probe_status_t {
  DISCOVERY_PROBE_IDLE,       ///< No probe running and no result held.
  DISCOVERY_PROBE_SEARCHING,  ///< Packets are going out, no answer yet.
  DISCOVERY_PROBE_FOUND,      ///< A console answered; its host is waiting to be taken.
  DISCOVERY_PROBE_NOT_FOUND,  ///< The deadline passed with no answer.
  DISCOVERY_PROBE_ERROR,      ///< The probe could not run or the answer was unusable (logged).
} DiscoveryProbeStatus;

/**
 * Starts a unicast discovery probe of one console by IP address, for Pair new device >
 * Enter IP address. Sends the discovery packet to the PS4 port and the PS5 port of that
 * address and keeps resending until an answer, cancel or the deadline. Works whether or not
 * the normal discovery service is running. UI thread only. Any probe already running or
 * finished is cancelled first.
 *
 * @param ip_text  Typed address; checked with ip_address_parse() and
 * ip_address_is_console_target(), so spaces and leading zeros are tolerated. The host built on
 * success carries the canonical form.
 * @return true when the probe is running. On false the status is DISCOVERY_PROBE_ERROR and the
 *         reason is logged with the address.
 */
bool discovery_probe_start(const char *ip_text);

/**
 * Advances the probe; call once per frame from the UI thread while it is SEARCHING. Never
 * blocks. Resends, checks the deadline and collects the answer. When it returns anything but
 * SEARCHING the network side is already cleaned up. Terminal results (FOUND, NOT_FOUND,
 * ERROR) are stable: repeated calls return the same value until cancel or start.
 */
DiscoveryProbeStatus discovery_probe_poll(void);

/**
 * Stops the probe and releases everything it holds, including a found host nobody took.
 * Safe in every state and when called repeatedly. UI thread only.
 */
void discovery_probe_cancel(void);

/**
 * Hands the found console to the caller, usable as context.active_host for host_register().
 * Only valid when the status is FOUND; returns NULL otherwise. The host is standalone: it is
 * never in context.hosts, so the discovery reaper cannot free it while the PIN is typed.
 * After this call the caller owns the host: a successful registration moves it into
 * context.config.registered_hosts (the table then owns it); on cancel or failure, release it
 * with discovery_probe_free_host(). Status returns to IDLE.
 *
 * The host's REGISTERED flag is set (with the stored credentials copied in) when the console's
 * MAC matches an already registered host, so the PIN screen can treat it as a re-pair.
 */
VitaChiakiHost *discovery_probe_take_host(void);

/**
 * Releases a host obtained from discovery_probe_take_host() that was NOT stored by a
 * successful registration. Clears context.active_host if it points at it. Call only when no
 * registration is running on it. NULL is ignored. A host the registered-host table holds (a
 * registration stored it, then failed to persist) is left alone: the table owns it.
 */
void discovery_probe_free_host(VitaChiakiHost *host);

/**
 * Call after a registration through the probe path succeeded: saves the typed address as a
 * manual host with save_manual_host(), so a console that broadcast cannot see appears on Home.
 * Used for new pairs and re-pairs alike (a duplicate address for the same console is skipped
 * by save_manual_host()). The new credentials also go to the broadcast-discovered entry of the
 * console (so it shows as paired at once) and to every saved manual host of it (so a re-pair
 * leaves none with the old credentials). The registered-table entry a re-pair replaces is left
 * alone, never freed.
 *
 * @param host      The host from discovery_probe_take_host(), now registered.
 * @param complete  Receives true only when the address is saved (or was already) and every live
 *                  entry of the console got the new credentials; false otherwise (logged with
 *                  the address and name). An entry in use by a running session is skipped and
 *                  counts as not updated. Must not be NULL.
 * @return the context.hosts entry that now stands for the console, for Home to focus; NULL when
 *         there is none.
 */
VitaChiakiHost *discovery_probe_save_manual_host(VitaChiakiHost *host, bool *complete);

/**
 * Gives the credentials of @host (just paired) to every other live entry of the same console:
 * the broadcast-discovered entry in context.hosts (so Home lists it as paired at once) and every
 * saved manual host. @host itself is skipped, and so is an entry in use by a running session
 * (host_in_active_use()); each failure is logged with the entry's hostname. The registered-table
 * entry, if any, is left alone. UI thread only.
 *
 * @param host         The host that was just registered.
 * @param all_updated  Set to false when any entry kept its old credentials; never set to true,
 *                     so the caller initialises it to true. Must not be NULL.
 * @return the discovered context.hosts entry that now stands for the console, or NULL.
 */
VitaChiakiHost *discovery_probe_sync_credentials(VitaChiakiHost *host, bool *all_updated);
