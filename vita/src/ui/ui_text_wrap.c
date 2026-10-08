/**
 * @file ui_text_wrap.c
 * @brief Word wrap into a fixed number of lines
 */

#include "ui/ui_text_wrap.h"

#include "ui/ui_chrome_layout.h"

#include <stddef.h>
#include <string.h>

static const char WRAP_ELLIPSIS[] = UI_ELLIPSIS;

/** Length of the word that starts at @s (up to the next space or the end). */
static size_t word_length(const char *s) {
  size_t n = 0;
  while (s[n] && s[n] != ' ')
    n++;
  return n;
}

/**
 * try_append() - Append @word (@len bytes) to @line, after a space when the line is not empty.
 * @return false, leaving @line unchanged, when the result would not fit the line buffer
 */
static bool try_append(char *line, const char *word, size_t len) {
  size_t used = strlen(line);
  size_t need = used + (used ? 1 : 0) + len + 1;
  if (need > UI_WRAP_LINE_MAX)
    return false;
  if (used)
    line[used++] = ' ';
  memcpy(line + used, word, len);
  line[used + len] = '\0';
  return true;
}

/** Remove the last word (and the space before it) from @line. */
static void drop_last_word(char *line) {
  char *space = strrchr(line, ' ');
  if (space)
    *space = '\0';
  else
    line[0] = '\0';
}

/**
 * add_ellipsis() - End the last line with "...", dropping words from it until it fits.
 * A single word that is wider than the line keeps its place and gets the dots anyway.
 */
static void add_ellipsis(char *line, int max_w, UiTextMeasureFn measure, void *ctx) {
  char candidate[UI_WRAP_LINE_MAX];
  for (;;) {
    size_t used = strlen(line);
    if (used + sizeof(WRAP_ELLIPSIS) <= UI_WRAP_LINE_MAX) {
      memcpy(candidate, line, used);
      memcpy(candidate + used, WRAP_ELLIPSIS, sizeof(WRAP_ELLIPSIS));
      if (measure(candidate, ctx) <= max_w || !strchr(line, ' ')) {
        memcpy(line, candidate, used + sizeof(WRAP_ELLIPSIS));
        return;
      }
    } else if (!strchr(line, ' ')) {
      /* One word that cannot take the dots in the buffer: cut the end to make room. */
      line[UI_WRAP_LINE_MAX - sizeof(WRAP_ELLIPSIS) - 1] = '\0';
      continue;
    }
    drop_last_word(line);
  }
}

void ui_text_wrap(const char *text, int max_w, UiTextMeasureFn measure, void *ctx, UiWrapped *out) {
  memset(out, 0, sizeof(*out));
  if (!text || !measure)
    return;

  char *line = NULL;
  const char *p = text;
  while (*p) {
    if (*p == ' ') {
      p++;
      continue;
    }
    size_t len = word_length(p);

    if (!line) {
      line = out->lines[out->count++];
    }

    /* Try the word on the current line. */
    char candidate[UI_WRAP_LINE_MAX];
    strcpy(candidate, line);
    bool fits =
        try_append(candidate, p, len) && (line[0] == '\0' || measure(candidate, ctx) <= max_w);
    if (fits) {
      strcpy(line, candidate);
      p += len;
      continue;
    }

    if (line[0] == '\0') {
      /* The word alone is wider than the line (or the buffer): it keeps its own line. */
      size_t keep = len < UI_WRAP_LINE_MAX - 1 ? len : UI_WRAP_LINE_MAX - 1;
      memcpy(line, p, keep);
      line[keep] = '\0';
      p += len;
      continue;
    }

    /* Start the next line, or give up when there is none left. */
    if (out->count == UI_WRAP_MAX_LINES) {
      out->truncated = true;
      add_ellipsis(line, max_w, measure, ctx);
      return;
    }
    line = out->lines[out->count++];
  }
}
