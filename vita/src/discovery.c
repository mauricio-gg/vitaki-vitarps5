#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <chiaki/discoveryservice.h>
#include <chiaki/log.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <psp2/kernel/processmgr.h>

#include "discovery.h"
#include "context.h"
#include "host.h"
#include "util.h"
#include "ui/ui_console_cards.h"

/// Allow some grace time before removing a flapping host
#define DISCOVERY_LOST_GRACE_US (3 * 1000 * 1000ULL)

/* Single writer for VitaChiakiHost's inline hostname/display_name/discovery_state_snapshot
 * fields: this always runs on the Chiaki discovery-service thread. snprintf-in-place (no
 * malloc/free) means any other thread reading these fields concurrently sees a torn-but-
 * NUL-terminated string at worst, never a dangling heap pointer. */
static bool set_host_discovery_snapshot(VitaChiakiHost *host_entry,
                                        const ChiakiDiscoveryHost *discovery_host,
                                        uint64_t now_us) {
  ChiakiDiscoveryHost *new_state = copy_discovery_host(discovery_host);
  if (!new_state) {
    CHIAKI_LOGE(&(context.log), "Failed to cache discovery state (out of memory)");
    return false;
  }

  if (host_entry->discovery_state) {
    destroy_discovery_host(host_entry->discovery_state);
    host_entry->discovery_state = NULL;
  }
  host_entry->discovery_state = new_state;
  host_entry->discovery_state_snapshot = new_state->state;

  if (new_state->host_addr)
    snprintf(host_entry->hostname, sizeof(host_entry->hostname), "%s", new_state->host_addr);
  else
    host_entry->hostname[0] = '\0';

  /* display_name precedence: discovery host_name > registered_state->server_nickname >
   * hostname/IP. If none of those are available this cycle, keep whatever name was
   * already known rather than blanking a good display_name for a transient gap. */
  if (new_state->host_name && new_state->host_name[0]) {
    snprintf(host_entry->display_name, sizeof(host_entry->display_name), "%s",
             new_state->host_name);
  } else if (host_entry->registered_state && host_entry->registered_state->server_nickname[0]) {
    snprintf(host_entry->display_name, sizeof(host_entry->display_name), "%s",
             host_entry->registered_state->server_nickname);
  } else if (host_entry->hostname[0]) {
    snprintf(host_entry->display_name, sizeof(host_entry->display_name), "%s",
             host_entry->hostname);
  }

  host_entry->last_discovery_seen_us = now_us;
  return true;
}

static int find_discovered_host_index_by_mac(MacAddr *host_mac) {
  for (int host_idx = 0; host_idx < MAX_CONTEXT_HOSTS; host_idx++) {
    VitaChiakiHost *host_entry = context.hosts[host_idx];
    if (host_entry && (host_entry->type & DISCOVERED) &&
        mac_addrs_match(&(host_entry->server_mac), host_mac)) {
      return host_idx;
    }
  }
  return -1;
}

static int find_target_slot_for_discovered_host(MacAddr *host_mac) {
  for (int host_idx = 0; host_idx < MAX_CONTEXT_HOSTS; host_idx++) {
    VitaChiakiHost *host_entry = context.hosts[host_idx];
    if (host_entry == NULL)
      return host_idx;
    if ((host_entry->type & MANUALLY_ADDED) &&
        mac_addrs_match(&(host_entry->server_mac), host_mac)) {
      return host_idx;
    }
  }
  return -1;
}

static void log_discovered_host_details(const ChiakiDiscoveryHost *host) {
  CHIAKI_LOGI(&(context.log), "--");
  CHIAKI_LOGI(&(context.log), "Discovered Host:");
  CHIAKI_LOGI(&(context.log), "State:                             %s",
              chiaki_discovery_host_state_string(host->state));

  if (host->system_version)
    CHIAKI_LOGI(&(context.log), "System Version:                    %s", host->system_version);
  if (host->device_discovery_protocol_version)
    CHIAKI_LOGI(&(context.log), "Device Discovery Protocol Version: %s",
                host->device_discovery_protocol_version);
  if (host->host_request_port)
    CHIAKI_LOGI(&(context.log), "Request Port:                      %hu",
                (unsigned short)host->host_request_port);
  if (host->host_name)
    CHIAKI_LOGI(&(context.log), "Host Name:                         %s", host->host_name);
  if (host->host_type)
    CHIAKI_LOGI(&(context.log), "Host Type:                         %s", host->host_type);
  if (host->host_id)
    CHIAKI_LOGI(&(context.log), "Host ID:                           %s", host->host_id);
  if (host->running_app_titleid)
    CHIAKI_LOGI(&(context.log), "Running App Title ID:              %s", host->running_app_titleid);
  if (host->running_app_name)
    CHIAKI_LOGI(&(context.log), "Running App Name:                  %s%s", host->running_app_name,
                (strcmp(host->running_app_name, "Persona 5") == 0 ? " (best game ever)" : ""));
}

/// Save a newly discovered host into the context
// Returns the index in context.hosts where it is saved (-1 if not saved)
int save_discovered_host(ChiakiDiscoveryHost *host) {
  CHIAKI_LOGI(&(context.log), "Saving discovered host...");
  uint64_t now_us = sceKernelGetProcessTimeWide();
  uint8_t host_mac[6];
  parse_mac(host->host_id, host_mac);

  int existing_idx = find_discovered_host_index_by_mac(&host_mac);
  if (existing_idx >= 0) {
    VitaChiakiHost *existing = context.hosts[existing_idx];
    if (existing && (existing->type & REGISTERED) && !existing->registered_state) {
      LOGE(
          "Discovery integrity warning: existing registered host missing registered_state (idx=%d "
          "host=%s)",
          existing_idx, existing->hostname[0] ? existing->hostname : "<null>");
    }
    set_host_discovery_snapshot(context.hosts[existing_idx], host, now_us);
    return existing_idx;
  }

  int target_idx = find_target_slot_for_discovered_host(&host_mac);
  if (target_idx < 0) {
    CHIAKI_LOGE(&(context.log), "Max # of hosts reached; could not save newly discovered host.");
    return -1;
  }

  // print some info about the host
  log_discovered_host_details(host);

  VitaChiakiHost *h = context.hosts[target_idx];
  bool reuse_existing = h != NULL;
  if (!h) {
    h = (VitaChiakiHost *)calloc(1, sizeof(VitaChiakiHost));
    if (!h) {
      CHIAKI_LOGE(&(context.log), "Failed to allocate host entry");
      return -1;
    }
    h->status_hint[0] = '\0';
    h->status_hint_expire_us = 0;
    h->status_hint_is_error = false;
  }
  h->type |= DISCOVERED;
  h->source = VITA_HOST_SOURCE_LOCAL_DISCOVERY;
  h->remoteplay_enabled = false;
  memset(h->psn_device_uid, 0, sizeof(h->psn_device_uid));

  ChiakiTarget target = chiaki_discovery_host_system_version_target(host);
  CHIAKI_LOGI(&(context.log), "Is PS5:                            %s",
              chiaki_target_is_ps5(target) ? "true" : "false");
  h->target = target;
  memcpy(&(h->server_mac), &host_mac, 6);
  if (!set_host_discovery_snapshot(h, host, now_us)) {
    if (!reuse_existing)
      free(h);
    return -1;
  }

  CHIAKI_LOGI(&(context.log), "--");

  // Check if the newly discovered host is a known registered one
  for (int rhost_idx = 0; rhost_idx < context.config.num_registered_hosts; rhost_idx++) {
    VitaChiakiHost *rhost = context.config.registered_hosts[rhost_idx];
    if (rhost == NULL) {
      continue;
    }

    if (mac_addrs_match(&(rhost->server_mac), &(h->server_mac))) {
      CHIAKI_LOGI(&(context.log), "Found registered host (%s) matching discovered host (%s).",
                  (rhost->registered_state && rhost->registered_state->server_nickname[0])
                      ? rhost->registered_state->server_nickname
                      : "<unknown>",
                  h->display_name);
      if (rhost->registered_state) {
        ChiakiRegisteredHost *new_state = calloc(1, sizeof(ChiakiRegisteredHost));
        if (new_state) {
          copy_host_registered_state(new_state, rhost->registered_state);
          if (h->registered_state)
            free(h->registered_state);
          h->registered_state = new_state;
          h->type |= REGISTERED;
        } else {
          CHIAKI_LOGE(&(context.log), "Failed to allocate registered host state");
        }
      } else if (h->registered_state) {
        h->type |= REGISTERED;
      } else {
        h->type &= ~REGISTERED;
        CHIAKI_LOGW(&(context.log), "Registered host match missing credential state for %s",
                    h->hostname[0] ? h->hostname : "<null>");
      }

      break;
    }
  }

  if ((h->type & REGISTERED) && !h->registered_state) {
    LOGE(
        "Discovery integrity warning: discovered registered host missing credential state "
        "(host=%s)",
        h->hostname[0] ? h->hostname : "<null>");
  }

  // Add to context
  if (!context.hosts[target_idx])
    context.num_hosts++;
  context.hosts[target_idx] = h;

  update_context_hosts();  // to remove any extra manual host copies

  return target_idx;
}

// remove discovered hosts if they haven't been seen for longer than the grace window
static void remove_lost_discovered_hosts(void) {
  uint64_t now_us = sceKernelGetProcessTimeWide();
  /* Tracks whether any host was actually freed below, so the console-card cache gets
   * invalidated once per call (not once per loop iteration) -- see ui_cards_mark_dirty(). */
  bool hosts_removed = false;
  for (int host_idx = 0; host_idx < MAX_CONTEXT_HOSTS; host_idx++) {
    VitaChiakiHost *h = context.hosts[host_idx];
    if (h && (h->type & DISCOVERED)) {
      /* Never free the actively-selected host struct out from under a connect/stream in
       * progress. host_in_active_use() (host.c) supersedes the old active_streaming/
       * connection_overlay_guard split: it covers the connection worker thread's whole
       * host_stream() span (superset of session_init/is_streaming), the connect overlay, and
       * -- unlike the predicates that used to live here -- the host_quit.c packet-loss
       * auto-retry window too, which runs on a different thread with neither of the other
       * flags set. Unlike the old unconditional "context.active_host == h" skip this replaced,
       * the guard is transient: it clears once the connect/stream/retry actually ends, so a
       * once-selected host that later goes offline is still reaped. */
      if (host_in_active_use(h))
        continue;

      uint64_t last_seen = h->last_discovery_seen_us;
      if (last_seen == 0 || now_us - last_seen < DISCOVERY_LOST_GRACE_US)
        continue;

      uint64_t stale_ms = (now_us - last_seen) / 1000;
      CHIAKI_LOGI(&(context.log), "Removing lost host from context (idx %d, stale %llums)",
                  host_idx, (unsigned long long)stale_ms);
      if (context.active_host == h)
        context.active_host = NULL;
      // free and remove from context
      host_free(h);
      context.hosts[host_idx] = NULL;
      hosts_removed = true;
    }
  }

  if (hosts_removed)
    ui_cards_mark_dirty();

  update_context_hosts();
}

static void invoke_discovery_callback(void *user) {
  VitaChiakiDiscoveryCallbackState *cb_state = (VitaChiakiDiscoveryCallbackState *)user;
  if (cb_state && cb_state->cb)
    cb_state->cb(cb_state->cb_user);
}

/// Called whenever new hosts are discovered
void discovery_cb(ChiakiDiscoveryHost *hosts, size_t hosts_count, void *user) {
  for (int dhost_idx = 0; dhost_idx < hosts_count; dhost_idx++) {
    save_discovered_host(&hosts[dhost_idx]);
  }

  remove_lost_discovered_hosts();
  invoke_discovery_callback(user);
}

/// Initiate the Chiaki discovery thread
ChiakiErrorCode start_discovery(VitaChiakiDiscoveryCb cb, void *cb_user) {
  if (context.discovery_enabled) {
    return CHIAKI_ERR_SUCCESS;
  }
  if (cb != NULL) {
    context.discovery_cb_state = malloc(sizeof(VitaChiakiDiscoveryCallbackState));
    if (!context.discovery_cb_state) {
      CHIAKI_LOGE(&(context.log), "Failed to allocate discovery callback state");
      return CHIAKI_ERR_MEMORY;
    }
    context.discovery_cb_state->cb = cb;
    context.discovery_cb_state->cb_user = cb_user;
  }
  ChiakiDiscoveryServiceOptions opts;
  opts.cb = discovery_cb;
  opts.cb_user = context.discovery_cb_state;
  opts.ping_ms = 500;
  opts.ping_initial_ms = opts.ping_ms;
  opts.hosts_max = MAX_CONTEXT_HOSTS;
  opts.host_drop_pings = HOST_DROP_PINGS;

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_BROADCAST;
  opts.send_addr = (struct sockaddr_in6 *)(void *)&addr;
  opts.send_addr_size = sizeof(addr);
  opts.send_host = NULL;

  ChiakiErrorCode err = chiaki_discovery_service_init(&(context.discovery), &opts, &(context.log));
  if (err != CHIAKI_ERR_SUCCESS) {
    if (context.discovery_cb_state != NULL) {
      free(context.discovery_cb_state);
      context.discovery_cb_state = NULL;
    }
    return err;
  }
  context.discovery_enabled = true;

  update_context_hosts();

  return err;
}

/// Terminate the Chiaki discovery thread, clean up discovey state in context
void stop_discovery(void) {
  if (!context.discovery_enabled) {
    return;
  }
  chiaki_discovery_service_fini(&(context.discovery));
  context.discovery_enabled = false;
  if (context.discovery_cb_state != NULL) {
    free(context.discovery_cb_state);
    context.discovery_cb_state = NULL;
  }

  update_context_hosts();
}
