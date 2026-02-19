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
