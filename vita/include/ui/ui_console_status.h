/**
 * @file ui_console_status.h
 * @brief Pure console classification and ordering for the Home list (SPEC.md section 3.1)
 *
 * No SDK dependency: the rules a person would notice if wrong (which status a console
 * shows, where it sits in the list) live here so they can be checked natively.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** The status a console row shows. */
typedef enum ui_console_status_t {
  UI_CONSOLE_READY = 0,
  UI_CONSOLE_STANDBY,
  UI_CONSOLE_UNPAIRED,
  UI_CONSOLE_UNAVAILABLE,
  UI_CONSOLE_COOLDOWN,
  UI_CONSOLE_ERROR,
  UI_CONSOLE_RETRYING,
} UiConsoleStatus;

/** What a console's status message currently counts as (SPEC.md section 6, flag 12). */
typedef enum ui_console_message_t {
  UI_CONSOLE_MESSAGE_NONE = 0,  ///< no message, or it has expired
  UI_CONSOLE_MESSAGE_ERROR,     ///< a failure the user must act on
  UI_CONSOLE_MESSAGE_RETRYING,  ///< the app is retrying or waiting
} UiConsoleMessage;

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
 * @message:     What the console's live status message counts as (ui_console_message_class()).
 *
 * Cooldown wins, then a live status message (Error or Retrying). Otherwise: not paired is Unpaired
 * when seen on the network and Unavailable when not; paired and seen is Ready or Standby; paired
 * and not seen is Ready with the Internet label when a valid PSN route exists, else Unavailable.
 */
UiConsoleState ui_console_classify(bool registered, bool discovered, bool standby, bool internet_ok,
                                   bool cooldown, UiConsoleMessage message);

/**
 * ui_console_message_class() - Classify a console's status message as Error or Retrying.
 * @msg:       The message text (host->status_hint); NULL or empty means none.
 * @is_error:  The flag the message was set with (host->status_hint_is_error).
 * @expire_us: When the message expires, 0 for never.
 * @now_us:    The current time, on the same clock as @expire_us.
 *
 * An expired or empty message is NONE. The messages SPEC flag 12 names as Retrying are
 * Retrying whatever their flag says (some are flagged as errors for the popup). Any other
 * message keeps the flag's meaning: flagged is Error, otherwise Retrying.
 */
UiConsoleMessage ui_console_message_class(const char *msg, bool is_error, uint64_t expire_us,
                                          uint64_t now_us);

/**
 * ui_console_hint_is_failure() - Does a hint open the "Could not connect" popup (SPEC C14)?
 * @msg:      The hint text being set; NULL or empty means none.
 * @is_error: The flag the hint is set with.
 *
 * True exactly when the hint counts as an Error (ui_console_message_class(), never expiring), so
 * the popup and the row's Error status always agree. Messages the app is retrying through, even
 * when their caller flags them as errors, never open it.
 */
bool ui_console_hint_is_failure(const char *msg, bool is_error);

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
 * ui_console_discovered_order_before() - Order of the unpaired consoles in the Pair new device
 * popup (SPEC C29): by name ignoring ASCII case, equal names by address.
 * @a_name, @a_ip: First console's name and IP address.
 * @b_name, @b_ip: Second console's name and IP address.
 *
 * Addresses that are dotted IPv4 compare by number, so 192.168.1.4 comes before 192.168.1.10;
 * anything else compares as text. Returns true only when a must sit strictly before b, so equal
 * consoles keep their existing order (stable sort).
 */
bool ui_console_discovered_order_before(const char *a_name, const char *a_ip, const char *b_name,
                                        const char *b_ip);

/**
 * ui_console_route_label() - The "Route" the detail panel shows for a console.
 * @discovered:  The console answered local discovery right now.
 * @internet_ok: A PSN route exists and the PSN token is valid (the same input as classify).
 *
 * "Local Network + Internet", "Local Network", "Internet" or "Not reachable".
 */
const char *ui_console_route_label(bool discovered, bool internet_ok);

/** Plain facts about the console the Profile Connection rows and Home's Connection panel describe.
 */
typedef struct ui_connection_facts_t {
  bool selected;     ///< a console exists to describe; false is "no console selected"
  bool registered;   ///< the console has pairing credentials
  bool discovered;   ///< it answered local discovery right now
  bool standby;      ///< discovery reports it in rest mode (only meaningful when @discovered)
  bool psn_source;   ///< it came from the PSN console list
  bool manual;       ///< it was added by hand
  bool internet_ok;  ///< a PSN route exists and the PSN token is valid
} UiConnectionFacts;

/** The two words the Connection rows show for one console. */
typedef struct ui_connection_words_t {
  const char *network_type;
  const char *status;
} UiConnectionWords;

/**
 * ui_console_connection_words() - The Network Type and Status words for the Connection rows
 * (SPEC.md section 3.7), so Profile and Home's info panel can never disagree.
 * @facts: What is known about the console.
 *
 * Network Type: "Local Wi-Fi" when discovered, else "PSN Internet" for a PSN console, else
 * "Manual Host" for a hand-added one, else "Unavailable". Status is the console's real state in
 * the Home list's words (ui_console_classify() without cooldown or message), and "None" only when
 * no console is selected.
 */
UiConnectionWords ui_console_connection_words(const UiConnectionFacts *facts);
