/**
 * @file ui_console_rows.c
 * @brief Pure rules for the Consoles list (see ui_console_rows.h)
 */

#include "ui/ui_console_rows.h"

#include <string.h>

bool ui_console_rows_has_filter(int total_consoles, bool filter_active) {
  return filter_active || total_consoles > UI_FILTER_ROW_MAX_PLAIN_CONSOLES;
}

int ui_console_rows_console_index(bool has_filter_row, int row, int console_count) {
  int index = has_filter_row ? row - 1 : row;
  return (index < 0 || index >= console_count) ? -1 : index;
}

int ui_console_rows_initial_focus(bool has_filter_row, int console_count) {
  return (has_filter_row && console_count > 0) ? 1 : 0;
}

int ui_console_rows_rebase_focus(int focus, bool had_filter_row, bool has_filter_row) {
  if (had_filter_row == has_filter_row)
    return focus;
  if (has_filter_row)
    return focus + 1;
  return focus > 0 ? focus - 1 : 0;
}

/** Lower-case an ASCII letter; every other byte is returned unchanged. */
static char ascii_lower(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

/** True when @needle is a case-insensitive substring of @haystack; @needle is non-empty. */
static bool contains_nocase(const char *haystack, const char *needle) {
  const size_t needle_len = strlen(needle);
  const size_t haystack_len = strlen(haystack);
  if (needle_len > haystack_len)
    return false;
  for (size_t i = 0; i + needle_len <= haystack_len; i++) {
    size_t j = 0;
    while (j < needle_len && ascii_lower(haystack[i + j]) == ascii_lower(needle[j]))
      j++;
    if (j == needle_len)
      return true;
  }
  return false;
}

int ui_console_option_rows(UiConsoleStatus status, bool both_routes, bool can_change_icon,
                           UiConsoleOptionRow out[UI_CONSOLE_OPTION_ROWS_MAX]) {
  int n = 0;
  if (status == UI_CONSOLE_UNPAIRED) {
    out[n++] = (UiConsoleOptionRow){UI_CONSOLE_OPTION_PAIR, false};
    if (can_change_icon)
      out[n++] = (UiConsoleOptionRow){UI_CONSOLE_OPTION_CHANGE_ICON, false};
    return n;
  }
  const bool cooldown = status == UI_CONSOLE_COOLDOWN;
  out[n++] = (UiConsoleOptionRow){
      status == UI_CONSOLE_STANDBY ? UI_CONSOLE_OPTION_WAKE_CONNECT : UI_CONSOLE_OPTION_CONNECT,
      cooldown};
  if (both_routes)
    out[n++] = (UiConsoleOptionRow){UI_CONSOLE_OPTION_CONNECT_VIA, cooldown};
  out[n++] = (UiConsoleOptionRow){UI_CONSOLE_OPTION_REPAIR, false};
  if (can_change_icon)
    out[n++] = (UiConsoleOptionRow){UI_CONSOLE_OPTION_CHANGE_ICON, false};
  return n;
}

bool ui_console_matches_filter(const char *name, const char *ip, const char *filter) {
  if (!filter || !*filter)
    return true;
  return (name && contains_nocase(name, filter)) || (ip && contains_nocase(ip, filter));
}
