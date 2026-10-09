// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

/* Unicast discovery probe for Pair new device > Enter IP address (issue #330).
 *
 * Threading: everything public runs on the UI thread. The only other thread is lib's one-shot
 * listener, which calls probe_reply_cb() once. That callback deep-copies the reply into
 * probe.reply under probe.mutex; the UI thread picks it up in discovery_probe_poll(). The
 * listener is always joined (discovery_probe_cancel / cleanup) before the mutex is destroyed,
 * so the callback never outlives the state it writes. */

#include "discovery_probe.h"

#include <chiaki/discovery.h>
#include <chiaki/thread.h>
#include <netinet/in.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "context.h"
#include "discovery.h"
#include "host.h"
#include "ip_address.h"
#include "util.h"

#define MICROSECONDS_PER_MILLISECOND 1000ULL
/// A discovery host_id is the console's MAC as 12 hex digits.
#define HOST_ID_MAC_HEX_LEN 12

typedef struct {
  DiscoveryProbeStatus status;
  char ip[IP_ADDRESS_STRING_SIZE];
  struct sockaddr_in target_addr;  ///< Address with the port filled in per packet.
  uint64_t start_us;
  uint64_t last_send_us;

  bool mutex_ready;
  bool discovery_ready;
  bool thread_ready;
  ChiakiMutex mutex;  ///< Guards reply.
  ChiakiDiscovery discovery;
  ChiakiDiscoveryThread thread;
  ChiakiDiscoveryHost *reply;  ///< Deep copy written by the listener thread; owned by the probe.

  VitaChiakiHost *host;  ///< Built on found; owned by the probe until taken.
} DiscoveryProbe;

static DiscoveryProbe probe;

/* Runs on lib's listener thread. `response` points into that thread's stack buffers, so it is
 * deep-copied here and only the copy is handed on. */
static void probe_reply_cb(ChiakiDiscoveryHost *response, void *user) {
  (void)user;
  ChiakiDiscoveryHost *copy = copy_discovery_host(response);
  if (!copy) {
    CHIAKI_LOGE(&(context.log), "Discovery probe %s: out of memory copying the reply", probe.ip);
    return;
  }
  chiaki_mutex_lock(&probe.mutex);
  if (probe.reply) {
    chiaki_mutex_unlock(&probe.mutex);
    destroy_discovery_host(copy);
    return;
  }
  probe.reply = copy;
  chiaki_mutex_unlock(&probe.mutex);
}

/* Stops the listener thread and closes the socket and mutex. Safe when nothing was started. */
static void release_network(void) {
  if (probe.thread_ready) {
    ChiakiErrorCode err = chiaki_discovery_thread_stop(&probe.thread);
    if (err != CHIAKI_ERR_SUCCESS)
      CHIAKI_LOGE(&(context.log), "Discovery probe %s: stopping the listener failed: %s", probe.ip,
                  chiaki_error_string(err));
    probe.thread_ready = false;
  }
  if (probe.discovery_ready) {
    chiaki_discovery_fini(&probe.discovery);
    probe.discovery_ready = false;
  }
  if (probe.mutex_ready) {
    chiaki_mutex_fini(&probe.mutex);
    probe.mutex_ready = false;
  }
}

/* Frees everything the probe owns (network, unread reply, untaken host) and resets the state
 * to IDLE. */
static void reset_probe(void) {
  release_network();
  if (probe.reply) {
    destroy_discovery_host(probe.reply);
    probe.reply = NULL;
  }
  if (probe.host) {
    discovery_probe_free_host(probe.host);
    probe.host = NULL;
  }
  memset(&probe, 0, sizeof(probe));
  probe.status = DISCOVERY_PROBE_IDLE;
}

static uint64_t now_us(void) {
  return sceKernelGetProcessTimeWide();
}

static DiscoveryProbeStatus fail(const char *what, ChiakiErrorCode err) {
  CHIAKI_LOGE(&(context.log), "Discovery probe %s: %s: %s", probe.ip, what,
              chiaki_error_string(err));
  release_network();
  probe.status = DISCOVERY_PROBE_ERROR;
  return probe.status;
}

/* Sends the SRCH packet to one port with one protocol version. */
static ChiakiErrorCode send_search(const char *protocol_version, uint16_t port) {
  ChiakiDiscoveryPacket packet = {0};
  packet.cmd = CHIAKI_DISCOVERY_CMD_SRCH;
  packet.protocol_version = (char *)protocol_version;
  probe.target_addr.sin_port = htons(port);
  return chiaki_discovery_send(&probe.discovery, &packet, (struct sockaddr *)&probe.target_addr,
                               sizeof(probe.target_addr));
}

/* Sends the PS4 and the PS5 search packets. Both are tried even if the first fails. */
static ChiakiErrorCode send_both_searches(void) {
  ChiakiErrorCode ps4_err =
      send_search(CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS4, CHIAKI_DISCOVERY_PORT_PS4);
  ChiakiErrorCode ps5_err =
      send_search(CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS5, CHIAKI_DISCOVERY_PORT_PS5);
  probe.last_send_us = now_us();
  return ps4_err != CHIAKI_ERR_SUCCESS ? ps4_err : ps5_err;
}

bool discovery_probe_start(const char *ip_text) {
  reset_probe();
  probe.status = DISCOVERY_PROBE_ERROR;

  uint8_t octets[IP_ADDRESS_OCTET_COUNT];
  if (!ip_address_parse(ip_text, octets)) {
    CHIAKI_LOGE(&(context.log), "Discovery probe: \"%s\" is not an IP address",
                ip_text ? ip_text : "<null>");
    return false;
  }
  ip_address_format(octets, probe.ip, sizeof(probe.ip));

  probe.target_addr.sin_family = AF_INET;
  probe.target_addr.sin_addr.s_addr =
      htonl(((uint32_t)octets[0] << 24) | ((uint32_t)octets[1] << 16) | ((uint32_t)octets[2] << 8) |
            (uint32_t)octets[3]);

  ChiakiErrorCode err = chiaki_mutex_init(&probe.mutex, false);
  if (err != CHIAKI_ERR_SUCCESS) {
    fail("creating the mutex failed", err);
    return false;
  }
  probe.mutex_ready = true;

  err = chiaki_discovery_init(&probe.discovery, &(context.log), AF_INET);
  if (err != CHIAKI_ERR_SUCCESS) {
    fail("opening the discovery socket failed", err);
    return false;
  }
  probe.discovery_ready = true;

  err =
      chiaki_discovery_thread_start_oneshot(&probe.thread, &probe.discovery, probe_reply_cb, NULL);
  if (err != CHIAKI_ERR_SUCCESS) {
    fail("starting the listener failed", err);
    return false;
  }
  probe.thread_ready = true;

  probe.start_us = now_us();
  CHIAKI_LOGI(&(context.log), "Discovery probe %s: started (deadline %d ms)", probe.ip,
              DISCOVERY_PROBE_DEADLINE_MS);
  err = send_both_searches();
  if (err != CHIAKI_ERR_SUCCESS) {
    fail("sending the search packet failed", err);
    return false;
  }
  probe.status = DISCOVERY_PROBE_SEARCHING;
  return true;
}

/* Copies the registered state of an already paired console with the same MAC into host, and
 * marks it REGISTERED, so the PIN screen can treat the probe result as a re-pair. */
static void adopt_registered_state(VitaChiakiHost *host) {
  for (size_t i = 0; i < context.config.num_registered_hosts; i++) {
    VitaChiakiHost *registered = context.config.registered_hosts[i];
    if (!registered || !mac_addrs_match(&(registered->server_mac), &(host->server_mac)))
      continue;
    if (!registered->registered_state) {
      CHIAKI_LOGW(&(context.log), "Discovery probe %s: registered host has no credentials",
                  probe.ip);
      return;
    }
    ChiakiRegisteredHost *state = calloc(1, sizeof(*state));
    if (!state) {
      CHIAKI_LOGE(&(context.log), "Discovery probe %s: out of memory copying registered state",
                  probe.ip);
      return;
    }
    copy_host_registered_state(state, registered->registered_state);
    host->registered_state = state;
    host->type |= REGISTERED;
    return;
  }
}

static bool mac_is_zero(const uint8_t *mac) {
  for (int i = 0; i < 6; i++)
    if (mac[i])
      return false;
  return true;
}

/* Builds the standalone host from the reply. Takes ownership of `reply` on success and on
 * failure (it is destroyed on failure). Returns NULL and logs when the reply is unusable. */
static VitaChiakiHost *build_host(ChiakiDiscoveryHost *reply) {
  if (!reply->host_id || strlen(reply->host_id) < HOST_ID_MAC_HEX_LEN) {
    CHIAKI_LOGE(&(context.log), "Discovery probe %s: reply has no usable host id", probe.ip);
    destroy_discovery_host(reply);
    return NULL;
  }
  VitaChiakiHost *host = calloc(1, sizeof(*host));
  if (!host) {
    CHIAKI_LOGE(&(context.log), "Discovery probe %s: out of memory building the host", probe.ip);
    destroy_discovery_host(reply);
    return NULL;
  }
  parse_mac(reply->host_id, host->server_mac);
  if (mac_is_zero(host->server_mac)) {
    CHIAKI_LOGE(&(context.log), "Discovery probe %s: reply host id \"%s\" is not a MAC", probe.ip,
                reply->host_id);
    destroy_discovery_host(reply);
    free(host);
    return NULL;
  }

  host->source = VITA_HOST_SOURCE_LOCAL_DISCOVERY;
  host->target = chiaki_discovery_host_system_version_target(reply);
  snprintf(host->hostname, sizeof(host->hostname), "%s", probe.ip);
  snprintf(host->display_name, sizeof(host->display_name), "%s",
           (reply->host_name && reply->host_name[0]) ? reply->host_name : probe.ip);
  host->discovery_state = reply;
  host->discovery_state_snapshot = reply->state;
  host->last_discovery_seen_us = now_us();
  adopt_registered_state(host);
  return host;
}

DiscoveryProbeStatus discovery_probe_poll(void) {
  if (probe.status != DISCOVERY_PROBE_SEARCHING)
    return probe.status;

  chiaki_mutex_lock(&probe.mutex);
  ChiakiDiscoveryHost *reply = probe.reply;
  probe.reply = NULL;
  chiaki_mutex_unlock(&probe.mutex);

  if (reply) {
    release_network();
    probe.host = build_host(reply);
    if (!probe.host) {
      probe.status = DISCOVERY_PROBE_ERROR;
      return probe.status;
    }
    CHIAKI_LOGI(&(context.log), "Discovery probe %s: found \"%s\" (%s)", probe.ip,
                probe.host->display_name, chiaki_target_is_ps5(probe.host->target) ? "PS5" : "PS4");
    probe.status = DISCOVERY_PROBE_FOUND;
    return probe.status;
  }

  uint64_t now = now_us();
  if (now - probe.start_us >= DISCOVERY_PROBE_DEADLINE_MS * MICROSECONDS_PER_MILLISECOND) {
    CHIAKI_LOGI(&(context.log), "Discovery probe %s: not found after %d ms", probe.ip,
                DISCOVERY_PROBE_DEADLINE_MS);
    release_network();
    probe.status = DISCOVERY_PROBE_NOT_FOUND;
    return probe.status;
  }

  if (now - probe.last_send_us >= DISCOVERY_PROBE_RESEND_MS * MICROSECONDS_PER_MILLISECOND) {
    CHIAKI_LOGD(&(context.log), "Discovery probe %s: resending", probe.ip);
    ChiakiErrorCode err = send_both_searches();
    if (err != CHIAKI_ERR_SUCCESS)
      return fail("resending the search packet failed", err);
  }
  return probe.status;
}

void discovery_probe_cancel(void) {
  if (probe.status != DISCOVERY_PROBE_IDLE)
    CHIAKI_LOGI(&(context.log), "Discovery probe %s: cancelled", probe.ip);
  reset_probe();
}

VitaChiakiHost *discovery_probe_take_host(void) {
  if (probe.status != DISCOVERY_PROBE_FOUND || !probe.host)
    return NULL;
  VitaChiakiHost *host = probe.host;
  probe.host = NULL;
  reset_probe();
  return host;
}

void discovery_probe_free_host(VitaChiakiHost *host) {
  if (!host)
    return;
  if (context.active_host == host)
    context.active_host = NULL;
  host_free(host);
  free(host);
}

void discovery_probe_save_manual_host(VitaChiakiHost *host) {
  if (!host || !host->hostname[0]) {
    CHIAKI_LOGE(&(context.log), "Discovery probe: no host to save as a manual host");
    return;
  }
  save_manual_host(host, host->hostname);
}
