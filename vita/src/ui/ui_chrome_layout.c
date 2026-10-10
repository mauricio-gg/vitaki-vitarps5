/**
 * @file ui_chrome_layout.c
 * @brief Pure layout rules for the top bar and hint row (SPEC.md C06 and C23)
 */

#include "ui/ui_chrome_layout.h"

#include <string.h>

/** Width of the kept hints including the gaps between them. */
static int kept_width(const int *widths, const bool *keep, int count, int gap) {
  int total = 0;
  int kept = 0;
  for (int i = 0; i < count; i++) {
    if (!keep[i])
      continue;
    total += widths[i];
    kept++;
  }
  return kept > 0 ? total + gap * (kept - 1) : 0;
}

int ui_hint_row_fit(const int *widths, const bool *low_priority, int count, int gap, int avail_w,
                    bool *keep) {
  for (int i = 0; i < count; i++)
    keep[i] = true;

  int total = kept_width(widths, keep, count, gap);
  while (total > avail_w) {
    int drop = -1;
    for (int i = count - 1; i >= 0; i--) {
      if (keep[i] && low_priority[i]) {
        drop = i;
        break;
      }
    }
    if (drop < 0)
      break;
    keep[drop] = false;
    total = kept_width(widths, keep, count, gap);
  }
  return total;
}

/** True when @byte begins a UTF-8 character (is not a continuation byte). */
static bool is_char_start(unsigned char byte) {
  return (byte & 0xC0) != 0x80;
}

size_t ui_ellipsize_to_fit(const char *text, int max_w, UiMeasureFn measure, void *ctx, char *out,
                           size_t out_size) {
  if (out_size == 0)
    return 0;

  const size_t ellipsis_len = strlen(UI_ELLIPSIS);
  size_t len = strlen(text);

  if (measure(text, ctx) <= max_w) {
    if (len > out_size - 1)
      len = out_size - 1;
    memcpy(out, text, len);
    out[len] = '\0';
    return len;
  }

  /* Longest prefix first; only cut at character boundaries. */
  size_t cut = len;
  if (cut > out_size - 1 - ellipsis_len)
    cut = out_size > ellipsis_len + 1 ? out_size - 1 - ellipsis_len : 0;
  for (; cut > 0; cut--) {
    if (!is_char_start((unsigned char)text[cut]))
      continue;
    memcpy(out, text, cut);
    memcpy(out + cut, UI_ELLIPSIS, ellipsis_len + 1);
    if (measure(out, ctx) <= max_w)
      return cut + ellipsis_len;
  }

  size_t n = ellipsis_len < out_size - 1 ? ellipsis_len : out_size - 1;
  memcpy(out, UI_ELLIPSIS, n);
  out[n] = '\0';
  return n;
}
