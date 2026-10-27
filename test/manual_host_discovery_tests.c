// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for issue #234: discovery must never overwrite or wipe a manual host.
// A manual host's record is shared between context.config.manual_hosts (what is saved to disk)
// and context.hosts (what the Home screen lists). These tests run the real update_context_hosts(),
// save_discovered_host(), discovery_cb() and config_serialize_manual_hosts().
// `./tools/build.sh test` only cross-compiles, so run these on the host (from the repo root):
//   cc -std=gnu11 -Wall -Wextra -I test/stubs/manual_hosts -I vita/include -I lib/include \
//      -I lib/src -I vita/third_party test/manual_host_discovery_tests.c vita/src/discovery.c \
//      vita/src/host_storage.c vita/src/config_hosts.c vita/src/discovery_host.c \
//      vita/third_party/tomlc99/toml.c lib/src/base64.c \
//      -o /tmp/manual_host_discovery_tests && /tmp/manual_host_discovery_tests

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chiaki/base64.h>

#include "config_hosts.h"
#include "context.h"
#include "discovery.h"
#include "host.h"
#include "token_crypto.h"
#include "ui/ui_console_cards.h"

#define TYPED_ADDRESS "192.168.1.77"
#define DISCOVERED_ADDRESS "192.168.1.50"
#define CONSOLE_ID "AABBCCDDEEFF"
#define NOW_US 10000000ULL
#define LOST_US (NOW_US + 10000000ULL)

/* Defined in vita/src/discovery.c; no header declares them. */
int save_discovered_host(ChiakiDiscoveryHost *host);
void discovery_cb(ChiakiDiscoveryHost *hosts, size_t hosts_count, void *user);

VitaChiakiContext context;
static uint64_t fake_now_us = NOW_US;

/* ---- Stubs for platform calls and modules outside the code under test ---- */

uint64_t sceKernelGetProcessTimeWide(void) { return fake_now_us; }
void ui_cards_mark_dirty(void) {}
bool host_in_active_use(const VitaChiakiHost *host) {
  (void)host;
  return false;
}
bool config_serialize(VitaChiakiConfig *cfg) {
  (void)cfg;
  return true;
}
ChiakiErrorCode chiaki_discovery_service_init(ChiakiDiscoveryService *service,
                                              ChiakiDiscoveryServiceOptions *options,
                                              ChiakiLog *log) {
  (void)service;
  (void)options;
  (void)log;
  return CHIAKI_ERR_SUCCESS;
}
void chiaki_discovery_service_fini(ChiakiDiscoveryService *service) { (void)service; }
ChiakiTarget chiaki_discovery_host_system_version_target(ChiakiDiscoveryHost *host) {
  (void)host;
  return CHIAKI_TARGET_PS5_1;
}
const char *chiaki_discovery_host_state_string(ChiakiDiscoveryHostState state) {
  (void)state;
  return "state";
}
void chiaki_log(ChiakiLog *log, ChiakiLogLevel level, const char *fmt, ...) {
  (void)log;
  (void)level;
  (void)fmt;
}

/* The same trivial helpers as vita/src/util.c (that file pulls in Vita-only headers). */
/* config_hosts.c encrypts/decrypts registered-host pairing keys; these tests only serialize manual
 * hosts, so the crypto is never reached. */
char *token_crypto_encrypt(const char *plaintext, const char *kind) { (void)plaintext; (void)kind; return NULL; }
char *token_crypto_decrypt(const char *b64_blob, const char *kind) { (void)b64_blob; (void)kind; return NULL; }
size_t get_base64_size(size_t in) { return ((4 * in / 3) + 3) & ~3; }
void parse_b64(const char *val, uint8_t *dest, size_t len) {
  chiaki_base64_decode(val, get_base64_size(len), dest, &len);
}
void parse_mac(const char *mac_str, uint8_t *mac_dest) {
  for (int j = 0; j < 6; j++) {
    char digit[3] = {mac_str[2 * j], mac_str[2 * j + 1], 0};
    mac_dest[j] = (uint8_t)strtol(digit, NULL, 16);
  }
}

/* Mirrors host_free() in vita/src/host.c (that file is the whole streaming stack): free what the
 * host owns, then zero the struct in place and leave the husk allocated. */
void host_free(VitaChiakiHost *host) {
  if (!host)
    return;
  if (host->discovery_state)
    destroy_discovery_host(host->discovery_state);
  if (host->registered_state)
    free(host->registered_state);
  memset(host, 0, sizeof(*host));
}

/* ---- Helpers ---- */

static void reset_context(void) {
  memset(&context, 0, sizeof(context));
  fake_now_us = NOW_US;
}

/* A manual host as config_parse_manual_hosts() builds it, shared by config and display list. */
static VitaChiakiHost *add_manual_host(const char *address) {
  VitaChiakiHost *host = calloc(1, sizeof(*host));
  assert(host);
  host->type = REGISTERED | MANUALLY_ADDED;
  host->source = VITA_HOST_SOURCE_MANUAL_REMOTE;
  parse_mac(CONSOLE_ID, host->server_mac);
  host->registered_state = calloc(1, sizeof(ChiakiRegisteredHost));
  assert(host->registered_state);
  snprintf(host->hostname, sizeof(host->hostname), "%s", address);
  snprintf(host->display_name, sizeof(host->display_name), "%s", address);
  context.config.manual_hosts[context.config.num_manual_hosts++] = host;
  update_context_hosts();
  return host;
}

static ChiakiDiscoveryHost make_discovery_host(const char *address) {
  ChiakiDiscoveryHost host = {0};
  host.state = CHIAKI_DISCOVERY_HOST_STATE_READY;
  host.host_id = CONSOLE_ID;
  host.host_name = "PS5-Living-Room";
  host.host_addr = address;
  return host;
}

/* Runs the real serializer into memory; the caller frees the returned text. */
static char *serialize_manual_hosts(void) {
  char *text = NULL;
  size_t text_size = 0;
  FILE *fp = open_memstream(&text, &text_size);
  assert(fp);
  config_serialize_manual_hosts(fp, &context.config);
  fclose(fp);
  return text;
}

static int count_cards_for_console(VitaChiakiHost **found) {
  VitaChiakiHost *probe = NULL;
  int count = 0;
  for (int i = 0; i < MAX_CONTEXT_HOSTS; i++) {
    VitaChiakiHost *h = context.hosts[i];
    if (h && h->server_mac[0] == 0xAA && h->server_mac[5] == 0xFF) {
      probe = h;
      count++;
    }
  }
  if (found)
    *found = probe;
  return count;
}

static void assert_typed_address_still_saved(const VitaChiakiHost *manual) {
  char *saved = serialize_manual_hosts();
  assert(strstr(saved, "[[manual_hosts]]") != NULL);
  assert(strstr(saved, "hostname = \"" TYPED_ADDRESS "\"") != NULL);
  assert(strcmp(manual->hostname, TYPED_ADDRESS) == 0);
  free(saved);
}

/* ---- Tests ---- */

/** A console that answers discovery without an address must not blank the typed address, and the
 * user still sees one card for it. */
static void test_discovery_without_address_keeps_typed_address(void) {
  reset_context();
  VitaChiakiHost *manual = add_manual_host(TYPED_ADDRESS);
  ChiakiDiscoveryHost discovered = make_discovery_host(NULL);

  save_discovered_host(&discovered);

  assert_typed_address_still_saved(manual);
  VitaChiakiHost *card = NULL;
  assert(count_cards_for_console(&card) == 1);
  assert(card->type & DISCOVERED);
}

/** A console discovered at another address must not replace the typed address on disk, while the
 * card shows the discovered address and name the user connects with. */
static void test_discovery_at_other_address_keeps_typed_address(void) {
  reset_context();
  VitaChiakiHost *manual = add_manual_host(TYPED_ADDRESS);
  ChiakiDiscoveryHost discovered = make_discovery_host(DISCOVERED_ADDRESS);

  save_discovered_host(&discovered);

  assert_typed_address_still_saved(manual);
  VitaChiakiHost *card = NULL;
  assert(count_cards_for_console(&card) == 1);
  assert(card != manual);
  assert(strcmp(card->hostname, DISCOVERED_ADDRESS) == 0);
  assert(strcmp(card->display_name, "PS5-Living-Room") == 0);
}

/** When the console goes quiet and its discovered record is removed, the manual host must still be
 * saved with the typed address and must be back in the display list. */
static void test_lost_console_brings_manual_card_back(void) {
  reset_context();
  VitaChiakiHost *manual = add_manual_host(TYPED_ADDRESS);
  ChiakiDiscoveryHost discovered = make_discovery_host(DISCOVERED_ADDRESS);
  save_discovered_host(&discovered);

  fake_now_us = LOST_US;
  discovery_cb(NULL, 0, NULL);

  assert_typed_address_still_saved(manual);
  VitaChiakiHost *card = NULL;
  assert(count_cards_for_console(&card) == 1);
  assert(card == manual);
}

int main(void) {
  test_discovery_without_address_keeps_typed_address();
  test_discovery_at_other_address_keeps_typed_address();
  test_lost_console_brings_manual_card_back();
  printf("manual_host_discovery_tests: all passed\n");
  return 0;
}
