#pragma once
#include <chiaki/discovery.h>

/// Called whenever a new host has been discovered
typedef void (*VitaChiakiDiscoveryCb)(void* user);

/// Internal data structure that packages a user-specified callback
/// along with its data
typedef struct vita_chiaki_discovery_callback_state_t {
  VitaChiakiDiscoveryCb cb;
  void* cb_user;
} VitaChiakiDiscoveryCallbackState;

ChiakiErrorCode start_discovery(VitaChiakiDiscoveryCb cb, void* cb_user);
void stop_discovery(bool keep_hosts);

/// Deep-copy a ChiakiDiscoveryHost so the string fields remain valid
ChiakiDiscoveryHost* copy_discovery_host(const ChiakiDiscoveryHost* src);

/// Free a discovery host created by copy_discovery_host()
void destroy_discovery_host(ChiakiDiscoveryHost* host);
