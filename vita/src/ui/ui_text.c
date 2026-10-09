/**
 * @file ui_text.c
 * @brief Text rendering for the VitaRPS5 UI: the five SPEC type faces
 *
 * Eliminates first-frame atlas hitches and baseline jitter by:
 *  1. Pre-warming the vita2d_font glyph atlas for every type face before the
 *     first visible frame.
 *  2. Caching ascent and line-height once per face via a probe string, so all
 *     call sites share identical, metrics-derived baseline offsets.
 */

#include <vita2d.h>
#include <psp2/kernel/clib.h>

#include "ui/ui_text.h"
#include "ui/ui_constants.h"
#include "ui/ui_draw_stats.h"
#include "ui/ui_component.h"
#include "ui/ui_theme.h"

/* ============================================================================
 * Named Constants — no magic numbers below this section
 * ============================================================================ */

/*
 * The five SPEC type faces, indexed by UiFace: point size and which weight draws it.
 * Light faces use s_font_light, falling back to the regular font if Light failed to load.
 */
typedef struct {
  int pt_size;
  int line_height;
  int weight;
} FaceSpec;

static const FaceSpec UI_FACE_TABLE[UI_FACE_COUNT] = {
    [UI_FACE_T14] = {UI_T14_SIZE, UI_T14_LINE, UI_T14_WEIGHT},
    [UI_FACE_T16] = {UI_T16_SIZE, UI_T16_LINE, UI_T16_WEIGHT},
    [UI_FACE_T20] = {UI_T20_SIZE, UI_T20_LINE, UI_T20_WEIGHT},
    [UI_FACE_T28] = {UI_T28_SIZE, UI_T28_LINE, UI_T28_WEIGHT},
    [UI_FACE_T40] = {UI_T40_SIZE, UI_T40_LINE, UI_T40_WEIGHT},
};

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
 * Per-face metric cache
 * ============================================================================ */

typedef struct {
  int ascent;      /* Pixels above baseline for this face. */
  int line_height; /* Total line height (ascent + descent + leading). */
} FaceMetrics;

static FaceMetrics s_metrics[UI_FACE_COUNT];

/* ============================================================================
 * Module State
 * ============================================================================ */

static vita2d_font *s_font_regular = NULL;
static vita2d_font *s_font_light = NULL;
static int s_prewarm_needed = 0; /* armed to 1 only after a successful ui_text_init() */

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

/**
 * compute_metrics_for_face() - Measure ascent and line-height for one face.
 * @f:    Font to probe (the regular font; Regular and Light share UPM/ascender).
 * @face: Face whose point size is measured and whose slot is populated.
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
static void compute_metrics_for_face(vita2d_font *f, UiFace face) {
  int pt_size = UI_FACE_TABLE[face].pt_size;
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
  s_metrics[face].line_height = h;
  s_metrics[face].ascent = (h * UI_FONT_ASCENT_NUMERATOR) / UI_FONT_ASCENT_DENOMINATOR;
#ifndef NDEBUG
  sceClibPrintf("[ui_text] size=%d h=%d ascent=%d line=%d (font=%p)\n", pt_size, h,
                s_metrics[face].ascent, s_metrics[face].line_height, (const void *)f);
#endif
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
 * @light:   Light-weight font for the T20/T28/T40 faces.  If NULL the faces fall
 *           back to the regular font so the UI stays usable.
 *
 * Must be called after fonts are loaded and before ui_text_prewarm().
 * This function does NOT compute metrics — that is intentionally deferred to
 * ui_text_prewarm() because some FreeType/GXM paths require an active render
 * pass, which is guaranteed by the caller wrapping ui_text_prewarm() in
 * vita2d_start_drawing / vita2d_end_drawing.
 *
 * Both pointers are borrowed — ownership remains with the caller.
 */
void ui_text_init(vita2d_font *regular, vita2d_font *light) {
  s_font_regular = regular;
  s_font_light = light;

  if (!light)
    sceClibPrintf("[WARN] ui_text_init: Light font missing — T20/T28/T40 fall back to Regular\n");

  if (!regular) {
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
 * face_font() - Pick the loaded font that draws @face.
 * @face:   Face to resolve.
 * @caller: Short string identifying the calling function, for the warning.
 *
 * Light faces use the Light font and fall back to Regular if it failed to load.
 * Returns NULL (after a warning) for an out-of-range face.
 */
static vita2d_font *face_font(UiFace face, const char *caller) {
  if ((int)face < 0 || face >= UI_FACE_COUNT) {
    sceClibPrintf("[WARN] ui_text: %s received unknown face=%d\n", caller, (int)face);
    return NULL;
  }
  if (UI_FACE_TABLE[face].weight == UI_WEIGHT_LIGHT && s_font_light)
    return s_font_light;
  return s_font_regular;
}

/**
 * prewarm_one_face() - Bake all charset glyphs for one face.
 * @f:       Font that draws the face.
 * @pt_size: Point size of the face.
 *
 * Walks UI_FONT_PREWARM_CHARSET via utf8_extract(), issuing a
 * vita2d_font_draw_text call per glyph at fully transparent, off-screen
 * coordinates.  This forces FreeType rasterization and GXM atlas upload
 * without producing any visible output.
 */
static void prewarm_one_face(vita2d_font *f, int pt_size) {
  char glyph_buf[UI_FONT_UTF8_SEQ_BUFFER_BYTES];
  const char *p = UI_FONT_PREWARM_CHARSET;
  int extracted;

  while ((extracted = utf8_extract(&p, glyph_buf)) != 0) {
    if (extracted < 0)
      continue;

    vita2d_font_draw_text(f, UI_FONT_PREWARM_OFFSCREEN_X, UI_FONT_PREWARM_OFFSCREEN_Y,
                          UI_FONT_PREWARM_COLOR, (unsigned int)pt_size, glyph_buf);
  }
}

/**
 * ui_text_prewarm() - Rasterize every face's glyphs into the atlas.
 *
 * Must be called from within an active vita2d_start_drawing() /
 * vita2d_end_drawing() pair on the render thread.  Draws each character
 * individually at UI_FONT_PREWARM_OFFSCREEN_Y with alpha=0 to trigger
 * FreeType rasterization and GPU atlas upload without visible output.
 *
 * Each face is baked in the font that draws it, so only the sizes the UI
 * actually uses occupy atlas memory.
 *
 * Metrics (ascent, line-height) are derived from s_font_regular only.
 * Roboto Regular and Roboto Light share the same UPM and ascender, so a single
 * canonical measurement per face is sufficient.
 *
 * Each multibyte UTF-8 sequence is drawn as a single call so vita2d's internal
 * UTF-8 decoder sees the full codepoint.
 */
void ui_text_prewarm(void) {
  int face;

  if (!s_font_regular) {
    sceClibPrintf("[WARN] ui_text_prewarm: called before ui_text_init()\n");
    return;
  }

  /*
   * Measure ascent and line-height here rather than in ui_text_init() because
   * some FreeType/GXM code paths rasterize internally and require an active
   * render pass; callers wrap this function in vita2d_start_drawing /
   * vita2d_end_drawing, guaranteeing that context is present.
   */
  for (face = 0; face < UI_FACE_COUNT; face++) {
    compute_metrics_for_face(s_font_regular, (UiFace)face);
    prewarm_one_face(face_font((UiFace)face, "ui_text_prewarm"), UI_FACE_TABLE[face].pt_size);
  }

  s_prewarm_needed = 0;
}

/* ============================================================================
 * SPEC type faces
 * ============================================================================ */

void ui_text_draw_face(UiFace face, int x, int baseline_y, unsigned int color, const char *s) {
  vita2d_font *f = face_font(face, "ui_text_draw_face");
  if (!f || !s)
    return;
  UI_DRAW_STATS_TEXT(vita2d_font_draw_text(f, x, baseline_y, ui_layer_color(color),
                                           (unsigned int)UI_FACE_TABLE[face].pt_size, s));
}

int ui_text_face_width(UiFace face, const char *s) {
  vita2d_font *f = face_font(face, "ui_text_face_width");
  if (!f || !s)
    return 0;
  return (int)vita2d_font_text_width(f, (unsigned int)UI_FACE_TABLE[face].pt_size, s);
}

void ui_text_draw_face_centered_v(UiFace face, int x, int box_y, int box_h, unsigned int color,
                                  const char *s) {
  if (!face_font(face, "ui_text_draw_face_centered_v"))
    return;
  ui_text_draw_face(face, x, box_y + (box_h + s_metrics[face].ascent) / 2, color, s);
}

int ui_text_face_line_height(UiFace face) {
  if ((int)face < 0 || face >= UI_FACE_COUNT)
    return 0;
  return UI_FACE_TABLE[face].line_height;
}
