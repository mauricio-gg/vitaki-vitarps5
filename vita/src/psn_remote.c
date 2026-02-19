#include <stdlib.h>
#include <string.h>

#include "context.h"
#include "config.h"
#include "host.h"
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

static bool host_target_is_ps5_registered(const VitaChiakiHost *host) {
  return host && (host->type & REGISTERED) && host->registered_state &&
         chiaki_target_is_ps5(host->target);
}

static VitaChiakiHost *find_registered_source_for_device(const char *device_name) {
  VitaChiakiHost *fallback = NULL;
  for (int i = 0; i < context.config.num_registered_hosts; i++) {
    VitaChiakiHost *candidate = context.config.registered_hosts[i];
    if (!host_target_is_ps5_registered(candidate))
      continue;
    if (!fallback)
      fallback = candidate;
    if (device_name && candidate->registered_state->server_nickname &&
        strcmp(candidate->registered_state->server_nickname, device_name) == 0) {
      return candidate;
    }
  }
  return fallback;
}

static void remove_existing_psn_hosts(void) {
  for (int i = 0; i < MAX_CONTEXT_HOSTS; i++) {
    VitaChiakiHost *host = context.hosts[i];
    if (!host || host->source != VITA_HOST_SOURCE_PSN_REMOTE)
      continue;
    if (context.active_host == host)
      context.active_host = NULL;
    host_free(host);
    context.hosts[i] = NULL;
  }
}

static int add_psn_host_from_device(const ChiakiHolepunchDeviceInfo *device) {
  if (!device || !device->remoteplay_enabled)
    return 0;

  VitaChiakiHost *src = find_registered_source_for_device(device->device_name);
  if (!src) {
    LOGD("Skipping PSN device without matching registered PS5 host: %s",
         device->device_name);
    return 0;
  }

  int slot = -1;
  for (int i = 0; i < MAX_CONTEXT_HOSTS; i++) {
    if (!context.hosts[i]) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    LOGE("No free host slot while adding PSN remote host");
    return 0;
  }

  VitaChiakiHost *host = calloc(1, sizeof(VitaChiakiHost));
  if (!host) {
    LOGE("Out of memory while creating PSN remote host");
    return 0;
  }

  copy_host(host, src, false);
  host->source = VITA_HOST_SOURCE_PSN_REMOTE;
  host->remoteplay_enabled = device->remoteplay_enabled;
  memcpy(host->psn_device_uid, device->device_uid, sizeof(host->psn_device_uid));
  host->type |= REGISTERED;
  host->type &= ~(DISCOVERED | MANUALLY_ADDED);
  if (device->type == CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5)
    host->target = CHIAKI_TARGET_PS5_1;
  if (!host->hostname || !host->hostname[0]) {
    host->hostname = strdup(device->device_name);
  }

  context.hosts[slot] = host;
  return 1;
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

int psn_remote_refresh_hosts(void) {
#if CHIAKI_CAN_USE_HOLEPUNCH
  if (!psn_auth_enabled()) {
    LOGD("PSN host refresh skipped: PSN internet mode disabled");
    return 1;
  }
  const char *token = psn_auth_access_token();
  if (!token || !token[0]) {
    LOGD("PSN host refresh skipped: missing OAuth access token");
    return 1;
  }

  ChiakiHolepunchDeviceInfo *devices = NULL;
  size_t device_count = 0;
  ChiakiErrorCode err = chiaki_holepunch_list_devices(
      token, CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, &devices, &device_count,
      &context.log);
  if (err != CHIAKI_ERR_SUCCESS) {
    LOGE("Failed to fetch PSN remote hosts: %s", chiaki_error_string(err));
    return 1;
  }

  remove_existing_psn_hosts();
  int added = 0;
  for (size_t i = 0; i < device_count; i++)
    added += add_psn_host_from_device(&devices[i]);
  chiaki_holepunch_free_device_list(&devices);
  update_context_hosts();
  LOGD("PSN host refresh completed: %d hosts added", added);
  if (!config_serialize(&context.config)) {
    LOGE("Failed to persist config after PSN host refresh");
  }
  return 0;
#else
  LOGE("PSN host refresh unavailable: holepunch stack disabled for this build");
  return 1;
#endif
}
