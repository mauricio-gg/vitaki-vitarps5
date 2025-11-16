#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <chiaki/discoveryservice.h>
#include <chiaki/log.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <psp2/kernel/processmgr.h>

#include "discovery.h"
#include "context.h"
#include "host.h"
#include "util.h"

/// Allow some grace time before removing a flapping host
#define DISCOVERY_LOST_GRACE_US (3 * 1000 * 1000ULL)

/// Save a newly discovered host into the context
// Returns the index in context.hosts where it is saved (-1 if not saved)
int save_discovered_host(ChiakiDiscoveryHost* host) {
  CHIAKI_LOGI(&(context.log), "Saving discovered host...");
  uint64_t now_us = sceKernelGetProcessTimeWide();
  // Check if the host is already known, and if not, locate a free spot for it
  uint8_t host_mac[6];
  parse_mac(host->host_id, host_mac);

  // Check if there is an identical discovered host in context; return if so
  for (int host_idx = 0; host_idx < MAX_NUM_HOSTS; host_idx++) {
    VitaChiakiHost* h = context.hosts[host_idx];
    if (h && (h->type & DISCOVERED)) {
      if (mac_addrs_match(&(h->server_mac), &host_mac)) {
        // Already known discovered host. Just copy the discovery state and exit.
        if (h->discovery_state && (h->discovery_state != host)) {
          free(h->discovery_state);
        }
        h->discovery_state =
            (ChiakiDiscoveryHost*)malloc(sizeof(ChiakiDiscoveryHost));
        memcpy(h->discovery_state, host, sizeof(ChiakiDiscoveryHost));
        if (h->hostname) free(h->hostname);
        h->hostname = strdup(h->discovery_state->host_addr);
        h->last_discovery_seen_us = now_us;
        return host_idx;
      }
    }
  }

  // Determine whether there is room in context for a new host to be added
  int target_idx = -1;
  for (int host_idx = 0; host_idx < MAX_NUM_HOSTS; host_idx++) {
    VitaChiakiHost* h = context.hosts[host_idx];
    if (h == NULL) {
      target_idx = host_idx;
      break;
    } else if (h->type & MANUALLY_ADDED) {
      if (mac_addrs_match(&(h->server_mac), &host_mac)) {
        // Manually added host matched this discovered host, so there is space available
        target_idx = host_idx;
        break;
      }
    }
  }

  // Maximum number of hosts reached, can't save host
  // TODO: Indicate to user
  if (target_idx < 0) {
    CHIAKI_LOGE(&(context.log), "Max # of hosts reached; could not save newly discovered host.");
    return -1;
  }

  // print some info about the host

  CHIAKI_LOGI(&(context.log), "--");
  CHIAKI_LOGI(&(context.log), "Discovered Host:");
  CHIAKI_LOGI(&(context.log), "State:                             %s", chiaki_discovery_host_state_string(host->state));

  if(host->system_version)
    CHIAKI_LOGI(&(context.log), "System Version:                    %s", host->system_version);

  if(host->device_discovery_protocol_version)
    CHIAKI_LOGI(&(context.log), "Device Discovery Protocol Version: %s", host->device_discovery_protocol_version);

  if(host->host_request_port)
    CHIAKI_LOGI(&(context.log), "Request Port:                      %hu", (unsigned short)host->host_request_port);

  if(host->host_name)
    CHIAKI_LOGI(&(context.log), "Host Name:                         %s", host->host_name);

  if(host->host_type)
    CHIAKI_LOGI(&(context.log), "Host Type:                         %s", host->host_type);

  if(host->host_id)
    CHIAKI_LOGI(&(context.log), "Host ID:                           %s", host->host_id);

  if(host->running_app_titleid)
    CHIAKI_LOGI(&(context.log), "Running App Title ID:              %s", host->running_app_titleid);

  if(host->running_app_name)
    CHIAKI_LOGI(&(context.log), "Running App Name:                  %s%s", host->running_app_name, (strcmp(host->running_app_name, "Persona 5") == 0 ? " (best game ever)" : ""));


  VitaChiakiHost* h = (VitaChiakiHost*)malloc(sizeof(VitaChiakiHost));
  h->registered_state = NULL;
  h->type = DISCOVERED;

  ChiakiTarget target = chiaki_discovery_host_system_version_target(host);
  CHIAKI_LOGI(&(context.log),   "Is PS5:                            %s", chiaki_target_is_ps5(target) ? "true" : "false");
  h->target = target;
  memcpy(&(h->server_mac), &host_mac, 6);
  h->discovery_state =
      (ChiakiDiscoveryHost*)malloc(sizeof(ChiakiDiscoveryHost));
  memcpy(h->discovery_state, host, sizeof(ChiakiDiscoveryHost));
  h->hostname = strdup(h->discovery_state->host_addr);
  h->last_discovery_seen_us = now_us;

  CHIAKI_LOGI(&(context.log), "--");

  // Check if the newly discovered host is a known registered one
  for (int rhost_idx = 0; rhost_idx < context.config.num_registered_hosts; rhost_idx++) {
    VitaChiakiHost* rhost =
        context.config.registered_hosts[rhost_idx];
    if (rhost == NULL) {
      continue;
    }
    /*CHIAKI_LOGI(&(context.log), "  Checking rhost %d for match", rhost_idx);
    CHIAKI_LOGI(&(context.log), "  %X%X%X%X%X%X (r) vs %X%X%X%X%X%X\n",
                rhost->server_mac[0], rhost->server_mac[1], rhost->server_mac[2], rhost->server_mac[3], rhost->server_mac[4], rhost->server_mac[5],
                h->server_mac[0], h->server_mac[1], h->server_mac[2], h->server_mac[3], h->server_mac[4], h->server_mac[5]
                );*/

    if (mac_addrs_match(&(rhost->server_mac), &(h->server_mac))) {
      CHIAKI_LOGI(&(context.log), "Found registered host (%s) matching discovered host (%s).",
                  rhost->registered_state->server_nickname,
                  h->discovery_state->host_name
                  );
      h->type |= REGISTERED;

      // copy registered state
      h->registered_state = NULL;
      if (rhost->registered_state) {
        h->registered_state = malloc(sizeof(ChiakiRegisteredHost));;
        copy_host_registered_state(h->registered_state, rhost->registered_state);
      }

      break;
    }
  }

  // Add to context
  if (!context.hosts[target_idx]) context.num_hosts++;
  context.hosts[target_idx] = h;

  update_context_hosts(); // to remove any extra manual host copies

  return target_idx;
}

// remove discovered hosts if they haven't been seen for longer than the grace window
static void remove_lost_discovered_hosts(void) {
  uint64_t now_us = sceKernelGetProcessTimeWide();
  for (int host_idx = 0; host_idx < MAX_NUM_HOSTS; host_idx++) {
    VitaChiakiHost* h = context.hosts[host_idx];
    if (h && (h->type & DISCOVERED)) {
      bool active_streaming = (context.active_host == h) &&
                              (context.stream.session_init ||
                               context.stream.is_streaming);
      if (active_streaming)
        continue;

      uint64_t last_seen = h->last_discovery_seen_us;
      if (last_seen == 0 ||
          now_us - last_seen < DISCOVERY_LOST_GRACE_US)
        continue;

      uint64_t stale_ms = (now_us - last_seen) / 1000;
      CHIAKI_LOGI(&(context.log),
                  "Removing lost host from context (idx %d, stale %llums)",
                  host_idx,
                  (unsigned long long)stale_ms);
      // free and remove from context
      host_free(h);
      context.hosts[host_idx] = NULL;
    }
  }

  update_context_hosts();
}

/// Called whenever new hosts are discovered
void discovery_cb(ChiakiDiscoveryHost* hosts, size_t hosts_count, void* user) {
  for (int dhost_idx = 0; dhost_idx < hosts_count; dhost_idx++) {
    save_discovered_host(&hosts[dhost_idx]);
  }

  remove_lost_discovered_hosts();

  // Call caller-defined callback
  VitaChiakiDiscoveryCallbackState* cb_state =
      (VitaChiakiDiscoveryCallbackState*)user;
  if (cb_state && cb_state->cb) {
    cb_state->cb(cb_state->cb_user);
  }
}

/// Initiate the Chiaki discovery thread
ChiakiErrorCode start_discovery(VitaChiakiDiscoveryCb cb, void* cb_user) {
  if (context.discovery_enabled) {
    return CHIAKI_ERR_SUCCESS;
  }
  if (cb != NULL) {
    context.discovery_cb_state =
        malloc(sizeof(VitaChiakiDiscoveryCallbackState));
    context.discovery_cb_state->cb = cb;
    context.discovery_cb_state->cb_user = cb_user;
  }
  ChiakiDiscoveryServiceOptions opts;
  opts.cb = discovery_cb;
  opts.cb_user = context.discovery_cb_state;
  opts.ping_ms = 500;
  opts.ping_initial_ms = opts.ping_ms;
  opts.hosts_max = MAX_NUM_HOSTS;
  opts.host_drop_pings = HOST_DROP_PINGS;

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_BROADCAST;
  opts.send_addr = (struct sockaddr*) &addr;
  opts.send_addr_size = sizeof(addr);
  opts.send_host = NULL;

  ChiakiErrorCode err = chiaki_discovery_service_init(&(context.discovery),
                                                      &opts, &(context.log));
  context.discovery_enabled = true;

  update_context_hosts();

  return err;
}

/// Terminate the Chiaki discovery thread, clean up discovey state in context
void stop_discovery(bool keep_hosts) {
  if (!context.discovery_enabled) {
    return;
  }
  chiaki_discovery_service_fini(&(context.discovery));
  context.discovery_enabled = false;
  if (!keep_hosts) {
    for (int i = 0; i < MAX_NUM_HOSTS; i++) {
      VitaChiakiHost* h = context.hosts[i];
      if (h == NULL) {
        continue;
      }
      if (!(h->type & MANUALLY_ADDED)) {
        host_free(h);
        context.hosts[i] = NULL;
        context.num_hosts--;
      } else if (h->type & DISCOVERED) {
        free(h->discovery_state);
        h->discovery_state = NULL;
      }
    }
  }
  if (context.discovery_cb_state != NULL) {
    free(context.discovery_cb_state);
    context.discovery_cb_state = NULL;
  }

  update_context_hosts();
}
