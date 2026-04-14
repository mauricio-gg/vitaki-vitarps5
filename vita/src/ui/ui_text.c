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
 * X coordinate for prewarm draws — any non-positive value is fine.
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
 * ui_text_init() - Store font pointers; metric computation is deferred to
 *                  ui_text_prewarm().
 * @regular: Proportional font, or NULL (prewarm will be skipped if NULL).
 * @mono:    Monospace font, or NULL (prewarm will be skipped if NULL).
 *
 * Must be called after fonts are loaded and before ui_text_prewarm().
 * Metric computation is intentionally deferred: some FreeType/GXM paths
 * need an active render pass, which is guaranteed by the caller wrapping
 * ui_text_prewarm() in vita2d_start_drawing / vita2d_end_drawing.
 */
void ui_text_init(vita2d_font *regular, vita2d_font *mono) {
  s_font_regular = regular;
  s_font_mono = mono;

  if (!regular || !mono) {
    sceClibPrintf("[WARN] ui_text_init: NULL font, skipping metric compute\n");
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
 * Each multibyte UTF-8 sequence is drawn as a single call so vita2d's internal
 * UTF-8 decoder sees the full codepoint.
 */
void ui_text_prewarm(void) {
  /*
   * Glyph-by-glyph iteration: walk the charset byte-by-byte and extract
   * each UTF-8 sequence into a small NUL-terminated buffer for drawing.
   *
   * UTF-8 sequence lengths:
   *   1-byte: 0x00–0x7F
   *   2-byte: 0xC0–0xDF start
   *   3-byte: 0xE0–0xEF start
   *   4-byte: 0xF0–0xF7 start (not in our charset, included for safety)
   */
  int i;
  int size_idx;
  const char *p;
  char glyph_buf[8]; /* Largest UTF-8 sequence is 4 bytes + NUL + padding. */

  if (!s_font_regular || !s_font_mono) {
    sceClibPrintf("[WARN] ui_text_prewarm: called before ui_text_init()\n");
    return;
  }

  /*
   * Measure ascent and line-height here rather than in ui_text_init() because
   * some FreeType/GXM code paths rasterize internally and require an active
   * render pass; callers wrap this function in vita2d_start_drawing /
   * vita2d_end_drawing, guaranteeing that context is present.
   */
  for (i = 0; i < UI_FONT_PREWARM_SIZE_COUNT; i++) {
    compute_metrics_for_size(s_font_regular, UI_FONT_PREWARM_SIZES[i], i);
  }
  for (i = 0; i < UI_FONT_PREWARM_MONO_SIZE_COUNT; i++) {
    int slot = size_index(UI_FONT_PREWARM_MONO_SIZES[i]);
    if (slot >= 0)
      compute_metrics_for_size(s_font_mono, UI_FONT_PREWARM_MONO_SIZES[i], slot);
  }

  /* --- Bake regular font: all four prewarm sizes --- */
  for (size_idx = 0; size_idx < UI_FONT_PREWARM_SIZE_COUNT; size_idx++) {
    int pt = UI_FONT_PREWARM_SIZES[size_idx];

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

      /* Copy the sequence into the local buffer and NUL-terminate. */
      glyph_buf[0] = p[0];
      if (seq_len > 1)
        glyph_buf[1] = p[1];
      if (seq_len > 2)
        glyph_buf[2] = p[2];
      if (seq_len > 3)
        glyph_buf[3] = p[3];
      glyph_buf[seq_len] = '\0';

      vita2d_font_draw_text(s_font_regular, UI_FONT_PREWARM_OFFSCREEN_X,
                            UI_FONT_PREWARM_OFFSCREEN_Y, UI_FONT_PREWARM_COLOR, (unsigned int)pt,
                            glyph_buf);

      p += seq_len;
    }
  }

  /* --- Bake mono font: body and small sizes only --- */
  for (size_idx = 0; size_idx < UI_FONT_PREWARM_MONO_SIZE_COUNT; size_idx++) {
    int pt = UI_FONT_PREWARM_MONO_SIZES[size_idx];

    for (p = UI_FONT_PREWARM_CHARSET; *p != '\0';) {
      unsigned char lead = (unsigned char)*p;
      int seq_len;

      if (lead < 0x80u) {
        seq_len = 1;
      } else if (lead < 0xC0u) {
        p++;
        continue;
      } else if (lead < 0xE0u) {
        seq_len = 2;
      } else if (lead < 0xF0u) {
        seq_len = 3;
      } else {
        seq_len = 4;
      }

      glyph_buf[0] = p[0];
      if (seq_len > 1)
        glyph_buf[1] = p[1];
      if (seq_len > 2)
        glyph_buf[2] = p[2];
      if (seq_len > 3)
        glyph_buf[3] = p[3];
      glyph_buf[seq_len] = '\0';

      vita2d_font_draw_text(s_font_mono, UI_FONT_PREWARM_OFFSCREEN_X, UI_FONT_PREWARM_OFFSCREEN_Y,
                            UI_FONT_PREWARM_COLOR, (unsigned int)pt, glyph_buf);

      p += seq_len;
    }
  }

  s_prewarm_needed = 0;
}

/**
 * ui_text_draw() - Draw a UTF-8 string at integer-floored screen coordinates.
 */
void ui_text_draw(vita2d_font *f, int x, int baseline_y, unsigned int color, int pt_size,
                  const char *s) {
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
  int idx = size_index(pt_size);

  if (idx < 0) {
    warn_unknown_size("ui_text_draw_centered_v", pt_size);
    return;
  }

  ascent = s_metrics[idx].ascent;
  baseline_y = box_y + (box_h + ascent) / 2;

  vita2d_font_draw_text(f, x, baseline_y, color, (unsigned int)pt_size, s);
}
