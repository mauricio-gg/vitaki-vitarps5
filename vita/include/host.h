#pragma once
#include <stdint.h>
#include <stdbool.h>

#include <chiaki/common.h>
#include <chiaki/discovery.h>
#include <chiaki/regist.h>

// Host limits for multi-console discovery
#define MAX_REGISTERED_HOSTS 8  // Persisted paired consoles
#define MAX_MANUAL_HOSTS 8      // Manually-added by IP
#define MAX_CONTEXT_HOSTS 64    // Display array (discovered + manual)
// Deprecated: use specific constants above
#define MAX_NUM_HOSTS MAX_CONTEXT_HOSTS
#define HOST_DROP_PINGS 3;

// Inline string storage sizes for VitaChiakiHost (see hostname/display_name below).
#define VITA_HOST_HOSTNAME_LEN 256
#define VITA_HOST_DISPLAY_NAME_LEN 64

typedef enum vita_chiaki_host_type_t {
  DISCOVERED = 0x01,
  MANUALLY_ADDED = 0x02,
  REGISTERED = 0x04,
} VitaChiakiHostType;

typedef enum vita_chiaki_host_source_t {
  VITA_HOST_SOURCE_LOCAL_DISCOVERY = 0,
  VITA_HOST_SOURCE_MANUAL_REMOTE = 1,
  VITA_HOST_SOURCE_PSN_REMOTE = 2,
} VitaChiakiHostSource;

typedef struct vita_chiaki_host_t {
  VitaChiakiHostType type;
  VitaChiakiHostSource source;
  ChiakiTarget target;
  uint8_t server_mac[6];
  uint8_t psn_device_uid[32];
  bool remoteplay_enabled;
  /** True when a PSN_REMOTE entry for this physical console was found and merged here. */
  bool psn_remote_available;
  /* Inline (not heap-allocated) so the UI thread can safely read a torn-but-terminated
   * string at worst, instead of a live pointer the discovery thread may free/re-strdup
   * concurrently. The string members owned by `discovery_state` below (host_name, host_addr,
   * etc.) must never be dereferenced outside the discovery thread that writes them -- use
   * these snapshot fields instead. `registered_state` is different: it is safe to read from
   * any thread as long as callers follow the struct-leak + NULL-check discipline documented
   * on host_free() (host.c) -- e.g. host_stream()/host_wakeup() legitimately dereference it. */
  char hostname[VITA_HOST_HOSTNAME_LEN];
  /** Best-known console name for UI display. Precedence: discovery host_name >
   * registered_state->server_nickname > hostname/IP. Set at host-creation time by whichever
   * code builds the entry (manual add, PSN refresh, config load); refreshed thereafter only by
   * the discovery thread for hosts that are actively DISCOVERED. */
  char display_name[VITA_HOST_DISPLAY_NAME_LEN];
  /** Snapshot of discovery_state->state, safe to read from any thread. */
  ChiakiDiscoveryHostState discovery_state_snapshot;
  uint64_t last_discovery_seen_us;

  ChiakiDiscoveryHost *discovery_state;
  ChiakiRegisteredHost *registered_state;

  char status_hint[96];
  uint64_t status_hint_expire_us;
  bool status_hint_is_error;
} VitaChiakiHost;

typedef uint8_t MacAddr[6];

void host_free(VitaChiakiHost *host);
/* True while a connect/stream is actively using this host's memory (connection worker alive,
 * session initialized, connect overlay up, or a packet-loss auto-retry is scheduled/in flight
 * -- see the full rationale on the definition in host.c). Free-guards must use this, not bare
 * context.active_host identity: selecting a host never clears context.active_host on its own
 * (only the free paths that call this helper do, and only once the host is no longer in use),
 * so identity alone would make a guard permanent instead of transient. */
bool host_in_active_use(const VitaChiakiHost *host);
int host_register(VitaChiakiHost *host, int pin);
int host_wakeup(VitaChiakiHost *host);
int host_stream(VitaChiakiHost *host);
void host_cancel_stream_request(void);
void host_finalize_deferred_session(void);
bool mac_addrs_match(MacAddr *a, MacAddr *b);
void save_manual_host(VitaChiakiHost *rhost, char *new_hostname);
void delete_manual_host(VitaChiakiHost *mhost);
void update_context_hosts();
int count_manual_hosts_of_console(VitaChiakiHost *host);
void copy_host(VitaChiakiHost *h_dest, VitaChiakiHost *h_src, bool copy_hostname);
void copy_host_registered_state(ChiakiRegisteredHost *rstate_dest,
                                const ChiakiRegisteredHost *rstate_src);
