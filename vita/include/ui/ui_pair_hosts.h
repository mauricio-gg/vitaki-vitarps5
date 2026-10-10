/**
 * @file ui_pair_hosts.h
 * @brief The unpaired consoles found on the network, for the Pair new device item and popup
 *        (SPEC.md 3.1a, C29)
 *
 * Reads context.hosts directly and never goes through the Home card cache, which refreshes only
 * every 10 seconds: a console that answers or goes quiet shows up here on the next frame. Only the
 * hosts' inline snapshot fields are read (display_name, hostname, target), never the strings
 * owned by discovery_state, so it is safe next to the discovery thread (see ui_cards_map_host()).
 */

#pragma once

#include <stdbool.h>

#include "context.h"
#include "ui/ui_console_rows.h"

/** Bytes of a listed console's name and address, terminator included. */
#define UI_PAIR_HOST_NAME_MAX VITA_HOST_DISPLAY_NAME_LEN
#define UI_PAIR_HOST_IP_MAX 48

/** One unpaired console that answered discovery. */
typedef struct ui_pair_host_t {
  VitaChiakiHost *host;  ///< the entry in context.hosts; borrowed
  char name[UI_PAIR_HOST_NAME_MAX];
  char ip[UI_PAIR_HOST_IP_MAX];
  bool ps5;  ///< PS5, otherwise PS4
} UiPairHost;

/** Most consoles the list can hold: every slot of context.hosts. */
#define UI_PAIR_HOSTS_MAX MAX_CONTEXT_HOSTS

/** ui_pair_hosts_init() - Start the discovery clock the item's "Searching..." window reads. */
void ui_pair_hosts_init(void);

/**
 * ui_pair_hosts_collect() - Fill @out with the unpaired, discovered consoles in popup order
 * (ui_console_discovered_order_before()).
 * @return how many were written, at most UI_PAIR_HOSTS_MAX
 */
int ui_pair_hosts_collect(UiPairHost out[UI_PAIR_HOSTS_MAX]);

/** ui_pair_hosts_count() - How many consoles ui_pair_hosts_collect() would list. */
int ui_pair_hosts_count(void);

/**
 * ui_pair_hosts_item_phase() - What the Pair new device item says about discovery.
 * @found: Unpaired consoles found now (ui_pair_hosts_count()).
 *
 * The "Searching..." window counts from when discovery was last seen starting, which is noticed
 * the first time this runs after it starts (the app starts it at launch, so the window opens
 * with the app).
 */
UiPairPhase ui_pair_hosts_item_phase(int found);
