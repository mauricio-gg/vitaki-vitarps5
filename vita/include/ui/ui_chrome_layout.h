/**
 * @file ui_chrome_layout.h
 * @brief Pure layout rules for the top bar and hint row (SPEC.md C06 and C23)
 *
 * No SDK dependency: the two rules a person would notice if wrong live here so they
 * can be checked natively. Which hints survive a narrow row, and how a cooldown
 * reason is shortened to fit the banner.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

/** Text ellipsis marker appended to a shortened string. */
#define UI_ELLIPSIS "..."

/**
 * ui_hint_row_fit() - Decide which hints are kept when the row is too narrow (SPEC C06).
 * @widths:        Width of each hint in pixels.
 * @low_priority:  True for hints that may be dropped; the others are never dropped.
 * @count:         Number of hints.
 * @gap:           Space between neighbouring kept hints.
 * @avail_w:       Width the kept hints must fit in.
 * @keep:          Out: true for each hint that stays.
 *
 * Starts with every hint kept and drops low-priority hints one at a time, the
 * rightmost first, until the row fits. If only never-dropped hints remain and they
 * still do not fit, they all stay.
 *
 * Returns the width the kept hints occupy, gaps included (0 when none are kept).
 */
int ui_hint_row_fit(const int *widths, const bool *low_priority, int count, int gap, int avail_w,
                    bool *keep);

/** Measures a UTF-8 string in pixels. */
typedef int (*UiMeasureFn)(const char *text, void *ctx);

/**
 * ui_ellipsize_to_fit() - Shorten @text with UI_ELLIPSIS so it measures at most @max_w.
 * @text:     NUL-terminated UTF-8 string.
 * @max_w:    Largest allowed width in pixels.
 * @measure:  Width function.
 * @ctx:      Passed to @measure.
 * @out:      Destination buffer.
 * @out_size: Size of @out in bytes.
 *
 * A string that already fits is copied unchanged. Otherwise the longest prefix that,
 * with UI_ELLIPSIS appended, fits is kept; the cut never lands inside a multi-byte
 * character. When not even UI_ELLIPSIS fits, @out is UI_ELLIPSIS alone. The result
 * is always NUL-terminated and truncated to @out_size - 1 bytes if needed.
 *
 * Returns the length of the string written to @out.
 */
size_t ui_ellipsize_to_fit(const char *text, int max_w, UiMeasureFn measure, void *ctx, char *out,
                           size_t out_size);
