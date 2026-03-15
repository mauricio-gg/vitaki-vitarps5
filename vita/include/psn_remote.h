#pragma once

#include <chiaki/common.h>
#if CHIAKI_CAN_USE_HOLEPUNCH
#include <chiaki/remote/holepunch.h>
#endif

#include "host.h"

int psn_remote_prepare_connect_host(
    VitaChiakiHost *host
#if CHIAKI_CAN_USE_HOLEPUNCH
    ,
    ChiakiHolepunchSession *out_session
#endif
);

int psn_remote_refresh_hosts(void);
void psn_remote_clear_cached_hosts(void);
const char *psn_remote_last_error(void);
