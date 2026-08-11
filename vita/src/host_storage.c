#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "context.h"
#include "host.h"
#include "config.h"
#include "ui/ui_console_cards.h"

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

static void compact_context_hosts(void) {
  int write_idx = 0;
  for (int read_idx = 0; read_idx < MAX_CONTEXT_HOSTS; read_idx++) {
    VitaChiakiHost *h = context.hosts[read_idx];
    if (!h)
      continue;
    if (write_idx != read_idx)
      context.hosts[write_idx] = h;
    write_idx++;
  }
  for (int i = write_idx; i < MAX_CONTEXT_HOSTS; i++)
    context.hosts[i] = NULL;
}

static bool mac_addr_is_zero(const MacAddr *mac) {
  if (!mac)
    return true;
  for (int i = 0; i < 6; i++) {
    if ((*mac)[i] != 0)
      return false;
  }
  return true;
}

bool mac_addrs_match(MacAddr *a, MacAddr *b) {
  for (int j = 0; j < 6; j++) {
    if ((*a)[j] != (*b)[j]) {
      return false;
    }
  }
  return true;
}

void save_manual_host(VitaChiakiHost *rhost, char *new_hostname) {
  if (!rhost || !new_hostname || !new_hostname[0]) {
    CHIAKI_LOGE(&(context.log), "Missing host or hostname; could not save manual host.");
    return;
  }

  if (mac_addr_is_zero(&(rhost->server_mac))) {
    CHIAKI_LOGE(&(context.log), "Missing host MAC; could not save manual host.");
    return;
  }

  if (context.config.num_manual_hosts >= MAX_MANUAL_HOSTS) {
    CHIAKI_LOGE(&(context.log), "Max manual hosts reached; could not save.");
    return;
  }

  for (int i = 0; i < context.config.num_manual_hosts; i++) {
    VitaChiakiHost *h = context.config.manual_hosts[i];
    if (!h)
      continue;
    if (mac_addrs_match(&(h->server_mac), &(rhost->server_mac))) {
      if (strcmp(h->hostname, new_hostname) == 0) {
        CHIAKI_LOGW(&(context.log), "Duplicate manual host. Not saving.");
        return;
      }
    }
  }

  VitaChiakiHost *newhost = (VitaChiakiHost *)calloc(1, sizeof(VitaChiakiHost));
  if (!newhost) {
    CHIAKI_LOGE(&(context.log), "Out of memory while saving manual host.");
    return;
  }
  copy_host(newhost, rhost, false);
  snprintf(newhost->hostname, sizeof(newhost->hostname), "%s", new_hostname);
  if (!newhost->display_name[0])
    snprintf(newhost->display_name, sizeof(newhost->display_name), "%s", new_hostname);
  newhost->type = REGISTERED | MANUALLY_ADDED;
  newhost->source = VITA_HOST_SOURCE_MANUAL_REMOTE;

  CHIAKI_LOGI(&(context.log), "--");
  CHIAKI_LOGI(&(context.log), "Adding manual host:");

  CHIAKI_LOGI(&(context.log), "Host Name (address):               %s", newhost->hostname);
  CHIAKI_LOGI(&(context.log), "Host MAC:                          %X%X%X%X%X%X\n",
              newhost->server_mac[0], newhost->server_mac[1], newhost->server_mac[2],
              newhost->server_mac[3], newhost->server_mac[4], newhost->server_mac[5]);
  CHIAKI_LOGI(&(context.log), "Is PS5:                            %s",
              chiaki_target_is_ps5(newhost->target) ? "true" : "false");

  CHIAKI_LOGI(&(context.log), "--");

  context.config.manual_hosts[context.config.num_manual_hosts++] = newhost;

  persist_config_or_warn();

  LOGD("> UPDATE CONTEXT...");
  update_context_hosts();
  LOGD("> UPDATE CONTEXT DONE");
}

void delete_manual_host(VitaChiakiHost *mhost) {
  for (int i = 0; i < context.config.num_manual_hosts; i++) {
    VitaChiakiHost *h = context.config.manual_hosts[i];
    if (h == mhost) {
      context.config.manual_hosts[i] = NULL;
    }
  }
  host_free(mhost);

  for (int i = 0; i < context.config.num_manual_hosts;) {
    VitaChiakiHost *h = context.config.manual_hosts[i];
    if (!h) {
      for (int j = i + 1; j < context.config.num_manual_hosts; j++) {
        context.config.manual_hosts[j - 1] = context.config.manual_hosts[j];
      }
      context.config.manual_hosts[context.config.num_manual_hosts - 1] = NULL;
      context.config.num_manual_hosts--;
      continue;
    }
    i++;
  }

  persist_config_or_warn();

  update_context_hosts();
}

void update_context_hosts() {
  bool hide_remote_if_discovered = true;
  /* Tracks whether any context.hosts[] slot was removed/freed below, so the console-card cache
   * gets invalidated once per call (not once per removal) -- see ui_cards_mark_dirty(). */
  bool hosts_removed = false;

  for (int host_idx = 0; host_idx < MAX_CONTEXT_HOSTS; host_idx++) {
    VitaChiakiHost *h = context.hosts[host_idx];
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
        hosts_removed = true;
      }
    }
  }

  if (hide_remote_if_discovered) {
    for (int i = 0; i < MAX_CONTEXT_HOSTS; i++) {
      VitaChiakiHost *mhost = context.hosts[i];
      if (!(mhost && (mhost->type & MANUALLY_ADDED) && !mac_addr_is_zero(&(mhost->server_mac))))
        continue;
      for (int j = 0; j < MAX_CONTEXT_HOSTS; j++) {
        if (j == i)
          continue;
        VitaChiakiHost *h = context.hosts[j];
        if (!(h && (h->type & DISCOVERED) && !(h->type & MANUALLY_ADDED) &&
              !mac_addr_is_zero(&(h->server_mac))))
          continue;
        if (mac_addrs_match(&(h->server_mac), &(mhost->server_mac))) {
          context.hosts[i] = NULL;
          hosts_removed = true;
        }
      }
    }
  }

  /* When a LAN-discovered host and a PSN_REMOTE host share the same MAC address they represent
   * the same physical console.  Merge by setting psn_remote_available on the discovered host
   * and removing the PSN_REMOTE entry so only a single card appears in the UI. */
  for (int i = 0; i < MAX_CONTEXT_HOSTS; i++) {
    VitaChiakiHost *psn = context.hosts[i];
    if (!(psn && psn->source == VITA_HOST_SOURCE_PSN_REMOTE &&
          !mac_addr_is_zero(&(psn->server_mac))))
      continue;
    for (int j = 0; j < MAX_CONTEXT_HOSTS; j++) {
      if (j == i)
        continue;
      VitaChiakiHost *disc = context.hosts[j];
      if (!(disc && (disc->type & DISCOVERED) && !mac_addr_is_zero(&(disc->server_mac))))
        continue;
      if (mac_addrs_match(&(disc->server_mac), &(psn->server_mac))) {
        disc->psn_remote_available = true;
        memcpy(disc->psn_device_uid, psn->psn_device_uid, sizeof(disc->psn_device_uid));
        if (host_in_active_use(psn)) {
          /* Don't free the actively-selected PSN host struct out from under a connect/stream
           * in progress. The merge re-runs on the next discovery callback and converges once
           * the connect settles and psn stops being active. */
          break;
        }
        /* Not actively in use: if psn is still the selected-but-idle active_host (e.g. it was
         * picked but no connect/stream ever started before the LAN discovery merge caught up),
         * clear the pointer before freeing so it doesn't dangle. */
        if (context.active_host == psn)
          context.active_host = NULL;
        LOGD(
            "update_context_hosts dedup: freeing PSN host ptr=%p hostname=%s, keeping LAN host "
            "ptr=%p hostname=%s",
            (void *)psn, psn->hostname[0] ? psn->hostname : "<null>", (void *)disc,
            disc->hostname[0] ? disc->hostname : "<null>");
        host_free(psn);
        context.hosts[i] = NULL;
        hosts_removed = true;
        break;
      }
    }
  }

  compact_context_hosts();

  for (int i = 0; i < context.config.num_manual_hosts; i++) {
    VitaChiakiHost *mhost = context.config.manual_hosts[i];
    if (!mhost || !mhost->hostname[0] || mac_addr_is_zero(&(mhost->server_mac)))
      continue;

    bool already_in_context = false;
    for (int host_idx = 0; host_idx < MAX_CONTEXT_HOSTS; host_idx++) {
      VitaChiakiHost *h = context.hosts[host_idx];
      if (!h)
        continue;
      if (!h->hostname[0])
        continue;
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
      VitaChiakiHost *h = context.hosts[host_idx];
      if (h == NULL) {
        context.hosts[host_idx] = mhost;
        added_to_context = true;
        break;
      }
    }

    if (!added_to_context) {
      CHIAKI_LOGE(&(context.log),
                  "Max # of hosts reached; could not add manual host %d to context.", i);
    }
  }

  for (int host_idx = 0; host_idx < MAX_CONTEXT_HOSTS; host_idx++) {
    VitaChiakiHost *h = context.hosts[host_idx];
    if (!h)
      continue;
    if ((h->type & REGISTERED) && !h->registered_state) {
      LOGE(
          "Context host integrity warning: registered host at slot %d missing registered_state "
          "(hostname=%s)",
          host_idx, h->hostname[0] ? h->hostname : "<null>");
    }
  }

  context.num_hosts = count_nonnull_context_hosts();

  if (hosts_removed)
    ui_cards_mark_dirty();
}

int count_manual_hosts_of_console(VitaChiakiHost *host) {
  if (!host)
    return 0;
  if (mac_addr_is_zero(&(host->server_mac)))
    return 0;
  int sum = 0;
  for (int i = 0; i < context.config.num_manual_hosts; i++) {
    VitaChiakiHost *mhost = context.config.manual_hosts[i];
    if (!mhost)
      continue;
    if (mac_addr_is_zero(&(mhost->server_mac)))
      continue;
    if (mac_addrs_match(&(host->server_mac), &(mhost->server_mac))) {
      sum++;
    }
  }
  return sum;
}

void copy_host(VitaChiakiHost *h_dest, VitaChiakiHost *h_src, bool copy_hostname) {
  h_dest->type = h_src->type;
  h_dest->source = h_src->source;
  h_dest->target = h_src->target;
  memcpy(&h_dest->server_mac, &(h_src->server_mac), 6);
  memcpy(&h_dest->psn_device_uid, &(h_src->psn_device_uid), sizeof(h_dest->psn_device_uid));
  h_dest->remoteplay_enabled = h_src->remoteplay_enabled;
  h_dest->psn_remote_available = h_src->psn_remote_available;

  // Full clear (not just a NUL at index 0) preserves the torn-read invariant on these
  // inline buffers: without it, a concurrent in-place snprintf (see discovery.c's
  // set_host_discovery_snapshot) writing a shorter string would leave stale bytes of the
  // previous longer string past the new NUL terminator; the memset prevents a torn read
  // from exposing them.
  memset(h_dest->hostname, 0, sizeof(h_dest->hostname));
  if (copy_hostname) {
    snprintf(h_dest->hostname, sizeof(h_dest->hostname), "%s", h_src->hostname);
  }
  memset(h_dest->display_name, 0, sizeof(h_dest->display_name));
  snprintf(h_dest->display_name, sizeof(h_dest->display_name), "%s", h_src->display_name);
  h_dest->discovery_state_snapshot = h_src->discovery_state_snapshot;
  h_dest->last_discovery_seen_us = h_src->last_discovery_seen_us;

  h_dest->registered_state = NULL;
  ChiakiRegisteredHost *rstate_src = h_src->registered_state;
  if (rstate_src) {
    ChiakiRegisteredHost *rstate_dest = malloc(sizeof(ChiakiRegisteredHost));
    if (!rstate_dest) {
      CHIAKI_LOGE(&(context.log), "Out of memory while copying registered host state");
    } else {
      h_dest->registered_state = rstate_dest;
      copy_host_registered_state(rstate_dest, rstate_src);
    }
  }

  // Nickname fallback tier (precedence documented in host.h:48-51: discovery host_name >
  // server_nickname > hostname/IP). h_src->display_name is empty for a registered host loaded
  // straight from config before this fix, and could still be empty for other future callers,
  // so honor the full precedence chain here rather than only at the discovery-thread callsite.
  if (!h_dest->display_name[0] && h_dest->registered_state &&
      h_dest->registered_state->server_nickname[0]) {
    snprintf(h_dest->display_name, sizeof(h_dest->display_name), "%s",
             h_dest->registered_state->server_nickname);
  }

  h_dest->discovery_state = NULL;
  // Full clear (not just a NUL at index 0) prevents a torn read from exposing stale bytes of
  // a previous longer string -- same invariant as hostname/display_name above: status_hint is
  // written off-thread (host_set_hint) and read by the UI thread.
  memset(h_dest->status_hint, 0, sizeof(h_dest->status_hint));
  sceClibSnprintf(h_dest->status_hint, sizeof(h_dest->status_hint), "%s", h_src->status_hint);
  h_dest->status_hint_is_error = h_src->status_hint_is_error;
  h_dest->status_hint_expire_us = h_src->status_hint_expire_us;
}

void copy_host_registered_state(ChiakiRegisteredHost *rstate_dest,
                                const ChiakiRegisteredHost *rstate_src) {
  if (rstate_src) {
    if (rstate_src->server_nickname) {
      strncpy(rstate_dest->server_nickname, rstate_src->server_nickname,
              sizeof(rstate_dest->server_nickname));
      rstate_dest->server_nickname[sizeof(rstate_dest->server_nickname) - 1] = '\0';
    } else {
      rstate_dest->server_nickname[0] = '\0';
    }
    rstate_dest->target = rstate_src->target;
    memcpy(rstate_dest->rp_key, rstate_src->rp_key, sizeof(rstate_dest->rp_key));
    rstate_dest->rp_key_type = rstate_src->rp_key_type;
    memcpy(rstate_dest->rp_regist_key, rstate_src->rp_regist_key,
           sizeof(rstate_dest->rp_regist_key));
  }
}
