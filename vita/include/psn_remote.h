#pragma once

#include <chiaki/common.h>
#if CHIAKI_CAN_USE_HOLEPUNCH
#include <chiaki/remote/holepunch.h>
#endif

#include "host.h"

int psn_remote_prepare_connect_host(VitaChiakiHost *host
#if CHIAKI_CAN_USE_HOLEPUNCH
                                    ,
                                    ChiakiHolepunchSession *out_session
#endif
);

int psn_remote_refresh_hosts(void);
void psn_remote_clear_cached_hosts(void);
const char *psn_remote_last_error(void);

/* Clear the Sony WS retry-interval cooldown armed by a rejected
 * session_create (see psn_remote.c). Call only after minting a *new*
 * authorization grant, not after a token refresh, so re-authentication
 * isn't shadowed by a stale cooldown (GH #204). No-op when the holepunch
 * stack is disabled. */
void psn_remote_reset_retry_gate(void);
