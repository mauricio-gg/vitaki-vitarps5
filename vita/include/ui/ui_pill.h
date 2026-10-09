/**
 * @file ui_pill.h
 * @brief C19 Pill: a 32 px pill with one line of text (SPEC.md C19)
 *
 * Display-only draw helper. HUD fill, T16 TEXT, pill caps, 16 px side padding.
 * Variants: warn (1 px WARN outline, the cooldown banner), unstable (a 12 px ERR
 * dot, the whole pill pulsing over UI_PILL_PULSE_MS) and plain (no outline, no dot; its label
 * may mix text with button glyphs, see UiPillPart).
 */

#pragma once

#include <stdint.h>

/** Pill variants. */
typedef enum ui_pill_kind_t {
  UI_PILL_WARN = 0,
  UI_PILL_UNSTABLE,
} UiPillKind;

/** ui_pill_width() - Width the pill needs for @text (padding and, for unstable, the dot). */
int ui_pill_width(UiPillKind kind, const char *text);

/**
 * ui_pill_draw() - Draw a pill with its top-left corner at (@x, @y).
 * Warn costs 7 draws (fill 3, outline 3, text); unstable costs 5 (fill 3, dot, text).
 */
void ui_pill_draw(UiPillKind kind, int x, int y, const char *text);

/**
 * One piece of a plain pill's label: a text run (@text set) or a button glyph (@text NULL,
 * @glyph the UI_BTN_* action, drawn like the hint row's glyph). Spaces inside text are kept, so
 * "Hold " and " + " give the gaps. @width is filled by ui_pill_plain_layout().
 */
typedef struct ui_pill_part_t {
  const char *text;
  uint32_t glyph;
  int width;
} UiPillPart;

/**
 * ui_pill_plain_layout() - Measure the parts of a plain pill once.
 * @parts: Parts to measure; their @width is set. Keep the array and reuse it for every frame.
 * @count: Number of parts.
 * Return: Width of the whole pill, side padding included. Measures text, so call it once, not
 * per frame.
 */
int ui_pill_plain_layout(UiPillPart *parts, int count);

/**
 * ui_pill_plain_draw() - Draw a plain pill of width @w (from ui_pill_plain_layout()) with its
 * top-left corner at (@x, @y). Paper cost: fill 3, plus 1 per text run and per glyph.
 */
void ui_pill_plain_draw(int x, int y, int w, const UiPillPart *parts, int count);
