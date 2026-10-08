/**
 * @file ui_console_status.h
 * @brief Pure console classification and ordering for the Home list (SPEC.md section 3.1)
 *
 * No SDK dependency: the rules a person would notice if wrong (which status a console
 * shows, where it sits in the list) live here so they can be checked natively.
 */

#pragma once

#include <stdbool.h>

/** The status a console row shows. Error and Retrying arrive with ticket #301. */
typedef enum ui_console_status_t {
  UI_CONSOLE_READY = 0,
  UI_CONSOLE_STANDBY,
  UI_CONSOLE_UNPAIRED,
  UI_CONSOLE_UNAVAILABLE,
  UI_CONSOLE_COOLDOWN,
} UiConsoleStatus;

/** Classification result: the status plus whether the row carries the "Internet" route label. */
typedef struct ui_console_state_t {
  UiConsoleStatus status;
  bool internet_route;
} UiConsoleState;

/**
 * ui_console_classify() - Decide what a console row shows.
 * @registered:  The console has pairing credentials.
 * @discovered:  The console answered local discovery right now.
 * @standby:     Discovery reports it in rest mode (only meaningful when @discovered).
 * @internet_ok: A PSN route exists and the PSN token is valid.
 * @cooldown:    The post-stream cooldown is active for this console.
 *
 * Cooldown wins. Otherwise: not paired is Unpaired when seen on the network and
 * Unavailable when not; paired and seen is Ready or Standby; paired and not seen is
 * Ready with the Internet label when a valid PSN route exists, else Unavailable.
 */
UiConsoleState ui_console_classify(bool registered, bool discovered, bool standby, bool internet_ok,
                                   bool cooldown);

/** ui_console_status_label() - The word a status goes by on Home: "Ready", "Standby", ... */
const char *ui_console_status_label(UiConsoleStatus status);

/**
 * ui_console_order_before() - List order: paired consoles first, then by name.
 * @a_registered: First console is paired.
 * @a_name:       First console's name.
 * @b_registered: Second console is paired.
 * @b_name:       Second console's name.
 *
 * Name comparison ignores ASCII case. Returns true only when a must sit strictly
 * before b, so equal consoles keep their existing order (stable sort).
 */
bool ui_console_order_before(bool a_registered, const char *a_name, bool b_registered,
                             const char *b_name);

/**
 * ui_console_route_label() - The "Route" the detail panel shows for a console.
 * @discovered:  The console answered local discovery right now.
 * @internet_ok: A PSN route exists and the PSN token is valid (the same input as classify).
 *
 * "Local Network + Internet", "Local Network", "Internet" or "Not reachable".
 */
const char *ui_console_route_label(bool discovered, bool internet_ok);
