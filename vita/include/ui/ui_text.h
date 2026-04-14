/**
 * @file ui_text.h
 * @brief Centralized text rendering helpers for VitaRPS5 UI
 *
 * Provides:
 *  - Atlas pre-warming at init time so every (codepoint, pt_size) pair is
 *    rasterized before the first real frame, eliminating cold-atlas hitches.
 *  - Per-size metric cache (ascent, line-height) populated once from a probe
 *    string via vita2d_font_text_height, so all call sites share identical
 *    baselines instead of ad-hoc +5/+6 magic offsets.
 *  - Thin wrappers around vita2d_font_draw_text / vita2d_font_text_width that
 *    guarantee integer coordinates.
 *  - Vertical-center helper that derives the baseline from cached ascent rather
 *    than a literal offset.
 *
 * Phase 1 of issue #127.  Call sites are migrated in phase 2.
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

/* ============================================================================
 * Initialization & Warm-up
 * ============================================================================ */

/**
 * ui_text_init() - Store font pointers and arm the deferred prewarm pass.
 * @regular: Proportional font loaded by init_ui() (Roboto-Regular.ttf).
 * @mono:    Monospace font loaded by init_ui() (RobotoMono-Regular.ttf).
 *
 * Must be called after fonts are loaded and before ui_text_prewarm().
 * Both pointers are borrowed — ownership remains with the caller.
 *
 * This function does NOT compute metrics or pre-warm the atlas — that is
 * intentionally deferred to ui_text_prewarm() because some FreeType/GXM
 * paths require an active render pass.  If either font pointer is NULL,
 * both metric computation and atlas prewarm are skipped.
 */
void ui_text_init(vita2d_font *regular, vita2d_font *mono);

/**
 * ui_text_needs_prewarm() - True until ui_text_prewarm() has been called.
 *
 * Use this flag in the first iteration of draw_ui() to schedule the warm-up
 * pass inside an active vita2d_start_drawing / vita2d_end_drawing pair.
 */
int ui_text_needs_prewarm(void);

/**
 * ui_text_prewarm() - Force-rasterize all (codepoint, pt_size) pairs.
 *
 * MUST be called from within a vita2d_start_drawing() / vita2d_end_drawing()
 * pair on the render thread so that texture uploads are committed.
 *
 * Iterates UI_FONT_PREWARM_SIZES x UI_FONT_PREWARM_CHARSET for the regular
 * font (6 sizes) and UI_FONT_PREWARM_MONO_SIZES x UI_FONT_PREWARM_CHARSET for
 * the mono font (2 sizes).  Draws each character at alpha=0 at off-screen
 * coordinates so glyphs are baked into the atlas without appearing on screen.
 * Also measures per-size metrics (ascent, line-height) while inside the active
 * render pass.
 */
void ui_text_prewarm(void);

/* ============================================================================
 * Drawing & Measurement
 * ============================================================================ */

/**
 * ui_text_draw() - Draw a UTF-8 string at integer coordinates.
 * @f:          Font pointer (regular or mono).
 * @x:          Left edge of the first glyph, in screen pixels.
 * @baseline_y: Baseline Y coordinate, in screen pixels.
 * @color:      ABGR colour value (e.g. RGBA8(r,g,b,a)).
 * @pt_size:    Point size — one of the six supported pt sizes (14/16/18/20/24/28);
 *              20 and 24 are literal values since they have no FONT_SIZE_* constant yet.
 * @s:          NUL-terminated UTF-8 string to render.
 *
 * If pt_size is not a known size, emits a sceClibPrintf warning and returns
 * without drawing.
 */
void ui_text_draw(vita2d_font *f, int x, int baseline_y, unsigned int color, int pt_size,
                  const char *s);

/**
 * ui_text_width() - Return the pixel width of a UTF-8 string.
 * @f:       Font pointer.
 * @pt_size: Point size — one of the six supported pt sizes (14/16/18/20/24/28);
 *           20 and 24 are literal values since they have no FONT_SIZE_* constant yet.
 * @s:       NUL-terminated UTF-8 string.
 *
 * Returns 0 and logs a warning if pt_size is unknown.
 */
int ui_text_width(vita2d_font *f, int pt_size, const char *s);

/* ============================================================================
 * Metric Queries (populated from cached probe measurement at init)
 * ============================================================================ */

/**
 * ui_text_ascent() - Return the ascent (pixels above baseline) for pt_size.
 * @pt_size: One of the six supported pt sizes (14/16/18/20/24/28); 20 and 24
 *           are literal values since they have no FONT_SIZE_* constant yet.
 *
 * Returns an approximation (~80% of the vita2d text-height bounding box,
 * chosen empirically for Roboto). Revisit if the font face changes.
 *
 * Metrics are derived from the regular font; valid for any face sharing the
 * same UPM/ascender (Roboto Regular and RobotoMono in this build). Swapping
 * to a different family requires re-measuring.
 *
 * Returns 0 and logs a warning for unknown sizes.
 */
int ui_text_ascent(int pt_size);

/**
 * ui_text_line_height() - Return the full line height in pixels for pt_size.
 * @pt_size: One of the six supported pt sizes (14/16/18/20/24/28); 20 and 24
 *           are literal values since they have no FONT_SIZE_* constant yet.
 *
 * Metrics are derived from the regular font; valid for any face sharing the
 * same UPM/ascender (Roboto Regular and RobotoMono in this build). Swapping
 * to a different family requires re-measuring.
 *
 * Returns 0 and logs a warning for unknown sizes.
 */
int ui_text_line_height(int pt_size);

/* ============================================================================
 * Layout Helpers
 * ============================================================================ */

/**
 * ui_text_draw_centered_v() - Draw a string vertically centred in a box.
 * @f:       Font pointer.
 * @x:       Left edge X, in screen pixels.
 * @box_y:   Top edge of the bounding box, in screen pixels.
 * @box_h:   Height of the bounding box, in screen pixels.
 * @color:   ABGR colour value.
 * @pt_size: Point size — one of the six supported pt sizes (14/16/18/20/24/28);
 *           20 and 24 are literal values since they have no FONT_SIZE_* constant yet.
 * @s:       NUL-terminated UTF-8 string to render.
 *
 * Baseline is computed as: box_y + (box_h + ascent) / 2
 * using integer arithmetic and the cached ascent for pt_size.
 */
void ui_text_draw_centered_v(vita2d_font *f, int x, int box_y, int box_h, unsigned int color,
                             int pt_size, const char *s);
