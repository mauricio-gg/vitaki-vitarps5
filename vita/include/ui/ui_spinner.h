/**
 * @file ui_spinner.h
 * @brief C16 Spinner: a 270 degree arc, 2 px, TEXT colour (SPEC.md C16, section 1.3)
 *
 * Display-only draw helper. Each size is baked once into a texture and rotated at draw time,
 * so a spinner is one draw. The large one turns once per 1.8 s, the small one once per 0.8 s.
 */

#pragma once

/** Spinner sizes. */
typedef enum ui_spinner_size_t {
  UI_SPINNER_LARGE = 0,  ///< 176 px, the Connecting art
  UI_SPINNER_INLINE,     ///< 16 px, a step marker
  UI_SPINNER_COUNT
} UiSpinnerSize;

/** ui_spinner_init() - Bake the arcs. Call once at start-up, after vita2d is initialised. */
void ui_spinner_init(void);

/** ui_spinner_draw() - Draw spinner @size centred at (@cx, @cy), rotated for the time now. */
void ui_spinner_draw(UiSpinnerSize size, int cx, int cy);
