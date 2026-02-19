#include <stdlib.h>
#include <string.h>

#include "context.h"
#include "psn_auth.h"
#include "psn_remote.h"

#if CHIAKI_CAN_USE_HOLEPUNCH
static bool psn_uid_is_zero(const uint8_t uid[32]) {
  if (!uid)
    return true;
  for (size_t i = 0; i < 32; i++) {
    if (uid[i] != 0)
      return false;
  }
  return true;
}
#endif

int psn_remote_prepare_connect_host(
    VitaChiakiHost *host
#if CHIAKI_CAN_USE_HOLEPUNCH
    ,
    ChiakiHolepunchSession *out_session
#endif
) {
  if (!host) {
    LOGE("PSN remote prepare failed: missing host");
    return 1;
  }

#if CHIAKI_CAN_USE_HOLEPUNCH
  if (!out_session) {
    LOGE("PSN remote prepare failed: missing output session");
    return 1;
  }
  *out_session = NULL;

  if (psn_uid_is_zero(host->psn_device_uid)) {
    LOGE("PSN remote prepare failed: missing PSN device UID");
    return 1;
  }

  const char *token = psn_auth_access_token();
  if (!token || !token[0]) {
    LOGE("PSN remote prepare failed: missing OAuth access token");
    return 1;
  }

  ChiakiHolepunchSession session = chiaki_holepunch_session_init(token, &context.log);
  if (!session) {
    LOGE("PSN remote prepare failed: chiaki_holepunch_session_init failed");
    return 1;
  }

  ChiakiErrorCode err = chiaki_holepunch_session_create(session);
  if (err != CHIAKI_ERR_SUCCESS) {
    LOGE("PSN remote prepare failed: session_create: %s", chiaki_error_string(err));
    chiaki_holepunch_session_fini(session);
    return 1;
  }

  ChiakiHolepunchConsoleType console_type = chiaki_target_is_ps5(host->target)
                                                ? CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5
                                                : CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS4;
  err = chiaki_holepunch_session_start(session, host->psn_device_uid, console_type);
  if (err != CHIAKI_ERR_SUCCESS) {
    LOGE("PSN remote prepare failed: session_start: %s", chiaki_error_string(err));
    chiaki_holepunch_session_fini(session);
    return 1;
  }

  err = chiaki_holepunch_session_punch_hole(session, CHIAKI_HOLEPUNCH_PORT_TYPE_CTRL);
  if (err != CHIAKI_ERR_SUCCESS) {
    LOGE("PSN remote prepare failed: punch ctrl: %s", chiaki_error_string(err));
    chiaki_holepunch_session_fini(session);
    return 1;
  }

  char selected_addr[256] = {0};
  chiaki_get_ps_selected_addr(session, selected_addr);
  if (selected_addr[0]) {
    free(host->hostname);
    host->hostname = strdup(selected_addr);
  }

  *out_session = session;
  return 0;
#else
  LOGE("PSN internet remote play stack is not enabled in this build");
  return 1;
#endif
}
