/**
 * @file ui_steps.h
 * @brief C16 ProgressSteps: the list of stages on the right of the Connecting screen
 *
 * Display-only draw helper (SPEC.md C16). Done steps get a 12 px OK dot, the current step is
 * T28 with its detail line, a glow and a 16 px spinner, pending steps show their number in
 * TEXT_3. Paper cost: 2 draws per done or pending step, 4 for the current one.
 */

#pragma once

/** One step. Strings are borrowed; they must stay valid and unchanged while they are shown
 * (the width of the current step's title is cached by pointer). */
typedef struct ui_step_t {
  const char *title;
  const char *detail;
} UiStep;

/**
 * ui_draw_steps() - Draw @count steps down from (@x, @y), @current marking the current one.
 * @count: At most UI_STEPS_MAX; extra steps are not drawn.
 * @current: Index of the current step. Steps before it are done, steps after it are pending;
 *           an index outside 0..count - 1 is clamped.
 */
void ui_draw_steps(int x, int y, const UiStep steps[], int count, int current);
