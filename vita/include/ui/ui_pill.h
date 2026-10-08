/**
 * @file ui_pill.h
 * @brief C19 Pill: a 32 px pill with one line of text (SPEC.md C19)
 *
 * Display-only draw helper. HUD fill, T16 TEXT, pill caps, 16 px side padding.
 * Variants: warn (1 px WARN outline, the cooldown banner) and unstable (a 12 px ERR
 * dot, the whole pill pulsing over UI_PILL_PULSE_MS). The plain variant arrives with
 * the stream overlay.
 */

#pragma once

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
