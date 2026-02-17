#include "context.h"
#include "config.h"
#include "host.h"

#include <chiaki/base64.h>

#include <stdlib.h>
#include <string.h>

static void persist_config_or_warn(void) {
  if (!config_serialize(&context.config)) {
    LOGE("Failed to persist config changes");
  }
}

static ChiakiRegist regist = {};

static bool host_update_registered_state_from_event(VitaChiakiHost *host,
                                                    const ChiakiRegisteredHost *event_host) {
  if (!host || !event_host) {
    LOGE("Registration callback missing host data");
    return false;
  }

  if (!host->registered_state) {
    host->registered_state = malloc(sizeof(ChiakiRegisteredHost));
    if (!host->registered_state) {
      LOGE("Out of memory while storing registration state");
      return false;
    }
  }

  copy_host_registered_state(host->registered_state, event_host);
  memcpy(&host->server_mac, &event_host->server_mac, sizeof(host->server_mac));
  return true;
}

static void regist_cb(ChiakiRegistEvent *event, void *user) {
  LOGD("regist event %d", event->type);
  if (event->type == CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS) {
    if (!context.active_host) {
      LOGE("Registration callback missing active host");
      chiaki_regist_stop(&regist);
      chiaki_regist_fini(&regist);
      return;
    }
    context.active_host->type |= REGISTERED;

    if (!host_update_registered_state_from_event(context.active_host, event->registered_host)) {
      chiaki_regist_stop(&regist);
      chiaki_regist_fini(&regist);
      return;
    }

    bool updated_existing_host = false;
    for (int rhost_idx = 0; rhost_idx < context.config.num_registered_hosts; rhost_idx++) {
      VitaChiakiHost *rhost = context.config.registered_hosts[rhost_idx];
      if (!rhost)
        continue;
      if (mac_addrs_match(&(rhost->server_mac), &(context.active_host->server_mac))) {
        context.config.registered_hosts[rhost_idx] = context.active_host;
        updated_existing_host = true;
        break;
      }
    }

    if (!updated_existing_host) {
      if (context.config.num_registered_hosts >= MAX_REGISTERED_HOSTS) {
        LOGE("Max registered hosts reached; could not persist new registration.");
      } else {
        context.config.registered_hosts[context.config.num_registered_hosts++] = context.active_host;
      }
    }

    persist_config_or_warn();
  }

  chiaki_regist_stop(&regist);
  chiaki_regist_fini(&regist);
}

int host_register(VitaChiakiHost* host, int pin) {
  if (!host->hostname || !host->discovery_state) {
    return 1;
  }
  if (!context.config.psn_account_id[0]) {
    LOGE("Missing PSN account id; cannot register host.");
    return 1;
  }
  ChiakiRegistInfo regist_info = {};
  regist_info.target = host->target;
  size_t account_id_size = sizeof(uint8_t[CHIAKI_PSN_ACCOUNT_ID_SIZE]);
  ChiakiErrorCode decode_err = chiaki_base64_decode(context.config.psn_account_id,
                                                    strlen(context.config.psn_account_id),
                                                    regist_info.psn_account_id,
                                                    &(account_id_size));
  if (decode_err != CHIAKI_ERR_SUCCESS || account_id_size != CHIAKI_PSN_ACCOUNT_ID_SIZE) {
    LOGE("Failed to decode PSN account id for registration: %s", chiaki_error_string(decode_err));
    return 1;
  }
  regist_info.psn_online_id = NULL;
  regist_info.pin = pin;
  regist_info.host = host->hostname;
  regist_info.broadcast = false;
  chiaki_regist_start(&regist, &context.log, &regist_info, regist_cb, NULL);
  return 0;
}

int host_wakeup(VitaChiakiHost* host) {
  if (!host) {
    LOGE("Missing host. Cannot send wakeup signal.");
    return 1;
  }
  if (!host->hostname) {
    LOGE("Missing hostname. Cannot send wakeup signal.");
    return 1;
  }
  if (!host->registered_state) {
    LOGE("Missing registered host state for %s. Cannot send wakeup signal.", host->hostname);
    return 1;
  }
  if (!host->registered_state->rp_regist_key[0]) {
    LOGE("Missing registration credential for %s. Cannot send wakeup signal.", host->hostname);
    return 1;
  }

  char *parse_end = NULL;
  uint64_t credential = (uint64_t)strtoull(host->registered_state->rp_regist_key, &parse_end, 16);
  if (parse_end == host->registered_state->rp_regist_key || *parse_end != '\0') {
    LOGE("Invalid wake credential format for %s: \"%s\"",
         host->hostname,
         host->registered_state->rp_regist_key);
    return 1;
  }

  bool is_ps5 = chiaki_target_is_ps5(host->target);
  LOGD("Attempting wake signal to %s (target=%s, discovery_enabled=%d)",
       host->hostname,
       is_ps5 ? "PS5" : "PS4",
       context.discovery_enabled ? 1 : 0);

  ChiakiErrorCode wake_err = chiaki_discovery_wakeup(&context.log,
                                                     context.discovery_enabled ? &context.discovery.discovery : NULL,
                                                     host->hostname,
                                                     credential,
                                                     is_ps5);
  if (wake_err != CHIAKI_ERR_SUCCESS) {
    LOGE("Wake signal failed for %s: %s", host->hostname, chiaki_error_string(wake_err));
    return 1;
  }

  LOGD("Wake signal sent successfully to %s", host->hostname);
  return 0;
}
