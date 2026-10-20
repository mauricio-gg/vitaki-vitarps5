#pragma once

/* Host-build stand-in for vita/include/context.h, used by test/manual_host_discovery_tests.c.
 * It carries only the fields discovery.c, host_storage.c and config_hosts.c read, and the real
 * VitaChiakiConfig, so the code under test sees the real host and manual-host structures. */

#include <psp2/kernel/clib.h>
#include <chiaki/discoveryservice.h>
#include <chiaki/log.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "config.h"
#include "discovery.h"
#include "host.h"

typedef struct vita_chiaki_context_t {
  ChiakiLog log;
  VitaChiakiConfig config;
  VitaChiakiHost *hosts[MAX_CONTEXT_HOSTS];
  int num_hosts;
  VitaChiakiHost *active_host;
  bool discovery_enabled;
  ChiakiDiscoveryService discovery;
  VitaChiakiDiscoveryCallbackState *discovery_cb_state;
} VitaChiakiContext;

extern VitaChiakiContext context;

#define LOGD(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#define LOGW(fmt, ...) fprintf(stderr, "[WARN] " fmt "\n", ##__VA_ARGS__)
