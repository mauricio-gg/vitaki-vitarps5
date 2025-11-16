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
