/**
 * @file ui_text_wrap.h
 * @brief Word wrap into a fixed number of lines (SPEC.md C04 status message)
 *
 * Pure: the caller supplies the width function, so the rules (words are never split, the
 * result is at most UI_WRAP_MAX_LINES lines, every line fits unless one word is wider than
 * the line) can be checked natively. Nothing is allocated; the result lives in the caller's
 * struct.
 */

#pragma once

#include <stdbool.h>

/** Most lines a wrapped text can take. */
#define UI_WRAP_MAX_LINES 3
/** Bytes per line including the terminator; a longer line is cut at a word boundary. */
#define UI_WRAP_LINE_MAX 96

/** Pixel width of the NUL-terminated string @s. */
typedef int (*UiTextMeasureFn)(const char *s, void *ctx);

typedef struct ui_wrapped_t {
  char lines[UI_WRAP_MAX_LINES][UI_WRAP_LINE_MAX];
  int count;       ///< lines used, 0 for an empty text
  bool truncated;  ///< the text did not fit; the last line ends with "..."
} UiWrapped;

/**
 * ui_text_wrap() - Break @text at spaces into lines no wider than @max_w.
 * @text:   UTF-8 text; runs of spaces count as one separator.
 * @max_w:  Line width limit in pixels.
 * @measure: Width function for the face the text is drawn in.
 * @ctx:    Passed to @measure.
 * @out:    Result; always fully initialised.
 *
 * A word wider than @max_w gets a line of its own and overflows it (words are never split).
 * When the text needs more than UI_WRAP_MAX_LINES lines, the last line drops words from its
 * end until it fits with "..." appended, and @out->truncated is set.
 */
void ui_text_wrap(const char *text, int max_w, UiTextMeasureFn measure, void *ctx, UiWrapped *out);
