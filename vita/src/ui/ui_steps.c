/**
 * @file ui_steps.c
 * @brief C16 ProgressSteps (SPEC.md C16, xmb.css .stp)
 */

#include "ui/ui_steps.h"

#include <stdbool.h>
#include <stddef.h>

#include <vita2d.h>

#include "ui/ui_component.h"
#include "ui/ui_spinner.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"

/** Height of a done or pending row, and of the current row: padding, title line, detail line. */
#define ROW_H (UI_STEP_PAD * 2 + UI_STEP_LINE)
#define CURRENT_ROW_H (UI_STEP_PAD * 2 + UI_STEP_CUR_LINE + UI_STEP_DETAIL_LINE)
/** Text starts after the marker column and one UI_S2 gap. */
#define TEXT_DX (UI_STEP_MARKER_W + UI_S2)

static const char *const STEP_NUMBERS[UI_STEPS_MAX] = {"1", "2", "3", "4", "5", "6", "7"};

/* Widths that never change are measured once; the current title's is measured when it changes. */
static int s_number_w[UI_STEPS_MAX];
static bool s_number_w_ready = false;
static const char *s_current_title = NULL;
static int s_current_title_w = 0;

/** Measure the digits once, on the first draw (the fonts are ready by then). */
static void measure_numbers(void) {
  for (int i = 0; i < UI_STEPS_MAX; i++)
    s_number_w[i] = ui_text_face_width(UI_FACE_T16, STEP_NUMBERS[i]);
  s_number_w_ready = true;
}

/** Draw the current step with its row top at @row_y. */
static void draw_current(int x, int row_y, const UiStep *step) {
  const int text_x = x + TEXT_DX;
  const int title_y = row_y + UI_STEP_PAD;

  if (step->title != s_current_title) {
    s_current_title = step->title;
    s_current_title_w = ui_text_face_width(UI_FACE_T28, step->title);
  }
  ui_glow_draw_rect((UiRect){text_x, title_y, s_current_title_w, UI_STEP_CUR_LINE}, UI_STEP_GLOW,
                    ui_color_scale_alpha(UI_GLOW, (float)UI_STEP_GLOW_PCT / 100.0f));
  ui_spinner_draw(UI_SPINNER_INLINE, x + UI_STEP_MARKER_W / 2, title_y + UI_STEP_CUR_LINE / 2);
  ui_text_draw_face_centered_v(UI_FACE_T28, text_x, title_y, UI_STEP_CUR_LINE, UI_TEXT,
                               step->title);
  ui_text_draw_face_centered_v(UI_FACE_T16, text_x, title_y + UI_STEP_CUR_LINE, UI_STEP_DETAIL_LINE,
                               UI_TEXT_2, step->detail);
}

/** Draw a done (@done) or pending step number @index with its row top at @row_y. */
static void draw_passive(int x, int row_y, const UiStep *step, int index, bool done) {
  const int line_y = row_y + UI_STEP_PAD;

  if (done) {
    vita2d_draw_fill_circle((float)(x + UI_STEP_MARKER_W / 2), (float)(line_y + UI_STEP_LINE / 2),
                            (float)UI_STEP_DOT / 2.0f, UI_OK);
  } else {
    ui_text_draw_face_centered_v(UI_FACE_T16, x + (UI_STEP_MARKER_W - s_number_w[index]) / 2,
                                 line_y, UI_STEP_LINE, UI_TEXT_3, STEP_NUMBERS[index]);
  }
  ui_text_draw_face_centered_v(UI_FACE_T20, x + TEXT_DX, line_y, UI_STEP_LINE, UI_TEXT_2,
                               step->title);
}

void ui_draw_steps(int x, int y, const UiStep steps[], int count, int current) {
  if (count > UI_STEPS_MAX)
    count = UI_STEPS_MAX;
  if (count <= 0)
    return;
  if (current < 0)
    current = 0;
  if (current >= count)
    current = count - 1;
  if (!s_number_w_ready)
    measure_numbers();

  int row_y = y;
  for (int i = 0; i < count; i++) {
    if (i == current) {
      draw_current(x, row_y, &steps[i]);
      row_y += CURRENT_ROW_H;
    } else {
      draw_passive(x, row_y, &steps[i], i, i < current);
      row_y += ROW_H;
    }
  }
}
