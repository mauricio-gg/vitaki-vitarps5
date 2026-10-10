/**
 * @file ui_pair_hosts.c
 * @brief The unpaired consoles found on the network (see ui_pair_hosts.h)
 */

#include "ui/ui_pair_hosts.h"

#include <stdio.h>

#include <psp2/kernel/processmgr.h>

#include "context.h"
#include "ui/ui_console_status.h"

/** Microseconds in a millisecond. */
#define US_PER_MS 1000ULL

/** When discovery was last seen starting, and whether it was running the last time we looked. */
static uint64_t s_discovery_since_us = 0;
static bool s_discovery_was_running = false;

void ui_pair_hosts_init(void) {
  s_discovery_since_us = sceKernelGetProcessTimeWide();
  s_discovery_was_running = false;
}

/** True for a host that answered discovery and is not paired. */
static bool is_unpaired_discovered(const VitaChiakiHost *host) {
  return host && (host->type & DISCOVERED) && host->discovery_state && !(host->type & REGISTERED);
}

int ui_pair_hosts_count(void) {
  int count = 0;
  for (int i = 0; i < MAX_CONTEXT_HOSTS; i++) {
    if (is_unpaired_discovered(context.hosts[i]))
      count++;
  }
  return count;
}

int ui_pair_hosts_collect(UiPairHost out[UI_PAIR_HOSTS_MAX]) {
  int count = 0;
  for (int i = 0; i < MAX_CONTEXT_HOSTS; i++) {
    VitaChiakiHost *host = context.hosts[i];
    if (!is_unpaired_discovered(host))
      continue;

    UiPairHost entry = {.host = host, .ps5 = chiaki_target_is_ps5(host->target)};
    snprintf(entry.name, sizeof(entry.name), "%s",
             host->display_name[0] ? host->display_name : host->hostname);
    snprintf(entry.ip, sizeof(entry.ip), "%s", host->hostname);

    /* Stable insertion into the sorted prefix. */
    int j = count++;
    while (j > 0 && ui_console_discovered_order_before(entry.name, entry.ip, out[j - 1].name,
                                                       out[j - 1].ip)) {
      out[j] = out[j - 1];
      j--;
    }
    out[j] = entry;
  }
  return count;
}

UiPairPhase ui_pair_hosts_item_phase(int found) {
  const uint64_t now_us = sceKernelGetProcessTimeWide();
  if (context.discovery_enabled && !s_discovery_was_running)
    s_discovery_since_us = now_us;
  s_discovery_was_running = context.discovery_enabled;
  return ui_console_rows_pair_phase(context.discovery_enabled, found,
                                    (uint32_t)((now_us - s_discovery_since_us) / US_PER_MS));
}
