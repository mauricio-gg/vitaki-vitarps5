#include <stdlib.h>
#include <string.h>

#include "context.h"
#include "host.h"
#include "config.h"

static void persist_config_or_warn(void) {
  if (!config_serialize(&context.config)) {
    LOGE("Failed to save config after host list update");
  }
}

static int count_nonnull_context_hosts(void) {
  int sum = 0;
  for (int host_idx = 0; host_idx < MAX_CONTEXT_HOSTS; host_idx++) {
    VitaChiakiHost *h = context.hosts[host_idx];
    if (h) {
      sum += 1;
    }
  }
  return sum;
}

bool mac_addrs_match(MacAddr* a, MacAddr* b) {
  for (int j = 0; j < 6; j++) {
    if ((*a)[j] != (*b)[j]) {
      return false;
    }
  }
  return true;
}

void save_manual_host(VitaChiakiHost* rhost, char* new_hostname) {
  if ((!rhost->server_mac)) {
    CHIAKI_LOGE(&(context.log), "Failed to get registered host mac; could not save.");
  }

  for (int i = 0; i < context.config.num_manual_hosts; i++) {
    VitaChiakiHost* h = context.config.manual_hosts[i];
    if (mac_addrs_match(&(h->server_mac), &(rhost->server_mac))) {
      if (strcmp(h->hostname, new_hostname) == 0) {
        CHIAKI_LOGW(&(context.log), "Duplicate manual host. Not saving.");
        return;
      }
    }
  }

  VitaChiakiHost* newhost = (VitaChiakiHost*)malloc(sizeof(VitaChiakiHost));
  copy_host(newhost, rhost, false);
  newhost->hostname = strdup(new_hostname);
  newhost->type = REGISTERED | MANUALLY_ADDED;

  CHIAKI_LOGI(&(context.log), "--");
  CHIAKI_LOGI(&(context.log), "Adding manual host:");

  if(newhost->hostname)
    CHIAKI_LOGI(&(context.log), "Host Name (address):               %s", newhost->hostname);
  if(newhost->server_mac) {
    CHIAKI_LOGI(&(context.log), "Host MAC:                          %X%X%X%X%X%X\n", newhost->server_mac[0], newhost->server_mac[1], newhost->server_mac[2], newhost->server_mac[3], newhost->server_mac[4], newhost->server_mac[5]);
  }
  CHIAKI_LOGI(&(context.log),   "Is PS5:                            %s", chiaki_target_is_ps5(newhost->target) ? "true" : "false");

  CHIAKI_LOGI(&(context.log), "--");

  if (context.config.num_manual_hosts >= MAX_MANUAL_HOSTS) {
    CHIAKI_LOGE(&(context.log), "Max manual hosts reached; could not save.");
    return;
  }

  context.config.manual_hosts[context.config.num_manual_hosts++] = newhost;

  persist_config_or_warn();

  LOGD("> UPDATE CONTEXT...");
  update_context_hosts();
  LOGD("> UPDATE CONTEXT DONE");
}

void delete_manual_host(VitaChiakiHost* mhost) {

  for (int i = 0; i < context.config.num_manual_hosts; i++) {
    VitaChiakiHost* h = context.config.manual_hosts[i];
    if (h == mhost) {
      context.config.manual_hosts[i] = NULL;
    }
  }
  host_free(mhost);

  for (int i = 0; i < context.config.num_manual_hosts; i++) {
    VitaChiakiHost* h = context.config.manual_hosts[i];
    if (!h) {
      for (int j = i+1; j < context.config.num_manual_hosts; j++) {
        context.config.manual_hosts[j-1] = context.config.manual_hosts[j];
      }
      context.config.manual_hosts[context.config.num_manual_hosts-1] = NULL;
      context.config.num_manual_hosts--;
    }
  }

  persist_config_or_warn();

  update_context_hosts();
}

void update_context_hosts() {
  bool hide_remote_if_discovered = true;

  for (int host_idx = 0; host_idx < MAX_CONTEXT_HOSTS; host_idx++) {
    VitaChiakiHost* h = context.hosts[host_idx];
    if (h && (h->type & MANUALLY_ADDED)) {
      bool host_exists = false;
      for (int i = 0; i < context.config.num_manual_hosts; i++) {
        if (context.config.manual_hosts[i] == h) {
          host_exists = true;
          break;
        }
      }
      if (!host_exists) {
        context.hosts[host_idx] = NULL;
      }
    }
  }

  if (hide_remote_if_discovered) {
    for (int i = 0; i < MAX_CONTEXT_HOSTS; i++) {
      VitaChiakiHost* mhost = context.hosts[i];
      if (!(mhost && mhost->server_mac && (mhost->type & MANUALLY_ADDED))) continue;
      for (int j = 0; j < MAX_CONTEXT_HOSTS; j++) {
        if (j == i) continue;
        VitaChiakiHost* h = context.hosts[j];
        if (!(h && h->server_mac && (h->type & DISCOVERED) && !(h->type & MANUALLY_ADDED))) continue;
        if (mac_addrs_match(&(h->server_mac), &(mhost->server_mac))) {
          context.hosts[i] = NULL;
        }
      }
    }
  }

  for (int host_idx = 0; host_idx < MAX_CONTEXT_HOSTS; host_idx++) {
      VitaChiakiHost* h = context.hosts[host_idx];
      if (!h) {
        for (int j = host_idx+1; j < MAX_CONTEXT_HOSTS; j++) {
          context.hosts[j-1] = context.hosts[j];
        }
        context.hosts[MAX_CONTEXT_HOSTS-1] = NULL;
      }
  }

  for (int i = 0; i < context.config.num_manual_hosts; i++) {
    VitaChiakiHost* mhost = context.config.manual_hosts[i];

    bool already_in_context = false;
    for (int host_idx = 0; host_idx < MAX_CONTEXT_HOSTS; host_idx++) {
      VitaChiakiHost* h = context.hosts[host_idx];
      if (!h) continue;
      if ((!h->server_mac) || (!h->hostname)) continue;
      if (mac_addrs_match(&(h->server_mac), &(mhost->server_mac))) {
        if ((h->type & DISCOVERED) && hide_remote_if_discovered) {
          already_in_context = true;
          break;
        }

        if ((h->type & MANUALLY_ADDED) && (strcmp(h->hostname, mhost->hostname) == 0)) {
          already_in_context = true;
          break;
        }
      }
    }

    if (already_in_context) {
      continue;
    }

    bool added_to_context = false;
    for (int host_idx = 0; host_idx < MAX_CONTEXT_HOSTS; host_idx++) {
      VitaChiakiHost* h = context.hosts[host_idx];
      if (h == NULL) {
        context.hosts[host_idx] = mhost;
        added_to_context = true;
        break;
      }
    }

    if (!added_to_context) {
      CHIAKI_LOGE(&(context.log), "Max # of hosts reached; could not add manual host %d to context.", i);
    }

  }

  context.num_hosts = count_nonnull_context_hosts();
}

int count_manual_hosts_of_console(VitaChiakiHost* host) {
  if (!host) return 0;
  if (!host->server_mac) return 0;
  int sum = 0;
  for (int i = 0; i < context.config.num_manual_hosts; i++) {
    VitaChiakiHost* mhost = context.config.manual_hosts[i];
    if (!mhost) continue;
    if (!mhost->server_mac) continue;
    if (mac_addrs_match(&(host->server_mac), &(mhost->server_mac))) {
      sum++;
    }
  }
  return sum;
}

void copy_host(VitaChiakiHost* h_dest, VitaChiakiHost* h_src, bool copy_hostname) {
        h_dest->type = h_src->type;
        h_dest->target = h_src->target;
        if (h_src->server_mac) {
          memcpy(&h_dest->server_mac, &(h_src->server_mac), 6);
        }

        h_dest->hostname = NULL;
        if ((h_src->hostname) && copy_hostname) {
          h_dest->hostname = strdup(h_src->hostname);
        }

        h_dest->registered_state = NULL;
        ChiakiRegisteredHost* rstate_src = h_src->registered_state;
        if (rstate_src) {
          ChiakiRegisteredHost* rstate_dest = malloc(sizeof(ChiakiRegisteredHost));
          h_dest->registered_state = rstate_dest;
          copy_host_registered_state(rstate_dest, rstate_src);
        }

        h_dest->discovery_state = NULL;
        if (h_src->status_hint[0]) {
          sceClibSnprintf(h_dest->status_hint, sizeof(h_dest->status_hint), "%s", h_src->status_hint);
        } else {
          h_dest->status_hint[0] = '\0';
        }
        h_dest->status_hint_is_error = h_src->status_hint_is_error;
        h_dest->status_hint_expire_us = h_src->status_hint_expire_us;
}

void copy_host_registered_state(ChiakiRegisteredHost* rstate_dest, ChiakiRegisteredHost* rstate_src) {
  if (rstate_src) {
    if (rstate_src->server_nickname) {
      strncpy(rstate_dest->server_nickname, rstate_src->server_nickname, sizeof(rstate_dest->server_nickname));
    }
    rstate_dest->target = rstate_src->target;
    memcpy(rstate_dest->rp_key, rstate_src->rp_key, sizeof(rstate_dest->rp_key));
    rstate_dest->rp_key_type = rstate_src->rp_key_type;
    memcpy(rstate_dest->rp_regist_key, rstate_src->rp_regist_key, sizeof(rstate_dest->rp_regist_key));
  }
}
