/**
 * @file ui_console_rows.h
 * @brief Pure rules for the Consoles list: the Pair new device item, the Filter row, which
 *        console a row means, and what the filter matches (SPEC.md C02 "Filter item", sections
 *        3.1 and 3.1a)
 *
 * The list is the Pair new device item (row 0, always), then the Filter row when it is shown,
 * then the paired consoles.
 *
 * No Vita dependency, so the rules can be checked natively (test/ui_console_rows_tests.c).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui/ui_console_status.h"

/** Row of the Pair new device item. */
#define UI_CONSOLE_ROW_PAIR 0
/** Row of the Filter row, when it is shown: right under the item. */
#define UI_CONSOLE_ROW_FILTER 1

/** How long after discovery starts, or after the pairing popup opens, "nothing found yet" still
 * reads "Searching..." (SPEC 3.1a, C29). */
#define UI_PAIR_SEARCH_MS 5000

/** The Filter row is shown once there are more consoles than this (or a filter is active). */
#define UI_FILTER_ROW_MAX_PLAIN_CONSOLES 4

/**
 * ui_console_rows_has_filter() - Whether the Consoles list starts with the Filter row.
 * @total_consoles: Consoles known before any filter is applied.
 * @filter_active:  A filter is applied.
 */
bool ui_console_rows_has_filter(int total_consoles, bool filter_active);

/**
 * ui_console_rows_lead() - Rows before the first console: the Pair new device item, plus the
 * Filter row when it is shown.
 */
int ui_console_rows_lead(bool has_filter_row);

/**
 * ui_console_rows_console_index() - The console a list row stands for.
 * @has_filter_row: The list starts with the Filter row.
 * @row:            List row index.
 * @console_count:  Consoles in the list (after the filter).
 *
 * @return the index into the console cache, or -1 for the item, the Filter row or a row outside
 *         the list
 */
int ui_console_rows_console_index(bool has_filter_row, int row, int console_count);

/**
 * ui_console_rows_initial_focus() - The row focused when the Consoles category is entered:
 * the first console, not the item or the Filter row (so Confirm connects at once). The item
 * (row 0) when the list has no console.
 */
int ui_console_rows_initial_focus(bool has_filter_row, int console_count);

/**
 * ui_console_rows_rebase_focus() - Keep the focus on the same console when the Filter row
 * appears or disappears above it. The item above the Filter row never moves.
 * @focus:         Focused row before the change.
 * @had_filter_row / @has_filter_row: Whether the row existed before / exists now.
 *
 * @return the row that now holds the same console; the first console (or the item, when there
 *         is none) if the Filter row itself was focused and went away
 */
int ui_console_rows_rebase_focus(int focus, bool had_filter_row, bool has_filter_row);

/** What the Options column can do for a console (SPEC.md C05). */
typedef enum ui_console_option_t {
  UI_CONSOLE_OPTION_CONNECT = 0,
  UI_CONSOLE_OPTION_WAKE_CONNECT,  ///< Connect on a Standby console: wakes it first
  UI_CONSOLE_OPTION_CONNECT_VIA,
  UI_CONSOLE_OPTION_REPAIR,
  UI_CONSOLE_OPTION_CHANGE_ICON,
} UiConsoleOption;

/** One row of the Options column. */
typedef struct ui_console_option_row_t {
  UiConsoleOption option;
  bool disabled;
} UiConsoleOptionRow;

/** Most rows ui_console_option_rows() returns (Connect, Connect via, Re-pair, Change icon). */
#define UI_CONSOLE_OPTION_ROWS_MAX 4

/**
 * ui_console_option_rows() - The Options rows for a console, in order.
 * @status:      The console's status.
 * @both_routes: It has a local and an Internet route to choose between.
 * @can_change_icon: Its icon can be stored (it has a MAC); only then is Change icon offered.
 * @out:         Receives the rows.
 *
 * Connect (Wake and connect for Standby), Connect via only with both routes, then Re-pair, then
 * Change icon when it can be stored. Cooldown disables Connect and Connect via. Only paired
 * consoles are listed on Home, so there is no Options variant for an unpaired one.
 *
 * @return the number of rows
 */
int ui_console_option_rows(UiConsoleStatus status, bool both_routes, bool can_change_icon,
                           UiConsoleOptionRow out[UI_CONSOLE_OPTION_ROWS_MAX]);

/**
 * ui_console_matches_filter() - Whether a console is kept by the filter text.
 * @name: Console name (may be NULL).
 * @ip:   Console IP address (may be NULL).
 * @filter: Filter text; an empty or NULL filter keeps everything.
 *
 * A console matches when @filter is a case-insensitive (ASCII) substring of its name or its IP.
 */
bool ui_console_matches_filter(const char *name, const char *ip, const char *filter);

/** What the Pair new device item and its popup say about discovery (SPEC 3.1a, C29). */
typedef enum ui_pair_phase_t {
  UI_PAIR_PHASE_FOUND = 0,  ///< at least one unpaired console is listed
  UI_PAIR_PHASE_SEARCHING,  ///< none yet, and the search just started
  UI_PAIR_PHASE_NONE,       ///< none, and the search has had time to find some
  UI_PAIR_PHASE_OFF,        ///< Auto Discovery is not running
} UiPairPhase;

/**
 * ui_console_rows_pair_phase() - Which phase the item and the popup are in.
 * @discovery_running: Discovery is running.
 * @found:             Unpaired consoles listed.
 * @elapsed_ms:        Time since discovery started (the item) or since the popup opened.
 *
 * Off wins, then Found; with none, Searching holds for UI_PAIR_SEARCH_MS and then reads None.
 */
UiPairPhase ui_console_rows_pair_phase(bool discovery_running, int found, uint32_t elapsed_ms);
