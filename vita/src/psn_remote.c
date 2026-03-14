#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "context.h"
#include "config.h"
#include "host.h"
#include "psn_auth.h"
#include "psn_remote.h"

#if CHIAKI_CAN_USE_HOLEPUNCH
typedef enum psn_host_add_result_t {
  PSN_HOST_ADD_RESULT_ADDED = 1,
  PSN_HOST_ADD_RESULT_SKIPPED_REMOTEPLAY_DISABLED = 0,
  PSN_HOST_ADD_RESULT_SKIPPED_NO_REGISTERED_SOURCE = -1,
  PSN_HOST_ADD_RESULT_SKIPPED_NO_FREE_SLOT = -2,
  PSN_HOST_ADD_RESULT_SKIPPED_OOM = -3,
} PsnHostAddResult;

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

static VitaChiakiHost *find_registered_source_for_device(const char *device_name,
                                                         bool *out_exact_match) {
  VitaChiakiHost *fallback = NULL;
  if (out_exact_match)
    *out_exact_match = false;
  for (int i = 0; i < context.config.num_registered_hosts; i++) {
    VitaChiakiHost *candidate = context.config.registered_hosts[i];
    if (!host_target_is_ps5_registered(candidate))
      continue;
    if (!fallback)
      fallback = candidate;
    if (device_name && candidate->registered_state->server_nickname &&
        strcmp(candidate->registered_state->server_nickname, device_name) == 0) {
      if (out_exact_match)
        *out_exact_match = true;
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

static size_t count_psn_context_hosts(void) {
  size_t count = 0;
  for (int i = 0; i < MAX_CONTEXT_HOSTS; i++) {
    VitaChiakiHost *host = context.hosts[i];
    if (host && host->source == VITA_HOST_SOURCE_PSN_REMOTE)
      count++;
  }
  return count;
}

static PsnHostAddResult add_psn_host_from_device(const ChiakiHolepunchDeviceInfo *device,
                                                 size_t device_index) {
  if (!device) {
    LOGE("PSN host refresh device[%u]: null device entry",
         (unsigned int)device_index);
    return PSN_HOST_ADD_RESULT_SKIPPED_OOM;
  }

  LOGD("PSN host refresh device[%u]: name=%s remoteplay_enabled=%d type=%d",
       (unsigned int)device_index,
       device->device_name[0] ? device->device_name : "<unnamed>",
       device->remoteplay_enabled,
       device->type);

  if (!device->remoteplay_enabled) {
    LOGD("PSN host refresh device[%u]: skipped because remote play is disabled",
         (unsigned int)device_index);
    return PSN_HOST_ADD_RESULT_SKIPPED_REMOTEPLAY_DISABLED;
  }

  bool exact_match = false;
  VitaChiakiHost *src =
      find_registered_source_for_device(device->device_name, &exact_match);
  if (!src) {
    LOGD("PSN host refresh device[%u]: skipped because no registered PS5 seed host matched name=%s",
         (unsigned int)device_index,
         device->device_name[0] ? device->device_name : "<unnamed>");
    return PSN_HOST_ADD_RESULT_SKIPPED_NO_REGISTERED_SOURCE;
  }

  LOGD("PSN host refresh device[%u]: using %s registered seed host=%s",
       (unsigned int)device_index,
       exact_match ? "exact-match" : "fallback",
       (src->registered_state && src->registered_state->server_nickname &&
        src->registered_state->server_nickname[0])
           ? src->registered_state->server_nickname
           : (src->hostname ? src->hostname : "<unnamed>"));

  int slot = -1;
  for (int i = 0; i < MAX_CONTEXT_HOSTS; i++) {
    if (!context.hosts[i]) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    LOGE("PSN host refresh device[%u]: no free host slot while adding PSN host",
         (unsigned int)device_index);
    return PSN_HOST_ADD_RESULT_SKIPPED_NO_FREE_SLOT;
  }

  VitaChiakiHost *host = calloc(1, sizeof(VitaChiakiHost));
  if (!host) {
    LOGE("PSN host refresh device[%u]: out of memory while creating PSN host",
         (unsigned int)device_index);
    return PSN_HOST_ADD_RESULT_SKIPPED_OOM;
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
  LOGD("PSN host refresh device[%u]: added PSN host slot=%d hostname=%s source_seed=%s",
       (unsigned int)device_index,
       slot,
       host->hostname ? host->hostname : "<unnamed>",
       (src->registered_state && src->registered_state->server_nickname &&
        src->registered_state->server_nickname[0])
           ? src->registered_state->server_nickname
           : (src->hostname ? src->hostname : "<unnamed>"));
  return PSN_HOST_ADD_RESULT_ADDED;
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

  uint64_t now_unix = (uint64_t)time(NULL);
  if (!psn_auth_token_is_valid(now_unix) &&
      !psn_auth_refresh_token_if_needed(now_unix, false)) {
    LOGE("PSN remote prepare failed: OAuth token invalid and refresh failed");
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
  uint64_t now_unix = (uint64_t)time(NULL);
  LOGD("PSN host refresh begin: enabled=%d has_tokens=%d token_valid=%d now=%llu expires_at=%llu current_psn_hosts=%u",
       psn_auth_enabled(),
       psn_auth_has_tokens(),
       psn_auth_token_is_valid(now_unix),
       (unsigned long long)now_unix,
       (unsigned long long)context.config.psn_oauth_expires_at_unix,
       (unsigned int)count_psn_context_hosts());
  if (!psn_auth_enabled()) {
    LOGD("PSN host refresh skipped: PSN internet mode disabled");
    return 1;
  }
  if (!psn_auth_token_is_valid(now_unix) &&
      !psn_auth_refresh_token_if_needed(now_unix, false)) {
    LOGD("PSN host refresh skipped: OAuth token invalid and refresh failed");
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

  LOGD("PSN host refresh fetched %u PSN devices", (unsigned int)device_count);
  remove_existing_psn_hosts();
  int added = 0;
  int skipped_remoteplay_disabled = 0;
  int skipped_no_registered_source = 0;
  int skipped_no_free_slot = 0;
  int skipped_oom = 0;
  for (size_t i = 0; i < device_count; i++) {
    PsnHostAddResult result = add_psn_host_from_device(&devices[i], i);
    switch (result) {
    case PSN_HOST_ADD_RESULT_ADDED:
      added++;
      break;
    case PSN_HOST_ADD_RESULT_SKIPPED_REMOTEPLAY_DISABLED:
      skipped_remoteplay_disabled++;
      break;
    case PSN_HOST_ADD_RESULT_SKIPPED_NO_REGISTERED_SOURCE:
      skipped_no_registered_source++;
      break;
    case PSN_HOST_ADD_RESULT_SKIPPED_NO_FREE_SLOT:
      skipped_no_free_slot++;
      break;
    case PSN_HOST_ADD_RESULT_SKIPPED_OOM:
      skipped_oom++;
      break;
    }
  }
  chiaki_holepunch_free_device_list(&devices);
  update_context_hosts();
  LOGD("PSN host refresh completed: added=%d skipped_remoteplay_disabled=%d skipped_no_registered_source=%d skipped_no_free_slot=%d skipped_oom=%d visible_psn_hosts=%u total_hosts=%u",
       added,
       skipped_remoteplay_disabled,
       skipped_no_registered_source,
       skipped_no_free_slot,
       skipped_oom,
       (unsigned int)count_psn_context_hosts(),
       (unsigned int)context.num_hosts);
  if (!config_serialize(&context.config)) {
    LOGE("Failed to persist config after PSN host refresh");
  }
  return 0;
#else
  LOGE("PSN host refresh unavailable: holepunch stack disabled for this build");
  return 1;
#endif
}

void psn_remote_clear_cached_hosts(void) {
#if CHIAKI_CAN_USE_HOLEPUNCH
  remove_existing_psn_hosts();
  update_context_hosts();
#endif
}
