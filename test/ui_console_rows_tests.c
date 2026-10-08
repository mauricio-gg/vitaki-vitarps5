// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the Consoles list rules in vita/src/ui/ui_console_rows.c
// (issue #300): when the Filter row shows, which console a list row means, where the focus
// goes when the row appears, what the filter matches, and which Options rows a console
// offers (ticket #303). `./tools/build.sh test` only
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

/** With the Filter row, row 0 is the filter and row i is console i - 1; a wrong shift connects
 * to the neighbouring console. */
static void test_row_to_console_mapping(void) {
  assert(ui_console_rows_console_index(true, 0, 5) == -1);
  assert(ui_console_rows_console_index(true, 1, 5) == 0);
  assert(ui_console_rows_console_index(true, 5, 5) == 4);
  assert(ui_console_rows_console_index(true, 6, 5) == -1);
  assert(ui_console_rows_console_index(false, 0, 3) == 0);
  assert(ui_console_rows_console_index(false, 2, 3) == 2);
  assert(ui_console_rows_console_index(false, 3, 3) == -1);
  assert(ui_console_rows_console_index(true, 0, 0) == -1);
}

/** Focus starts on the first console so Confirm connects at once; with no console it stays on
 * row 0. */
static void test_initial_focus_skips_filter_row(void) {
  assert(ui_console_rows_initial_focus(true, 5) == 1);
  assert(ui_console_rows_initial_focus(false, 3) == 0);
  assert(ui_console_rows_initial_focus(true, 0) == 0);
}

/** The Filter row appearing above the focus must not move the focus to another console. */
static void test_focus_follows_console_when_row_changes(void) {
  assert(ui_console_rows_rebase_focus(0, false, true) == 1);
  assert(ui_console_rows_rebase_focus(3, false, true) == 4);
  assert(ui_console_rows_rebase_focus(3, true, false) == 2);
  assert(ui_console_rows_rebase_focus(0, true, false) == 0);
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

/** Unpaired offers Pair only; Standby wakes; Connect via needs both routes; Cooldown disables
 * Connect and Connect via but never Re-pair. */
static void test_option_rows_follow_status_and_routes(void) {
  UiConsoleOptionRow rows[UI_CONSOLE_OPTION_ROWS_MAX];

  assert(ui_console_option_rows(UI_CONSOLE_UNPAIRED, true, rows) == 1);
  assert(rows[0].option == UI_CONSOLE_OPTION_PAIR && !rows[0].disabled);

  assert(ui_console_option_rows(UI_CONSOLE_READY, false, rows) == 2);
  assert(rows[0].option == UI_CONSOLE_OPTION_CONNECT && !rows[0].disabled);
  assert(rows[1].option == UI_CONSOLE_OPTION_REPAIR);

  assert(ui_console_option_rows(UI_CONSOLE_STANDBY, true, rows) == 3);
  assert(rows[0].option == UI_CONSOLE_OPTION_WAKE_CONNECT);
  assert(rows[1].option == UI_CONSOLE_OPTION_CONNECT_VIA && !rows[1].disabled);
  assert(rows[2].option == UI_CONSOLE_OPTION_REPAIR);

  assert(ui_console_option_rows(UI_CONSOLE_COOLDOWN, true, rows) == 3);
  assert(rows[0].disabled && rows[1].disabled && !rows[2].disabled);
}

int main(void) {
  test_filter_row_rule();
  test_row_to_console_mapping();
  test_initial_focus_skips_filter_row();
  test_focus_follows_console_when_row_changes();
  test_filter_matches_name_and_ip();
  test_option_rows_follow_status_and_routes();
  printf("ui_console_rows_tests: all passed\n");
  return 0;
}
