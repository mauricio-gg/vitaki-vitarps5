/**
 * @file ui_console_status.c
 * @brief Pure console classification and ordering for the Home list
 */

#include "ui/ui_console_status.h"

#include <string.h>

#include "ip_address.h"

UiConsoleState ui_console_classify(bool registered, bool discovered, bool standby, bool internet_ok,
                                   bool cooldown, UiConsoleMessage message) {
  UiConsoleState state = {UI_CONSOLE_UNAVAILABLE, false};

  if (cooldown) {
    state.status = UI_CONSOLE_COOLDOWN;
  } else if (message == UI_CONSOLE_MESSAGE_ERROR) {
    state.status = UI_CONSOLE_ERROR;
  } else if (message == UI_CONSOLE_MESSAGE_RETRYING) {
    state.status = UI_CONSOLE_RETRYING;
  } else if (!registered) {
    state.status = discovered ? UI_CONSOLE_UNPAIRED : UI_CONSOLE_UNAVAILABLE;
  } else if (discovered) {
    state.status = standby ? UI_CONSOLE_STANDBY : UI_CONSOLE_READY;
  } else if (internet_ok) {
    state.status = UI_CONSOLE_READY;
    state.internet_route = true;
  }
  return state;
}

/**
 * Starts of the messages SPEC flag 12 lists as Retrying, as the code writes them (some end
 * in a changing number, so only the start is compared).
 */
static const char *const RETRYING_PREFIXES[] = {
    "Wake signal failed; attempting connection anyway",
    "Console releasing session",
    "Console busy",
    "Waiting for console network link",
    "Video references unstable",
    "Rebuilding stream at safer bitrate",
    "Persistent video desync",
    "Packet loss burst",
};

UiConsoleMessage ui_console_message_class(const char *msg, bool is_error, uint64_t expire_us,
                                          uint64_t now_us) {
  if (!msg || !msg[0] || (expire_us != 0 && now_us > expire_us))
    return UI_CONSOLE_MESSAGE_NONE;

  for (size_t i = 0; i < sizeof(RETRYING_PREFIXES) / sizeof(RETRYING_PREFIXES[0]); i++) {
    if (strncmp(msg, RETRYING_PREFIXES[i], strlen(RETRYING_PREFIXES[i])) == 0)
      return UI_CONSOLE_MESSAGE_RETRYING;
  }
  return is_error ? UI_CONSOLE_MESSAGE_ERROR : UI_CONSOLE_MESSAGE_RETRYING;
}

bool ui_console_hint_is_failure(const char *msg, bool is_error) {
  return ui_console_message_class(msg, is_error, 0, 0) == UI_CONSOLE_MESSAGE_ERROR;
}

const char *ui_console_status_label(UiConsoleStatus status) {
  switch (status) {
    case UI_CONSOLE_READY:
      return "Ready";
    case UI_CONSOLE_STANDBY:
      return "Standby";
    case UI_CONSOLE_UNPAIRED:
      return "Unpaired";
    case UI_CONSOLE_COOLDOWN:
      return "Please wait...";
    case UI_CONSOLE_ERROR:
      return "Error";
    case UI_CONSOLE_RETRYING:
      return "Retrying";
    case UI_CONSOLE_UNAVAILABLE:
    default:
      return "Unavailable";
  }
}

/** Lower-case an ASCII letter; every other byte is returned unchanged. */
static char ascii_lower(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

/** Compare two names ignoring ASCII case: negative, zero or positive like strcmp; NULL is "". */
static int compare_names_nocase(const char *a_name, const char *b_name) {
  const char *a = a_name ? a_name : "";
  const char *b = b_name ? b_name : "";
  while (*a && *b && ascii_lower(*a) == ascii_lower(*b)) {
    a++;
    b++;
  }
  return (unsigned char)ascii_lower(*a) - (unsigned char)ascii_lower(*b);
}

bool ui_console_order_before(bool a_registered, const char *a_name, bool b_registered,
                             const char *b_name) {
  if (a_registered != b_registered)
    return a_registered;
  return compare_names_nocase(a_name, b_name) < 0;
}

/** Compare two addresses by number when both are dotted IPv4, else as text: strcmp-like. */
static int compare_addresses(const char *a_ip, const char *b_ip) {
  uint8_t a[IP_ADDRESS_OCTET_COUNT];
  uint8_t b[IP_ADDRESS_OCTET_COUNT];
  if (ip_address_parse(a_ip, a) && ip_address_parse(b_ip, b)) {
    for (int i = 0; i < IP_ADDRESS_OCTET_COUNT; i++) {
      if (a[i] != b[i])
        return a[i] - b[i];
    }
    return 0;
  }
  return strcmp(a_ip ? a_ip : "", b_ip ? b_ip : "");
}

bool ui_console_discovered_order_before(const char *a_name, const char *a_ip, const char *b_name,
                                        const char *b_ip) {
  const int by_name = compare_names_nocase(a_name, b_name);
  if (by_name != 0)
    return by_name < 0;
  return compare_addresses(a_ip, b_ip) < 0;
}

const char *ui_console_route_label(bool discovered, bool internet_ok) {
  if (discovered && internet_ok)
    return "Local Network + Internet";
  if (discovered)
    return "Local Network";
  return internet_ok ? "Internet" : "Not reachable";
}

UiConnectionWords ui_console_connection_words(const UiConnectionFacts *facts) {
  UiConnectionWords words = {"Unavailable", "None"};
  if (!facts || !facts->selected)
    return words;

  if (facts->discovered)
    words.network_type = "Local Wi-Fi";
  else if (facts->psn_source)
    words.network_type = "PSN Internet";
  else if (facts->manual)
    words.network_type = "Manual Host";

  words.status = ui_console_status_label(ui_console_classify(facts->registered, facts->discovered,
                                                             facts->standby, facts->internet_ok,
                                                             false, UI_CONSOLE_MESSAGE_NONE)
                                             .status);
  return words;
}
