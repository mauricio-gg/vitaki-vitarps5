// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the Consoles list rules in vita/src/ui/ui_console_rows.c
// (issues #300, #330): when the Filter row shows, which console a list row means (the Pair new
// device item is row 0), where the focus goes when the row appears, what the filter matches,
// which Options rows a console offers (ticket #303) and what the Pair new device item says. `./tools/build.sh test` only
// cross-compiles, so run these on the host:
//   cc -std=c99 -Wall -Wextra -I vita/include test/ui_console_rows_tests.c \
//      vita/src/ui/ui_console_rows.c -o /tmp/ui_console_rows_tests && \
//      /tmp/ui_console_rows_tests

#include <assert.h>
#include <stdio.h>

#include "ui/ui_console_rows.h"

/** The Filter row shows for more than 4 consoles, or whenever a filter is active. */
static void test_filter_row_rule(void) {
  assert(!ui_console_rows_has_filter(4, false));
  assert(ui_console_rows_has_filter(5, false));
  assert(ui_console_rows_has_filter(1, true));
  assert(ui_console_rows_has_filter(0, true));
  assert(!ui_console_rows_has_filter(0, false));
}

/** Row 0 is always the Pair new device item; the Filter row (when shown) is row 1; a wrong shift
 * connects to the neighbouring console. */
static void test_row_to_console_mapping(void) {
  assert(ui_console_rows_console_index(false, 0, 3) == -1);
  assert(ui_console_rows_console_index(false, 1, 3) == 0);
  assert(ui_console_rows_console_index(false, 3, 3) == 2);
  assert(ui_console_rows_console_index(false, 4, 3) == -1);
  assert(ui_console_rows_console_index(true, 0, 5) == -1);
  assert(ui_console_rows_console_index(true, 1, 5) == -1);
  assert(ui_console_rows_console_index(true, 2, 5) == 0);
  assert(ui_console_rows_console_index(true, 6, 5) == 4);
  assert(ui_console_rows_console_index(true, 7, 5) == -1);
  assert(ui_console_rows_console_index(false, 1, 0) == -1);
}

/** Focus starts on the first console so Confirm connects at once; with no paired console the
 * Pair new device item is the only row and has the focus. */
static void test_initial_focus_skips_item_and_filter_row(void) {
  assert(ui_console_rows_initial_focus(false, 3) == 1);
  assert(ui_console_rows_initial_focus(true, 5) == 2);
  assert(ui_console_rows_initial_focus(false, 0) == UI_CONSOLE_ROW_PAIR);
  assert(ui_console_rows_initial_focus(true, 0) == UI_CONSOLE_ROW_PAIR);
}

/** The Filter row appearing above the focus must not move the focus to another console, and
 * must not push the Pair new device item (row 0) out from under the focus. */
static void test_focus_follows_console_when_row_changes(void) {
  assert(ui_console_rows_rebase_focus(0, false, true) == 0);
  assert(ui_console_rows_rebase_focus(1, false, true) == 2);
  assert(ui_console_rows_rebase_focus(3, false, true) == 4);
  assert(ui_console_rows_rebase_focus(0, true, false) == 0);
  assert(ui_console_rows_rebase_focus(1, true, false) == 1);
  assert(ui_console_rows_rebase_focus(4, true, false) == 3);
  assert(ui_console_rows_rebase_focus(2, true, true) == 2);
}

/** The filter matches the name or the IP address, ignoring case. */
static void test_filter_matches_name_and_ip(void) {
  assert(ui_console_matches_filter("PS5-Living-Room", "192.168.1.20", "living"));
  assert(ui_console_matches_filter("PS5-Living-Room", "192.168.1.20", "1.20"));
  assert(!ui_console_matches_filter("PS5-Living-Room", "192.168.1.20", "bedroom"));
  assert(!ui_console_matches_filter("PS5", "10.0.0.5", "0.0.0.50"));
  assert(ui_console_matches_filter("PS5", NULL, "ps"));
  assert(!ui_console_matches_filter(NULL, NULL, "ps"));
  assert(ui_console_matches_filter("PS5", "10.0.0.5", ""));
}

/** Standby wakes; Connect via needs both routes; Cooldown disables Connect and Connect via but
 * never Re-pair. */
static void test_option_rows_follow_status_and_routes(void) {
  UiConsoleOptionRow rows[UI_CONSOLE_OPTION_ROWS_MAX];

  assert(ui_console_option_rows(UI_CONSOLE_READY, false, false, rows) == 2);
  assert(rows[0].option == UI_CONSOLE_OPTION_CONNECT && !rows[0].disabled);
  assert(rows[1].option == UI_CONSOLE_OPTION_REPAIR);

  assert(ui_console_option_rows(UI_CONSOLE_STANDBY, true, false, rows) == 3);
  assert(rows[0].option == UI_CONSOLE_OPTION_WAKE_CONNECT);
  assert(rows[1].option == UI_CONSOLE_OPTION_CONNECT_VIA && !rows[1].disabled);
  assert(rows[2].option == UI_CONSOLE_OPTION_REPAIR);

  assert(ui_console_option_rows(UI_CONSOLE_COOLDOWN, true, false, rows) == 3);
  assert(rows[0].disabled && rows[1].disabled && !rows[2].disabled);
}

/** Change icon is the last row, offered only when the icon can be stored; the longest list
 * still fits the Options column's rows. */
static void test_change_icon_row_needs_a_storable_icon(void) {
  UiConsoleOptionRow rows[UI_CONSOLE_OPTION_ROWS_MAX];

  assert(ui_console_option_rows(UI_CONSOLE_READY, true, true, rows) == UI_CONSOLE_OPTION_ROWS_MAX);
  assert(rows[3].option == UI_CONSOLE_OPTION_CHANGE_ICON);

  assert(ui_console_option_rows(UI_CONSOLE_COOLDOWN, false, true, rows) == 3);
  assert(rows[2].option == UI_CONSOLE_OPTION_CHANGE_ICON && !rows[2].disabled);

  assert(ui_console_option_rows(UI_CONSOLE_READY, true, false, rows) == 3);
}

/** Off wins over everything; found consoles show whatever the clock says; with none the search
 * reads "Searching..." only for the first UI_PAIR_SEARCH_MS, then "None". */
static void test_pair_phase_follows_discovery_and_the_clock(void) {
  assert(ui_console_rows_pair_phase(false, 3, 0) == UI_PAIR_PHASE_OFF);
  assert(ui_console_rows_pair_phase(false, 0, 99999) == UI_PAIR_PHASE_OFF);
  assert(ui_console_rows_pair_phase(true, 2, 99999) == UI_PAIR_PHASE_FOUND);
  assert(ui_console_rows_pair_phase(true, 0, 0) == UI_PAIR_PHASE_SEARCHING);
  assert(ui_console_rows_pair_phase(true, 0, UI_PAIR_SEARCH_MS - 1) == UI_PAIR_PHASE_SEARCHING);
  assert(ui_console_rows_pair_phase(true, 0, UI_PAIR_SEARCH_MS) == UI_PAIR_PHASE_NONE);
}

int main(void) {
  test_filter_row_rule();
  test_row_to_console_mapping();
  test_initial_focus_skips_item_and_filter_row();
  test_focus_follows_console_when_row_changes();
  test_filter_matches_name_and_ip();
  test_option_rows_follow_status_and_routes();
  test_change_icon_row_needs_a_storable_icon();
  test_pair_phase_follows_discovery_and_the_clock();
  printf("ui_console_rows_tests: all passed\n");
  return 0;
}
