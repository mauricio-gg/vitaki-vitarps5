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
 * Point sizes to pre-warm.  Every entry is a FONT_SIZE_* constant from
 * ui_constants.h.  When adding a new size, update this table, the
 * _Static_assert literal, and the size_index() switch in lockstep.
 */
static const int UI_FONT_PREWARM_SIZES[] = {
    FONT_SIZE_SMALL,       /* 14 pt */
    FONT_SIZE_BODY,        /* 16 pt */
    FONT_SIZE_SUBHEADER,   /* 18 pt */
    FONT_SIZE_CARD_TITLE,  /* 20 pt */
    FONT_SIZE_HOME_HEADER, /* 24 pt */
    FONT_SIZE_HEADER,      /* 28 pt */
    FONT_SIZE_PIN_DIGIT,   /* 40 pt */
};

/* Number of entries in the regular-font prewarm size table. */
#define UI_FONT_PREWARM_SIZE_COUNT \
  ((int)(sizeof(UI_FONT_PREWARM_SIZES) / sizeof(UI_FONT_PREWARM_SIZES[0])))

/*
 * Compile-time guard: the size_index() switch has exactly 7 cases (one per
 * entry in UI_FONT_PREWARM_SIZES).  If a new size is added to the table this
 * assertion fires immediately, reminding the author to extend size_index().
 */
_Static_assert(UI_FONT_PREWARM_SIZE_COUNT == 7,
               "UI_FONT_PREWARM_SIZES changed: update size_index() switch AND this assert literal");

/*
 * Monospace font is used at small/body/subheader sizes (14/16/18 pt).  Keeping
 * the prewarm set minimal avoids baking unused glyph atlas rows for sizes that
 * mono never renders at.
 */
static const int UI_FONT_PREWARM_MONO_SIZES[] = {
    FONT_SIZE_SMALL,     /* 14 pt */
    FONT_SIZE_BODY,      /* 16 pt */
    FONT_SIZE_SUBHEADER, /* 18 pt — message log mono block */
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

/*
 * Ascent approximation ratio: ascent = (line_height * NUMERATOR) / DENOMINATOR.
 *
 * Chosen empirically for Roboto where the cap-height plus a small internal
 * leading is roughly 80% (4/5) of the bounding-box height returned by
 * vita2d_font_text_height().  This is a fixed constant; if the font face
 * changes, re-measure and update both macros together.
 */
#define UI_FONT_ASCENT_NUMERATOR 4
#define UI_FONT_ASCENT_DENOMINATOR 5

/*
 * Stack buffer size for a single UTF-8 glyph sequence plus NUL terminator.
 * UTF-8 encodes any codepoint in at most 4 bytes; 8 bytes gives alignment
 * headroom and is the size already established in the original prewarm code.
 */
#define UI_FONT_UTF8_SEQ_BUFFER_BYTES 8

/* ============================================================================
 * Per-size metric cache
 * ============================================================================ */

/*
 * Array length is UI_FONT_PREWARM_SIZE_COUNT.  Access is always through
 * size_index() so the layout is an implementation detail.
 */
typedef struct {
  int ascent;      /* Pixels above baseline for this pt size. */
  int line_height; /* Total line height (ascent + descent + leading). */
} FontSizeMetrics;

static FontSizeMetrics s_metrics[UI_FONT_PREWARM_SIZE_COUNT];

/* ============================================================================
 * Module State
 * ============================================================================ */

static vita2d_font *s_font_regular = NULL;
static vita2d_font *s_font_mono = NULL;
static int s_prewarm_needed = 0; /* armed to 1 only after a successful ui_text_init() */

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

/**
 * size_index() - Map a pt_size to its slot in s_metrics[].
 * @pt_size: One of the FONT_SIZE_* constants.
 *
 * Returns the table index (0–6), or -1 if pt_size is not a known size.
 * Using an explicit switch rather than a loop keeps the mapping O(1) and
 * makes compiler exhaustiveness warnings possible in the future.
 *
 * Index assignment must mirror UI_FONT_PREWARM_SIZES[] order exactly:
 *   0 = 14 pt (FONT_SIZE_SMALL)
 *   1 = 16 pt (FONT_SIZE_BODY)
 *   2 = 18 pt (FONT_SIZE_SUBHEADER)
 *   3 = 20 pt (FONT_SIZE_CARD_TITLE)
 *   4 = 24 pt (FONT_SIZE_HOME_HEADER)
 *   5 = 28 pt (FONT_SIZE_HEADER)
 *   6 = 40 pt (FONT_SIZE_PIN_DIGIT)
 */
static int size_index(int pt_size) {
  switch (pt_size) {
    case FONT_SIZE_SMALL:
      return 0;
    case FONT_SIZE_BODY:
      return 1;
    case FONT_SIZE_SUBHEADER:
      return 2;
    case FONT_SIZE_CARD_TITLE:
      return 3;
    case FONT_SIZE_HOME_HEADER:
      return 4;
    case FONT_SIZE_HEADER:
      return 5;
    case FONT_SIZE_PIN_DIGIT:
      return 6;
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
 * The factor 4/5 (integer arithmetic) is expressed via the named constants
 * UI_FONT_ASCENT_NUMERATOR / UI_FONT_ASCENT_DENOMINATOR defined in the
 * constants section above.
 */
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
#ifndef NDEBUG
  sceClibPrintf("[ui_text] size=%d h=%d ascent=%d line=%d (font=%p)\n", pt_size, h,
                s_metrics[slot].ascent, s_metrics[slot].line_height, (const void *)f);
#endif
}

/**
 * warn_unknown_size() - Emit a single diagnostic for an unknown pt_size.
 * @caller: Short string identifying the calling function, for context.
 * @pt_size: The unrecognised size value.
 */
static void warn_unknown_size(const char *caller, int pt_size) {
  sceClibPrintf(
      "[WARN] ui_text: %s received unknown pt_size=%d "
      "(expected FONT_SIZE_SMALL/BODY/SUBHEADER/CARD_TITLE/HOME_HEADER/HEADER)\n",
      caller, pt_size);
}

/**
 * utf8_extract() - Consume one UTF-8 sequence from *pp into out_buf.
 * @pp:      Pointer to the current read position in the source string.
 *           Advanced past the consumed bytes on return.
 * @out_buf: Caller-provided buffer of at least UI_FONT_UTF8_SEQ_BUFFER_BYTES
 *           bytes.  Receives the NUL-terminated sequence on success.
 *
 * Returns the byte length of the extracted sequence (1–4) on success, or 0
 * when *pp points at the NUL terminator (end of string).  On a malformed
 * leading byte (a bare continuation byte 0x80–0xBF in start position, or a
 * sequence whose required continuation bytes are absent or invalid), advances
 * *pp by one byte and returns -1 so the caller can skip and continue.
 *
 * This is the single canonical UTF-8 decoder used by prewarm_one_font();
 * no other caller duplicates this logic.
 */
static int utf8_extract(const char **pp, char *out_buf) {
  const char *p = *pp;
  unsigned char lead = (unsigned char)*p;
  int seq_len;

  if (lead == '\0')
    return 0;

  /* Determine sequence length from the leading byte. */
  if (lead < 0x80u) {
    seq_len = 1;
  } else if (lead < 0xC0u) {
    /* Bare continuation byte in leading position — skip one byte to re-sync. */
    *pp = p + 1;
    return -1;
  } else if (lead < 0xE0u) {
    seq_len = 2;
  } else if (lead < 0xF0u) {
    seq_len = 3;
  } else {
    seq_len = 4;
  }

  /*
   * Validate continuation bytes.  A byte outside [0x80, 0xBF] (including NUL,
   * which signals a truncated string, or any value >= 0xC0) means the sequence
   * is malformed.  Skip the leading byte and re-sync rather than forwarding
   * garbage to the caller.
   */
  if (seq_len > 1 && ((unsigned char)p[1] < 0x80u || (unsigned char)p[1] > 0xBFu)) {
    *pp = p + 1;
    return -1;
  }
  if (seq_len > 2 && ((unsigned char)p[2] < 0x80u || (unsigned char)p[2] > 0xBFu)) {
    *pp = p + 1;
    return -1;
  }
  if (seq_len > 3 && ((unsigned char)p[3] < 0x80u || (unsigned char)p[3] > 0xBFu)) {
    *pp = p + 1;
    return -1;
  }

  /* Copy the validated sequence and NUL-terminate. */
  out_buf[0] = p[0];
  if (seq_len > 1)
    out_buf[1] = p[1];
  if (seq_len > 2)
    out_buf[2] = p[2];
  if (seq_len > 3)
    out_buf[3] = p[3];
  out_buf[seq_len] = '\0';

  *pp = p + seq_len;
  return seq_len;
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
 * Walks UI_FONT_PREWARM_CHARSET via utf8_extract(), issuing a
 * vita2d_font_draw_text call per glyph at fully transparent, off-screen
 * coordinates.  This forces FreeType rasterization and GXM atlas upload
 * without producing any visible output.
 */
static void prewarm_one_font(vita2d_font *f, const int *sizes, int size_count) {
  char glyph_buf[UI_FONT_UTF8_SEQ_BUFFER_BYTES];
  int size_idx;

  for (size_idx = 0; size_idx < size_count; size_idx++) {
    int pt = sizes[size_idx];
    const char *p = UI_FONT_PREWARM_CHARSET;
    int extracted;

    while ((extracted = utf8_extract(&p, glyph_buf)) != 0) {
      if (extracted < 0)
        continue;

      vita2d_font_draw_text(f, UI_FONT_PREWARM_OFFSCREEN_X, UI_FONT_PREWARM_OFFSCREEN_Y,
                            UI_FONT_PREWARM_COLOR, (unsigned int)pt, glyph_buf);
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
 *   - UI_FONT_PREWARM_SIZES x UI_FONT_PREWARM_CHARSET for s_font_regular (6 sizes)
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

  /* --- Bake regular font: all six prewarm sizes --- */
  prewarm_one_font(s_font_regular, UI_FONT_PREWARM_SIZES, UI_FONT_PREWARM_SIZE_COUNT);

  /* --- Bake mono font: body and small sizes only --- */
  prewarm_one_font(s_font_mono, UI_FONT_PREWARM_MONO_SIZES, UI_FONT_PREWARM_MONO_SIZE_COUNT);

  s_prewarm_needed = 0;
}

/**
 * ui_text_draw() - Draw a UTF-8 string as a single whole-string call.
 *
 * Issues one vita2d_font_draw_text call for the entire string, preserving
 * vita2d's internal kerning pairs.  Sub-pixel placement aliasing is handled
 * by the atlas LINEAR (bilinear) texture filter applied at init time rather
 * than by integer-snapping individual glyph origins.
 */
void ui_text_draw(vita2d_font *f, int x, int baseline_y, unsigned int color, int pt_size,
                  const char *s) {
  if (!f) {
    sceClibPrintf("[WARN] ui_text_draw: NULL font pointer\n");
    return;
  }
  if (!s)
    return;
  /* Guard ensures pt_size is in UI_FONT_PREWARM_SIZES; catches forgotten new sizes early. */
  if (size_index(pt_size) < 0) {
    warn_unknown_size("ui_text_draw", pt_size);
    return;
  }
  vita2d_font_draw_text(f, x, baseline_y, color, (unsigned int)pt_size, s);
}

/**
 * ui_text_width() - Return the pixel width of a UTF-8 string.
 *
 * Delegates to vita2d_font_text_width() for whole-string measurement,
 * matching the kerning-aware advance that ui_text_draw() produces.
 * Centering math in callers is therefore exact.
 */
int ui_text_width(vita2d_font *f, int pt_size, const char *s) {
  if (!f) {
    sceClibPrintf("[WARN] ui_text_width: NULL font pointer\n");
    return 0;
  }
  if (!s)
    return 0;
  /* Guard ensures pt_size is in UI_FONT_PREWARM_SIZES; catches forgotten new sizes early. */
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
 * Delegates to ui_text_draw() which issues a single whole-string
 * vita2d_font_draw_text call with kerning intact.
 */
void ui_text_draw_centered_v(vita2d_font *f, int x, int box_y, int box_h, unsigned int color,
                             int pt_size, const char *s) {
  int idx;
  int baseline_y;

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

  baseline_y = box_y + (box_h + s_metrics[idx].ascent) / 2;
  ui_text_draw(f, x, baseline_y, color, pt_size, s);
}
