#include "context.h"
#include "config.h"
#include "host.h"

#include <chiaki/base64.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void persist_config_or_warn(void) {
  if (!config_serialize(&context.config)) {
    LOGE("Failed to persist config changes");
  }
}

static ChiakiRegist regist = {};

static void regist_cb(ChiakiRegistEvent *event, void *user) {
  LOGD("regist event %d", event->type);
  if (event->type == CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS) {
    context.active_host->type |= REGISTERED;

    if (context.active_host->registered_state != NULL) {
      free(context.active_host->registered_state);
      context.active_host->registered_state = event->registered_host;
      memcpy(&context.active_host->server_mac, &(event->registered_host->server_mac), 6);
      printf("FOUND HOST TO UPDATE\n");
      for (int rhost_idx = 0; rhost_idx < context.config.num_registered_hosts; rhost_idx++) {
        VitaChiakiHost* rhost =
            context.config.registered_hosts[rhost_idx];
        if (rhost == NULL) {
          continue;
        }

        printf("NAME1 %s\n", rhost->registered_state->server_nickname);
        printf("NAME2 %s\n", context.active_host->registered_state->server_nickname);
        if ((rhost->server_mac) && (context.active_host->server_mac) && mac_addrs_match(&(rhost->server_mac), &(context.active_host->server_mac))) {
          printf("FOUND MATCH\n");
          context.config.registered_hosts[rhost_idx] = context.active_host;
          break;
        }
      }
    } else {
      context.active_host->registered_state = event->registered_host;
      memcpy(&context.active_host->server_mac, &(event->registered_host->server_mac), 6);
      context.config.registered_hosts[context.config.num_registered_hosts++] = context.active_host;
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
  ChiakiRegistInfo regist_info = {};
  regist_info.target = host->target;
  size_t account_id_size = sizeof(uint8_t[CHIAKI_PSN_ACCOUNT_ID_SIZE]);
  chiaki_base64_decode(context.config.psn_account_id, /*sizeof(context.config.psn_account_id)*/12, regist_info.psn_account_id, &(account_id_size));
  regist_info.psn_online_id = NULL;
  regist_info.pin = pin;
  regist_info.host = host->hostname;
  regist_info.broadcast = false;
  chiaki_regist_start(&regist, &context.log, &regist_info, regist_cb, NULL);
  return 0;
}

int host_wakeup(VitaChiakiHost* host) {
  if (!host->hostname) {
    LOGE("Missing hostname. Cannot send wakeup signal.");
    return 1;
  }
  LOGD("Attempting to send wakeup signal....");
  uint64_t credential = (uint64_t)strtoull(host->registered_state->rp_regist_key, NULL, 16);
  chiaki_discovery_wakeup(&context.log,
                          context.discovery_enabled ? &context.discovery.discovery : NULL,
                          host->hostname, credential,
                          chiaki_target_is_ps5(host->target));
  return 0;
}
