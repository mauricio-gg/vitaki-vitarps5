/**
 * @file ui_console_status.c
 * @brief Pure console classification and ordering for the Home list
 */

#include "ui/ui_console_status.h"

UiConsoleState ui_console_classify(bool registered, bool discovered, bool standby, bool internet_ok,
                                   bool cooldown) {
  UiConsoleState state = {UI_CONSOLE_UNAVAILABLE, false};

  if (cooldown) {
    state.status = UI_CONSOLE_COOLDOWN;
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

/** Lower-case an ASCII letter; every other byte is returned unchanged. */
static char ascii_lower(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

bool ui_console_order_before(bool a_registered, const char *a_name, bool b_registered,
                             const char *b_name) {
  if (a_registered != b_registered)
    return a_registered;

  const char *a = a_name ? a_name : "";
  const char *b = b_name ? b_name : "";
  while (*a && *b && ascii_lower(*a) == ascii_lower(*b)) {
    a++;
    b++;
  }
  return (unsigned char)ascii_lower(*a) < (unsigned char)ascii_lower(*b);
}
