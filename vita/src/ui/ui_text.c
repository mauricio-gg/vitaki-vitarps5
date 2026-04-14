/**
 * @file ui_text.c
 * @brief Centralized text rendering helpers for VitaRPS5 UI (Phase 1, issue #127)
 *
 * Eliminates first-frame atlas hitches and baseline jitter by:
 *  1. Pre-warming the vita2d_font glyph atlas for all (codepoint, pt_size)
 *     pairs used by the UI before the first visible frame.
 *  2. Caching ascent and line-height once per pt size via a probe string, so
 *     all call sites share identical, metrics-derived baseline offsets.
 *
 * Phase 2 will migrate the 114 existing vita2d_font_draw_text call sites to
 * use ui_text_draw() / ui_text_draw_centered_v().
 */

#include <vita2d.h>
#include <psp2/kernel/clib.h>

#include "ui/ui_text.h"
#include "ui/ui_constants.h"

/* ============================================================================
 * Named Constants — no magic numbers below this section
 * ============================================================================ */

/*
 * Point sizes to pre-warm.  Derived directly from the FONT_SIZE_* constants
 * in ui_constants.h so that any future size addition is tracked here
 * automatically.
 */
static const int UI_FONT_PREWARM_SIZES[] = {
    FONT_SIZE_SMALL,     /* 14 pt */
    FONT_SIZE_BODY,      /* 16 pt */
    FONT_SIZE_SUBHEADER, /* 18 pt */
    FONT_SIZE_HEADER,    /* 28 pt */
};

/* Number of entries in the regular-font prewarm size table. */
#define UI_FONT_PREWARM_SIZE_COUNT \
  ((int)(sizeof(UI_FONT_PREWARM_SIZES) / sizeof(UI_FONT_PREWARM_SIZES[0])))

/*
 * Compile-time guard: the size_index() switch has exactly 4 cases (one per
 * entry in UI_FONT_PREWARM_SIZES).  If a new size is added to the table this
 * assertion fires immediately, reminding the author to extend size_index().
 */
_Static_assert(UI_FONT_PREWARM_SIZE_COUNT == 4,
               "size_index() switch must be updated to match UI_FONT_PREWARM_SIZES");

/*
 * Monospace font is only used at body and small sizes.  Keeping the prewarm
 * set minimal avoids baking unused glyph atlas rows for sizes that mono never
 * renders at.
 */
static const int UI_FONT_PREWARM_MONO_SIZES[] = {
    FONT_SIZE_SMALL, /* 14 pt */
    FONT_SIZE_BODY,  /* 16 pt */
};

/* Number of entries in the monospace prewarm size table. */
#define UI_FONT_PREWARM_MONO_SIZE_COUNT \
  ((int)(sizeof(UI_FONT_PREWARM_MONO_SIZES) / sizeof(UI_FONT_PREWARM_MONO_SIZES[0])))

/*
 * Character set to bake into the atlas.
 *
 * ASCII printable range 0x20–0x7E followed by the extended glyphs that the UI
 * actually renders (sourced from grepping all non-ASCII literals in
 * vita/src/ui/*.c and vita/src/video_overlay.c):
 *
 *   U+00B0  °   DEGREE SIGN          (ui_screens.c bitrate labels)
 *   U+00B7  ·   MIDDLE DOT           (ui_controller_diagram.c page text)
 *   U+00D7  ×   MULTIPLICATION SIGN  (plan: extended set)
 *   U+2026  …   HORIZONTAL ELLIPSIS  (plan: extended set)
 *   U+2192  →   RIGHTWARDS ARROW     (plan: extended set)
 *   U+2248  ≈   ALMOST EQUAL TO      (ui_screens.c bitrate labels)
 *   U+25A1  □   WHITE SQUARE         (ui_controller_diagram.c button symbols)
 *   U+25B3  △   WHITE UP-POINTING TRIANGLE (ui_controller_diagram.c)
 *   U+25CB  ○   WHITE CIRCLE         (ui_controller_diagram.c)
 *   U+2715  ✕   MULTIPLICATION X     (ui_controller_diagram.c)
 *
 * The string literal uses UTF-8 encoding directly.
 */
static const char UI_FONT_PREWARM_CHARSET[] =
    " !\"#$%&'()*+,-./0123456789:;<=>?"
    "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
    "`abcdefghijklmnopqrstuvwxyz{|}~"
    /* Extended glyphs (UTF-8): */
    "\xC2\xB0"     /* U+00B0  ° */
    "\xC2\xB7"     /* U+00B7  · */
    "\xC3\x97"     /* U+00D7  × */
    "\xE2\x80\xA6" /* U+2026  … */
    "\xE2\x86\x92" /* U+2192  → */
    "\xE2\x89\x88" /* U+2248  ≈ */
    "\xE2\x96\xA1" /* U+25A1  □ */
    "\xE2\x96\xB3" /* U+25B3  △ */
    "\xE2\x97\x8B" /* U+25CB  ○ */
    "\xE2\x9C\x95" /* U+2715  ✕ */
    ;

/*
 * Probe string used to measure ascent and line-height via
 * vita2d_font_text_height().  Contains uppercase, lowercase and a pipe to
 * ensure both ascenders and a tall cap are sampled.
 */
static const char UI_FONT_METRIC_PROBE[] = "Ag|";

/*
 * Y coordinate used when drawing prewarm glyphs off-screen.  Must be well
 * above the top edge (negative) so the Vita GXM scissor clips the draws
 * before they reach the framebuffer.
 */
#define UI_FONT_PREWARM_OFFSCREEN_Y (-256)

/*
 * X coordinate for prewarm draws.  -4096 exceeds any plausible glyph advance
 * width (the widest 28pt glyph is well under 64px), so even the rightmost
 * pixel of a wide character stays left of x=0 and is clipped by the scissor.
 */
#define UI_FONT_PREWARM_OFFSCREEN_X (-4096)

/*
 * Fully transparent colour used for prewarm draws — alpha = 0 means GXM
 * writes nothing to the framebuffer even if the scissor does not clip.
 */
#define UI_FONT_PREWARM_COLOR 0x00000000u

/* ============================================================================
 * Per-size metric cache
 * ============================================================================ */

/*
 * Slot count matches UI_FONT_PREWARM_SIZE_COUNT.  Access is always through
 * size_index() so the layout is an implementation detail.
 */
#define UI_FONT_METRIC_SLOTS UI_FONT_PREWARM_SIZE_COUNT

typedef struct {
  int ascent;      /* Pixels above baseline for this pt size. */
  int line_height; /* Total line height (ascent + descent + leading). */
} FontSizeMetrics;

static FontSizeMetrics s_metrics[UI_FONT_METRIC_SLOTS];

/* ============================================================================
 * Module State
 * ============================================================================ */

static vita2d_font *s_font_regular = NULL;
static vita2d_font *s_font_mono = NULL;
static int s_prewarm_needed = 1; /* 1 until ui_text_prewarm() runs */

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

/**
 * size_index() - Map a pt_size to its slot in s_metrics[].
 * @pt_size: One of the FONT_SIZE_* constants.
 *
 * Returns the table index (0–3), or -1 if pt_size is not a known size.
 * Using an explicit switch rather than a loop keeps the mapping O(1) and
 * makes compiler exhaustiveness warnings possible in the future.
 */
static int size_index(int pt_size) {
  switch (pt_size) {
    case FONT_SIZE_SMALL:
      return 0;
    case FONT_SIZE_BODY:
      return 1;
    case FONT_SIZE_SUBHEADER:
      return 2;
    case FONT_SIZE_HEADER:
      return 3;
    default:
      return -1;
  }
}

/**
 * compute_metrics_for_size() - Measure ascent and line-height for one pt size.
 * @f:       Font to probe (regular font is sufficient; metrics are size-driven).
 * @pt_size: Point size to measure.
 * @slot:    Index into s_metrics[] to populate.
 *
 * vita2d_font_text_height() returns the bounding-box height for the probe
 * string.  For typographic centering purposes we treat that full height as
 * line_height and derive ascent as ~80% of it (the standard ratio for Roboto
 * where the cap-height plus a small internal leading is roughly 80% of the
 * bounding box).  This ratio is chosen empirically for Roboto; it is a fixed
 * constant that does not adapt automatically if a different font is substituted.
 *
 * The factor 4/5 (integer arithmetic) is stored as a named constant to avoid
 * a bare magic literal.
 */
#define UI_FONT_ASCENT_NUMERATOR 4
#define UI_FONT_ASCENT_DENOMINATOR 5

static void compute_metrics_for_size(vita2d_font *f, int pt_size, int slot) {
  int h = (int)vita2d_font_text_height(f, (unsigned int)pt_size, UI_FONT_METRIC_PROBE);
  if (h <= 0) {
    /*
     * vita2d_font_text_height failed or returned zero — fall back to
     * pt_size itself as a safe approximation so callers always get a
     * non-zero metric.
     */
    sceClibPrintf(
        "[WARN] ui_text: vita2d_font_text_height returned %d "
        "for pt_size=%d; using pt_size as fallback\n",
        h, pt_size);
    h = pt_size;
  }
  s_metrics[slot].line_height = h;
  s_metrics[slot].ascent = (h * UI_FONT_ASCENT_NUMERATOR) / UI_FONT_ASCENT_DENOMINATOR;
  sceClibPrintf("[ui_text] size=%d h=%d ascent=%d line=%d (font=%p)\n", pt_size, h,
                s_metrics[slot].ascent, s_metrics[slot].line_height, (const void *)f);
}

/**
 * warn_unknown_size() - Emit a single diagnostic for an unknown pt_size.
 * @caller: Short string identifying the calling function, for context.
 * @pt_size: The unrecognised size value.
 */
static void warn_unknown_size(const char *caller, int pt_size) {
  sceClibPrintf(
      "[WARN] ui_text: %s received unknown pt_size=%d "
      "(expected FONT_SIZE_SMALL/BODY/SUBHEADER/HEADER)\n",
      caller, pt_size);
}

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * ui_text_init() - Store font pointers and arm the deferred prewarm pass.
 * @regular: Proportional font, or NULL (both metric computation and atlas
 *           prewarm are skipped if either pointer is NULL).
 * @mono:    Monospace font, or NULL (see above).
 *
 * Must be called after fonts are loaded and before ui_text_prewarm().
 * This function does NOT compute metrics — that is intentionally deferred to
 * ui_text_prewarm() because some FreeType/GXM paths require an active render
 * pass, which is guaranteed by the caller wrapping ui_text_prewarm() in
 * vita2d_start_drawing / vita2d_end_drawing.
 *
 * Both pointers are borrowed — ownership remains with the caller.
 */
void ui_text_init(vita2d_font *regular, vita2d_font *mono) {
  s_font_regular = regular;
  s_font_mono = mono;

  if (!regular || !mono) {
    sceClibPrintf(
        "[WARN] ui_text_init: NULL font pointer — "
        "skipping metrics and atlas prewarm\n");
    s_prewarm_needed = 0;
    return;
  }

  s_prewarm_needed = 1;
}

/**
 * ui_text_needs_prewarm() - Return 1 if ui_text_prewarm() has not yet run.
 */
int ui_text_needs_prewarm(void) {
  return s_prewarm_needed;
}

/**
 * prewarm_one_font() - Bake all charset glyphs for one font across a set of sizes.
 * @f:          Font to draw with.
 * @sizes:      Array of point sizes to iterate.
 * @size_count: Number of entries in @sizes.
 *
 * Walks UI_FONT_PREWARM_CHARSET byte-by-byte, extracts each UTF-8 sequence
 * into a small stack buffer, and issues a vita2d_font_draw_text call at fully
 * transparent, off-screen coordinates.  This forces FreeType rasterization and
 * the GXM atlas upload without producing any visible output.
 *
 * UTF-8 continuation bytes are validated before copying: if a required
 * continuation byte is NUL (truncated sequence), the leading byte is skipped
 * and iteration continues.  This prevents an OOB read if UI_FONT_PREWARM_CHARSET
 * ever gains a malformed tail.
 */
static void prewarm_one_font(vita2d_font *f, const int *sizes, int size_count) {
  int size_idx;
  const char *p;
  char glyph_buf[8]; /* 4-byte UTF-8 max + NUL = 5 needed; 8 for alignment/safety */

  for (size_idx = 0; size_idx < size_count; size_idx++) {
    int pt = sizes[size_idx];

    for (p = UI_FONT_PREWARM_CHARSET; *p != '\0';) {
      unsigned char lead = (unsigned char)*p;
      int seq_len;

      /* Determine UTF-8 sequence length from the leading byte. */
      if (lead < 0x80u) {
        seq_len = 1;
      } else if (lead < 0xC0u) {
        /*
         * Defensive: bytes in [0x80, 0xC0) are UTF-8 continuation bytes and
         * are malformed as sequence starters.  Skip one byte to re-sync.
         * Cannot fire with the current static charset but guards against any
         * future extension that introduces a bad byte.
         */
        p++;
        continue;
      } else if (lead < 0xE0u) {
        seq_len = 2;
      } else if (lead < 0xF0u) {
        seq_len = 3;
      } else {
        seq_len = 4;
      }

      /*
       * Bounds-check: verify that each required continuation byte is a valid
       * UTF-8 continuation byte in the range [0x80, 0xBF].  A byte outside
       * that range (including NUL, which signals a truncated charset string,
       * or any value >= 0xC0, which would be a spurious new leading byte)
       * indicates a malformed sequence.  Skip the leading byte and re-sync
       * rather than copying garbage to glyph_buf.
       */
      if (seq_len > 1 && ((unsigned char)p[1] < 0x80u || (unsigned char)p[1] > 0xBFu)) {
        p++;
        continue;
      }
      if (seq_len > 2 && ((unsigned char)p[2] < 0x80u || (unsigned char)p[2] > 0xBFu)) {
        p++;
        continue;
      }
      if (seq_len > 3 && ((unsigned char)p[3] < 0x80u || (unsigned char)p[3] > 0xBFu)) {
        p++;
        continue;
      }

      /* Copy the validated sequence into the local buffer and NUL-terminate. */
      glyph_buf[0] = p[0];
      if (seq_len > 1)
        glyph_buf[1] = p[1];
      if (seq_len > 2)
        glyph_buf[2] = p[2];
      if (seq_len > 3)
        glyph_buf[3] = p[3];
      glyph_buf[seq_len] = '\0';

      vita2d_font_draw_text(f, UI_FONT_PREWARM_OFFSCREEN_X, UI_FONT_PREWARM_OFFSCREEN_Y,
                            UI_FONT_PREWARM_COLOR, (unsigned int)pt, glyph_buf);

      p += seq_len;
    }
  }
}

/**
 * ui_text_prewarm() - Rasterize all (codepoint, pt_size) pairs into the atlas.
 *
 * Must be called from within an active vita2d_start_drawing() /
 * vita2d_end_drawing() pair on the render thread.  Draws each character
 * individually at UI_FONT_PREWARM_OFFSCREEN_Y with alpha=0 to trigger
 * FreeType rasterization and GPU atlas upload without visible output.
 *
 * Iterates:
 *   - UI_FONT_PREWARM_SIZES x UI_FONT_PREWARM_CHARSET for s_font_regular (4 sizes)
 *   - UI_FONT_PREWARM_MONO_SIZES x UI_FONT_PREWARM_CHARSET for s_font_mono (2 sizes)
 *
 * Metrics (ascent, line-height) are derived from s_font_regular only.
 * Roboto Regular and RobotoMono share the same UPM and ascender, so a single
 * canonical measurement per pt_size is sufficient for all font faces.
 *
 * Each multibyte UTF-8 sequence is drawn as a single call so vita2d's internal
 * UTF-8 decoder sees the full codepoint.
 */
void ui_text_prewarm(void) {
  int i;

  if (!s_font_regular || !s_font_mono) {
    sceClibPrintf("[WARN] ui_text_prewarm: called before ui_text_init()\n");
    return;
  }

  /*
   * Measure ascent and line-height here rather than in ui_text_init() because
   * some FreeType/GXM code paths rasterize internally and require an active
   * render pass; callers wrap this function in vita2d_start_drawing /
   * vita2d_end_drawing, guaranteeing that context is present.
   *
   * Regular font is the single canonical source for metrics — Roboto Regular
   * and RobotoMono share the same UPM/ascender, so there is no need to
   * re-measure with the mono face (which would clobber the same s_metrics[]
   * slots and risk writing stale values over freshly computed ones).
   */
  for (i = 0; i < UI_FONT_PREWARM_SIZE_COUNT; i++) {
    compute_metrics_for_size(s_font_regular, UI_FONT_PREWARM_SIZES[i], i);
  }

  /* --- Bake regular font: all four prewarm sizes --- */
  prewarm_one_font(s_font_regular, UI_FONT_PREWARM_SIZES, UI_FONT_PREWARM_SIZE_COUNT);

  /* --- Bake mono font: body and small sizes only --- */
  prewarm_one_font(s_font_mono, UI_FONT_PREWARM_MONO_SIZES, UI_FONT_PREWARM_MONO_SIZE_COUNT);

  s_prewarm_needed = 0;
}

/**
 * ui_text_draw() - Draw a UTF-8 string at integer-floored screen coordinates.
 */
void ui_text_draw(vita2d_font *f, int x, int baseline_y, unsigned int color, int pt_size,
                  const char *s) {
  if (!f) {
    sceClibPrintf("[WARN] ui_text_draw: NULL font pointer\n");
    return;
  }
  if (!s)
    return;
  if (size_index(pt_size) < 0) {
    warn_unknown_size("ui_text_draw", pt_size);
    return;
  }
  vita2d_font_draw_text(f, x, baseline_y, color, (unsigned int)pt_size, s);
}

/**
 * ui_text_width() - Return the pixel width of a UTF-8 string.
 */
int ui_text_width(vita2d_font *f, int pt_size, const char *s) {
  if (!f) {
    sceClibPrintf("[WARN] ui_text_width: NULL font pointer\n");
    return 0;
  }
  if (!s)
    return 0;
  if (size_index(pt_size) < 0) {
    warn_unknown_size("ui_text_width", pt_size);
    return 0;
  }
  return (int)vita2d_font_text_width(f, (unsigned int)pt_size, s);
}

/**
 * ui_text_ascent() - Return cached ascent (pixels above baseline) for pt_size.
 */
int ui_text_ascent(int pt_size) {
  int idx = size_index(pt_size);
  if (idx < 0) {
    warn_unknown_size("ui_text_ascent", pt_size);
    return 0;
  }
  return s_metrics[idx].ascent;
}

/**
 * ui_text_line_height() - Return cached line height in pixels for pt_size.
 */
int ui_text_line_height(int pt_size) {
  int idx = size_index(pt_size);
  if (idx < 0) {
    warn_unknown_size("ui_text_line_height", pt_size);
    return 0;
  }
  return s_metrics[idx].line_height;
}

/**
 * ui_text_draw_centered_v() - Draw a string vertically centred in a box.
 *
 * Baseline is computed from the cached ascent:
 *   baseline_y = box_y + (box_h + ascent) / 2
 *
 * This replaces ad-hoc magic offsets like "+5" / "+6" at individual call sites.
 */
void ui_text_draw_centered_v(vita2d_font *f, int x, int box_y, int box_h, unsigned int color,
                             int pt_size, const char *s) {
  int ascent;
  int baseline_y;
  int idx;

  if (!f) {
    sceClibPrintf("[WARN] ui_text_draw_centered_v: NULL font pointer\n");
    return;
  }
  if (!s)
    return;

  idx = size_index(pt_size);
  if (idx < 0) {
    warn_unknown_size("ui_text_draw_centered_v", pt_size);
    return;
  }

  ascent = s_metrics[idx].ascent;
  baseline_y = box_y + (box_h + ascent) / 2;

  vita2d_font_draw_text(f, x, baseline_y, color, (unsigned int)pt_size, s);
}
