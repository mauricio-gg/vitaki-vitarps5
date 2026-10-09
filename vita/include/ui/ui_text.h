/**
 * @file ui_text.h
 * @brief Text rendering for the VitaRPS5 UI: the five SPEC type faces
 *
 * Provides:
 *  - Atlas pre-warming at init time so every glyph of every face is
 *    rasterized before the first real frame, eliminating cold-atlas hitches.
 *  - Per-face metric cache (ascent, line-height) populated once from a probe
 *    string via vita2d_font_text_height, so all call sites share identical
 *    baselines instead of ad-hoc +5/+6 magic offsets.
 *  - Draw, measure and vertical-centre helpers per face.
 *
 * Thread-safety: all functions must be called from the render thread. The
 * module is not thread-safe and shares state with vita2d_font, which itself
 * is render-thread-only.
 *
 * Lifecycle: there is no explicit deinit. Reloading fonts at runtime requires
 * calling ui_text_init() again followed by ui_text_prewarm() on the next
 * render pass.
 */

#pragma once

#include <vita2d.h>
#include "ui/ui_constants.h"
#include "ui/ui_theme.h"

/* ============================================================================
 * Initialization & Warm-up
 * ============================================================================ */

/**
 * ui_text_init() - Store font pointers and arm the deferred prewarm pass.
 * @regular: Proportional font loaded by init_ui() (Roboto-Regular.ttf).
 * @light:   Light font loaded by init_ui() (Roboto-Light.ttf), used by the SPEC
 *           faces T20/T28/T40.  May be NULL: those faces then draw in Regular.
 *
 * Must be called after fonts are loaded and before ui_text_prewarm().
 * Both pointers are borrowed — ownership remains with the caller.
 *
 * This function does NOT compute metrics or pre-warm the atlas — that is
 * intentionally deferred to ui_text_prewarm() because some FreeType/GXM
 * paths require an active render pass.  If either font pointer is NULL,
 * both metric computation and atlas prewarm are skipped.
 */
void ui_text_init(vita2d_font *regular, vita2d_font *light);

/**
 * ui_text_needs_prewarm() - True until ui_text_prewarm() has been called.
 *
 * Use this flag in the first iteration of draw_ui() to schedule the warm-up
 * pass inside an active vita2d_start_drawing / vita2d_end_drawing pair.
 */
int ui_text_needs_prewarm(void);

/**
 * ui_text_prewarm() - Force-rasterize every face's glyphs.
 *
 * MUST be called from within a vita2d_start_drawing() / vita2d_end_drawing()
 * pair on the render thread so that texture uploads are committed.
 *
 * Bakes the charset for each face in the font that draws it, at alpha=0 and
 * off-screen coordinates so glyphs reach the atlas without appearing on screen.
 * Also measures per-face metrics (ascent, line-height) while inside the active
 * render pass.
 */
void ui_text_prewarm(void);

/* ============================================================================
 * SPEC type faces (ui_theme.h: T14, T16, T20_REGULAR Regular; T20, T28, T40 Light)
 * ============================================================================ */

/**
 * ui_text_draw_face() - Draw a string in one of the SPEC faces.
 * @face:       a UiFace (UI_FACE_T14 .. UI_FACE_T20_REGULAR).
 * @x:          Left edge of the first glyph, in screen pixels.
 * @baseline_y: Baseline Y coordinate, in screen pixels.
 * @color:      ABGR colour value.
 * @s:          NUL-terminated UTF-8 string.
 */
void ui_text_draw_face(UiFace face, int x, int baseline_y, unsigned int color, const char *s);

/** ui_text_face_width() - Pixel width of @s in @face; 0 for an unknown face. */
int ui_text_face_width(UiFace face, const char *s);

/**
 * ui_text_draw_face_centered_v() - Draw a string in @face vertically centred in a box.
 * @face:  a UiFace (UI_FACE_T14 .. UI_FACE_T20_REGULAR).
 * @x:     Left edge X, in screen pixels.
 * @box_y: Top edge of the box, in screen pixels.
 * @box_h: Height of the box, in screen pixels.
 * @color: ABGR colour value.
 * @s:     NUL-terminated UTF-8 string.
 */
void ui_text_draw_face_centered_v(UiFace face, int x, int box_y, int box_h, unsigned int color,
                                  const char *s);

/** ui_text_face_line_height() - SPEC line height of @face in pixels; 0 for an unknown face. */
int ui_text_face_line_height(UiFace face);
