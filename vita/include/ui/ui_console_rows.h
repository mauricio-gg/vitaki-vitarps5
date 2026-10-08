/**
 * @file ui_console_rows.h
 * @brief Pure rules for the Consoles list: the Filter row, which console a row means, and what
 *        the filter matches (SPEC.md C02 "Filter item", section 3.1)
 *
 * No Vita dependency, so the rules can be checked natively (test/ui_console_rows_tests.c).
 */

#pragma once

#include <stdbool.h>

/** The Filter row is shown once there are more consoles than this (or a filter is active). */
#define UI_FILTER_ROW_MAX_PLAIN_CONSOLES 4

/**
 * ui_console_rows_has_filter() - Whether the Consoles list starts with the Filter row.
 * @total_consoles: Consoles known before any filter is applied.
 * @filter_active:  A filter is applied.
 */
bool ui_console_rows_has_filter(int total_consoles, bool filter_active);

/**
 * ui_console_rows_console_index() - The console a list row stands for.
 * @has_filter_row: The list starts with the Filter row.
 * @row:            List row index.
 * @console_count:  Consoles in the list (after the filter).
 *
 * @return the index into the console cache, or -1 for the Filter row or a row outside the list
 */
int ui_console_rows_console_index(bool has_filter_row, int row, int console_count);

/**
 * ui_console_rows_initial_focus() - The row focused when the Consoles category is entered:
 * the first console, not the Filter row (so Confirm connects at once). Row 0 when the list
 * has no console.
 */
int ui_console_rows_initial_focus(bool has_filter_row, int console_count);

/**
 * ui_console_rows_rebase_focus() - Keep the focus on the same console when the Filter row
 * appears or disappears above it.
 * @focus:         Focused row before the change.
 * @had_filter_row / @has_filter_row: Whether the row existed before / exists now.
 *
 * @return the row that now holds the same console; row 0 if the Filter row itself was focused
 *         and went away
 */
int ui_console_rows_rebase_focus(int focus, bool had_filter_row, bool has_filter_row);

/**
 * ui_console_matches_filter() - Whether a console is kept by the filter text.
 * @name: Console name (may be NULL).
 * @ip:   Console IP address (may be NULL).
 * @filter: Filter text; an empty or NULL filter keeps everything.
 *
 * A console matches when @filter is a case-insensitive (ASCII) substring of its name or its IP.
 */
bool ui_console_matches_filter(const char *name, const char *ip, const char *filter);
