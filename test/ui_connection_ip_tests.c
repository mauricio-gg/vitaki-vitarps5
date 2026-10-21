// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) test for issue #94: the Profile > Connection Console IP row (and the Home
// detail panel's Address row, which reads the same function) must never show a PSN console's
// name as its address. `./tools/build.sh test` only cross-compiles, so run this on the host
// (from the repo root):
//   cc -std=gnu11 -Wall -Wextra -I test/stubs/manual_hosts -I vita/include -I lib/include \
//      -I lib/src -I vita/third_party test/ui_connection_ip_tests.c \
//      vita/src/ui/ui_value_labels.c vita/src/ui/ui_console_status.c vita/src/ip_address.c -o /tmp/ui_connection_ip_tests && /tmp/ui_connection_ip_tests

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "context.h"
#include "host.h"
#include "ui/ui_console_cards.h"
#include "ui/ui_value_labels.h"

#define CONSOLE_NAME "Living Room PS5"
#define SEED_ADDRESS "192.168.1.40"
#define LAN_ADDRESS "192.168.1.77"

VitaChiakiContext context;

/* ---- Stubs for the parts of ui_value_labels.c this test never reaches ---- */
void ui_cards_map_host(VitaChiakiHost *host, ConsoleCardInfo *card) {
  (void)host;
  (void)card;
}
int ui_cards_get_selected_index(void) {
  return -1;
}
bool psn_auth_token_is_valid(uint64_t now_epoch) {
  (void)now_epoch;
  return false;
}

/* A PSN host as add_psn_host_from_device() builds it: hostname is the console name. */
static VitaChiakiHost make_psn_host(const char *display_address) {
  VitaChiakiHost host;
  memset(&host, 0, sizeof(host));
  host.source = VITA_HOST_SOURCE_PSN_REMOTE;
  snprintf(host.hostname, sizeof(host.hostname), "%s", CONSOLE_NAME);
  snprintf(host.display_address, sizeof(host.display_address), "%s", display_address);
  return host;
}

int main(void) {
  /* The #94 bug: an unknown address must hide the row, not show the console name. */
  VitaChiakiHost psn_unknown = make_psn_host("");
  assert(ui_connection_console_ip(&psn_unknown) == NULL);

  VitaChiakiHost psn_known = make_psn_host(SEED_ADDRESS);
  assert(strcmp(ui_connection_console_ip(&psn_known), SEED_ADDRESS) == 0);

  /* A LAN / manual host still shows its hostname. */
  VitaChiakiHost lan;
  memset(&lan, 0, sizeof(lan));
  snprintf(lan.hostname, sizeof(lan.hostname), "%s", LAN_ADDRESS);
  assert(strcmp(ui_connection_console_ip(&lan), LAN_ADDRESS) == 0);

  /* No host, or a host with no address at all: row hidden. */
  VitaChiakiHost empty;
  memset(&empty, 0, sizeof(empty));
  assert(ui_connection_console_ip(NULL) == NULL);
  assert(ui_connection_console_ip(&empty) == NULL);

  printf("ui_connection_ip_tests: all passed\n");
  return 0;
}
