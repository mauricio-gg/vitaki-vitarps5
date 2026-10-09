#pragma once

#include <chiaki/common.h>
#if CHIAKI_CAN_USE_HOLEPUNCH
#include <chiaki/remote/holepunch.h>
#endif

#include <stdint.h>

#include "host.h"

int psn_remote_prepare_connect_host(VitaChiakiHost *host
#if CHIAKI_CAN_USE_HOLEPUNCH
                                    ,
                                    ChiakiHolepunchSession *out_session
#endif
);

int psn_remote_refresh_hosts(void);

#if CHIAKI_CAN_USE_HOLEPUNCH
/*
 * The two halves of psn_remote_refresh_hosts(), split so the background refresh can run the network
 * half on a worker thread and the apply half on the main thread (GH #366).
 */

/** Log the "PSN host refresh begin" line. */
void psn_remote_log_refresh_begin(uint64_t now_unix);

/**
 * Network half: fetch the account's PS5 device list. Reads and writes no shared state, so it is
 * safe on a worker thread. The token is passed in, copied by the caller.
 *
 * @param devices      on success, the list; free with chiaki_holepunch_free_device_list()
 * @return 0 on success, 1 on failure (logged)
 */
int psn_remote_fetch_devices(const char *token, ChiakiHolepunchDeviceInfo **devices,
                             size_t *device_count);

/**
 * Apply half: replace the PSN hosts in context.hosts with the fetched devices. Main thread only.
 * Does not save the config and does not free the list.
 *
 * @return number of consoles added
 */
int psn_remote_apply_devices(const ChiakiHolepunchDeviceInfo *devices, size_t device_count);
#endif

void psn_remote_clear_cached_hosts(void);
const char *psn_remote_last_error(void);

/* Clear the Sony WS retry-interval cooldown armed by a rejected
 * session_create (see psn_remote.c). Call only after minting a *new*
 * authorization grant, not after a token refresh, so re-authentication
 * isn't shadowed by a stale cooldown (GH #204). No-op when the holepunch
 * stack is disabled. */
void psn_remote_reset_retry_gate(void);
