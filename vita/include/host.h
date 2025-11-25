#pragma once
#include <stdint.h>
#include <stdbool.h>

#include <chiaki/common.h>
#include <chiaki/discovery.h>
#include <chiaki/regist.h>

#define MAX_NUM_HOSTS 4
#define HOST_DROP_PINGS 3;

typedef enum vita_chiaki_host_type_t {
  DISCOVERED = 0x01,
  MANUALLY_ADDED = 0x02,
  REGISTERED = 0x04,
} VitaChiakiHostType;

typedef struct vita_chiaki_host_t {
  VitaChiakiHostType type;
  ChiakiTarget target;
  uint8_t server_mac[6];
  char* hostname;
  uint64_t last_discovery_seen_us;

  ChiakiDiscoveryHost* discovery_state;
  ChiakiRegisteredHost* registered_state;

  char status_hint[96];
  uint64_t status_hint_expire_us;
  bool status_hint_is_error;
} VitaChiakiHost;


typedef uint8_t MacAddr[6];

void host_free(VitaChiakiHost* host);
int host_register(VitaChiakiHost* host, int pin);
int host_wakeup(VitaChiakiHost* host);
int host_stream(VitaChiakiHost* host);
void host_cancel_stream_request(void);
bool mac_addrs_match(MacAddr* a, MacAddr* b);
void save_manual_host(VitaChiakiHost* rhost, char* new_hostname);
void delete_manual_host(VitaChiakiHost* mhost);
void update_context_hosts();
int count_manual_hosts_of_console(VitaChiakiHost* host);
void copy_host(VitaChiakiHost* h_dest, VitaChiakiHost* h_src, bool copy_hostname);
void copy_host_registered_state(ChiakiRegisteredHost* rstate_dest, ChiakiRegisteredHost* rstate_src);
