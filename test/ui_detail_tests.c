// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the pure helpers behind the Home detail panel and Home motion:
// the status-message word wrap and the ease-out curve. `./tools/build.sh test` only
// cross-compiles for arm-vita-eabi and never executes, so run these natively:
//
//   cc -std=c99 -Wall -Wextra -I vita/include \
//      test/ui_detail_tests.c vita/src/ui/ui_text_wrap.c -o /tmp/ui_detail_tests && \
//      /tmp/ui_detail_tests

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui/ui_motion.h"
#include "ui/ui_text_wrap.h"

/* The panel's text width in pixels (SPEC C04). */
#define PANEL_W 304

/* Every Error and Retrying message of the SPEC copy deck. */
static const char *const MESSAGES[] = {
    "Wake signal failed. Check pairing and network.",
    "Remote Play already active on console",
    "Console Remote Play crashed - wait a moment",
    "Missing console credentials. Re-pair may be required.",
    "Enable PSN internet mode in settings.",
    "PSN login required for internet remote play.",
    "PSN session expired. Re-authenticate in Profile.",
    "Could not determine host address.",
    "Wake signal failed; attempting connection anyway.",
    "Console releasing session... ready in 7s",
    "Console busy - retrying in 3s...",
    "Waiting for console network link...",
    "Video references unstable - requesting keyframe",
    "Rebuilding stream at safer bitrate",
    "Persistent video desync - rebuilding session",
    "Packet loss burst - requesting keyframe",
};

/* A fixed-pitch stand-in for the font: every byte is char_w pixels wide. */
static int measure_fixed(const char *s, void *ctx) {
  return (int)strlen(s) * *(const int *)ctx;
}

/* The words of @s in order, space separated, so wrapped output can be compared with the input. */
static void normalise(const char *s, char *out) {
  size_t n = 0;
  int pending_space = 0;
  for (; *s; s++) {
    if (*s == ' ') {
      pending_space = n > 0;
      continue;
    }
    if (pending_space)
      out[n++] = ' ';
    pending_space = 0;
    out[n++] = *s;
  }
  out[n] = '\0';
}

static int is_single_word(const char *s) {
  return strchr(s, ' ') == NULL;
}

/* Words are never split, no text is lost, at most three lines, and every line fits. */
static void test_wrap_keeps_words_whole_and_lines_inside_the_panel(void) {
  const int char_widths[] = {7, 8, 9, 10};
  for (size_t m = 0; m < sizeof(MESSAGES) / sizeof(MESSAGES[0]); m++) {
    for (size_t w = 0; w < sizeof(char_widths) / sizeof(char_widths[0]); w++) {
      int char_w = char_widths[w];
      UiWrapped wrapped;
      ui_text_wrap(MESSAGES[m], PANEL_W, measure_fixed, &char_w, &wrapped);

      assert(wrapped.count >= 1 && wrapped.count <= UI_WRAP_MAX_LINES);

      char joined[UI_WRAP_MAX_LINES * UI_WRAP_LINE_MAX] = "";
      for (int i = 0; i < wrapped.count; i++) {
        assert(measure_fixed(wrapped.lines[i], &char_w) <= PANEL_W);
        if (i)
          strcat(joined, " ");
        strcat(joined, wrapped.lines[i]);
      }

      char expected[UI_WRAP_MAX_LINES * UI_WRAP_LINE_MAX];
      normalise(MESSAGES[m], expected);
      if (!wrapped.truncated) {
        assert(strcmp(joined, expected) == 0);
      } else {
        /* Truncated text is a whole-word prefix of the original, then "...". */
        size_t body = strlen(joined) - 3;
        assert(strcmp(joined + body, "...") == 0);
        assert(strncmp(joined, expected, body) == 0);
        assert(expected[body] == ' ' || expected[body] == '\0');
      }
    }
  }
}

/* A text that needs a fourth line ends on the third with dots, and the dots fit. */
static void test_wrap_truncates_with_dots_that_fit(void) {
  int char_w = 8;
  UiWrapped wrapped;
  ui_text_wrap("one two three four five six seven eight nine ten eleven twelve thirteen fourteen "
               "fifteen sixteen seventeen eighteen nineteen twenty twentyone twentytwo",
               PANEL_W, measure_fixed, &char_w, &wrapped);

  assert(wrapped.truncated);
  assert(wrapped.count == UI_WRAP_MAX_LINES);
  const char *last = wrapped.lines[UI_WRAP_MAX_LINES - 1];
  assert(strlen(last) > 3 && strcmp(last + strlen(last) - 3, "...") == 0);
  assert(measure_fixed(last, &char_w) <= PANEL_W);
}

/* A word wider than the panel is kept whole on a line of its own instead of being cut. */
static void test_wrap_never_cuts_an_oversize_word(void) {
  int char_w = 8;
  UiWrapped wrapped;
  ui_text_wrap("go Supercalifragilisticexpialidocious-and-then-some now", PANEL_W, measure_fixed,
               &char_w, &wrapped);

  assert(wrapped.count == 3);
  assert(strcmp(wrapped.lines[0], "go") == 0);
  assert(strcmp(wrapped.lines[1], "Supercalifragilisticexpialidocious-and-then-some") == 0);
  assert(strcmp(wrapped.lines[2], "now") == 0);
  assert(is_single_word(wrapped.lines[1]));
}

/* No text, or only spaces, takes no lines. */
static void test_wrap_empty_text_has_no_lines(void) {
  int char_w = 8;
  UiWrapped wrapped;
  ui_text_wrap("", PANEL_W, measure_fixed, &char_w, &wrapped);
  assert(wrapped.count == 0);
  ui_text_wrap("   ", PANEL_W, measure_fixed, &char_w, &wrapped);
  assert(wrapped.count == 0);
  ui_text_wrap(NULL, PANEL_W, measure_fixed, &char_w, &wrapped);
  assert(wrapped.count == 0);
}

/* The slide curve starts and ends exactly on target and never runs backwards or overshoots, so
 * rows land where the layout says and the detail panel never jitters. */
static void test_ease_out_runs_from_zero_to_one_without_overshoot(void) {
  const float x1 = 0.22f, y1 = 0.7f, x2 = 0.2f, y2 = 1.0f;
  assert(ui_bezier_ease(0.0f, x1, y1, x2, y2) == 0.0f);
  assert(ui_bezier_ease(1.0f, x1, y1, x2, y2) == 1.0f);

  float previous = 0.0f;
  for (int i = 1; i <= 100; i++) {
    float v = ui_bezier_ease((float)i / 100.0f, x1, y1, x2, y2);
    assert(v >= previous);
    assert(v <= 1.0f);
    previous = v;
  }
  /* Ease-out: well ahead of linear at the start. */
  assert(ui_bezier_ease(0.1f, x1, y1, x2, y2) > 0.2f);
}

int main(void) {
  test_wrap_keeps_words_whole_and_lines_inside_the_panel();
  test_wrap_truncates_with_dots_that_fit();
  test_wrap_never_cuts_an_oversize_word();
  test_wrap_empty_text_has_no_lines();
  test_ease_out_runs_from_zero_to_one_without_overshoot();
  puts("ui_detail_tests: all passed");
  return 0;
}
