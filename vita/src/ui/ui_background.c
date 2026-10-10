/**
 * @file ui_background.c
 * @brief C27 Background: the wave behind every screen (SPEC.md C27, FEASIBILITY.md section 3)
 *
 * A port of paintRibbons (docs/design/ui-mocks/xmb-wave.js) at blur level None. The mock draws
 * the ribbons with additive blending; vita2d blends with normal alpha, so the band alpha is
 * multiplied by UI_BG_BLEND_GAIN to land near the same brightness. No GXM state is touched.
 *
 * Draw calls: gradient + glow (1, one triangle list), 5 ribbon fills, 5 highlight lines,
 * dust (1) = 12. The Home vignette is 3 more.
 *
 * vita2d_draw_array hands the vertex pointer straight to the GPU, so it needs GPU-visible
 * memory that outlives the call. Every draw copies its vertices into vita2d's per-frame pool.
 * The pool is reset by vita2d each frame; nothing here allocates on the heap.
 *
 * Blur levels Soft, Strong and Dark (tickets #302, #326): the wave is drawn into a 480x272 target
 * (half the screen) and averaged down by exact 2:1 halvings, 480x272 -> 240x136 (Soft) -> 120x68
 * -> 60x34 (Strong and Dark). The main scene then draws the Soft or Strong/Dark result upscaled
 * with bilinear filtering, under a veil. The halvings matter: a target pass without MSAA takes
 * ONE sample per texel, so drawing the wave straight into 240x136 or 60x34 point-samples it
 * (dotted highlight lines, stair-stepped edges) and the upscale turns that into blocks. Drawing
 * big and averaging is what the mock's canvas does. Five vita2d/GXM facts shape the code:
 *  - A render target needs a scene of its own, so ui_background_prepare() renders them before the
 *    main scene opens. It also does the pool reset, because vita2d_start_drawing() would reset
 *    the pool after the target scenes recorded their vertices, and the main scene would overwrite
 *    them before the GPU read them. vita2d_start_drawing_advanced() does not reset the pool.
 *  - vita2d's colour and texture fragment programs are built for the display's 4x MSAA; a scene
 *    on a non-MSAA target needs programs built for MSAA none. Each pass swaps one in for its
 *    duration.
 *  - vita2d's projection always maps 960x544 to the whole target, so a halving pass draws the
 *    source as a 960x544 quad. Its texture coordinates run 0 to 1 over the whole texture, so with
 *    bilinear filtering every destination texel centre lands on the corner shared by four source
 *    texels: an exact 2x2 average.
 *  - The colour program keeps destination alpha at 1 (alpha blends as "over"), so the upscaled
 *    picture is fully opaque and the clear colour never shows through the ribbons.
 *  - vita2d creates the target surface with a stride of w pixels but the texture reads rows
 *    padded to a multiple of 8, which breaks widths such as 60; the surface is re-initialised
 *    with the padded stride.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <vita2d.h>
#include <psp2/gxm.h>
#include <psp2/kernel/processmgr.h>
#include <shared.h>

#include "context.h"
#include "ui/ui_background.h"
#include "ui/ui_constants.h"
#include "ui/ui_theme.h"

/* ============================================================================
 * Geometry sizes
 * ============================================================================ */

#define VERTS_PER_RECT 6
#define VERTS_PER_FAN_TRIANGLE 3
#define VERTS_PER_STRIP_STOP 2
#define PCT_DIVISOR 100.0f

#define BACKDROP_BAND_COUNT 2
#define BACKDROP_GLOW_VERTS (UI_BG_HORIZON_SEGMENTS * VERTS_PER_FAN_TRIANGLE)
#define BACKDROP_VERTS (BACKDROP_BAND_COUNT * VERTS_PER_RECT + BACKDROP_GLOW_VERTS)

#define RIBBON_STRIP_VERTS (UI_BG_COLS * VERTS_PER_STRIP_STOP)
#define DUST_VERTS (UI_BG_DUST_COUNT * VERTS_PER_RECT)

#define VIG_RIGHT_STOPS 3
#define VIG_LEFT_STOPS 3
#define VIG_VERTICAL_STOPS 4

#define COLOUR_BYTE_MAX 255.0f
#define COLOUR_ROUND 0.5f
#define ALPHA_SHIFT 24
#define TWO_PI 6.2831853f
#define HALF 0.5f

/* ============================================================================
 * State: one static ribbon state, plus the prebuilt geometry
 * ============================================================================ */

typedef struct {
  float x;
  float y;
  float radius;
  float phase;
} DustPoint;

typedef struct {
  float pos;
  uint32_t colour;
} GradientStop;

static vita2d_color_vertex s_backdrop[BACKDROP_VERTS];
static vita2d_color_vertex s_fill[UI_BG_RIBBON_COUNT][RIBBON_STRIP_VERTS];
static vita2d_color_vertex s_line[UI_BG_RIBBON_COUNT][RIBBON_STRIP_VERTS];
static vita2d_color_vertex s_dust_verts[DUST_VERTS];
static DustPoint s_dust[UI_BG_DUST_COUNT];

static vita2d_color_vertex s_vig_right[VIG_RIGHT_STOPS * VERTS_PER_STRIP_STOP];
static vita2d_color_vertex s_vig_left[VIG_LEFT_STOPS * VERTS_PER_STRIP_STOP];
static vita2d_color_vertex s_vig_vertical[VIG_VERTICAL_STOPS * VERTS_PER_STRIP_STOP];

/* The theme the backdrop, glow and vignette vertices were built with (NULL before the first build).
 * Compared by pointer: ui_theme_current() returns the same pointer until the theme changes. */
static const UiTheme *s_built_theme = NULL;

/** The fill colour of ribbon @p i in the current theme; colours cycle through the three tokens. */
static uint32_t ribbon_colour(int i) {
  const uint32_t colours[UI_BG_RIBBON_COLOURS] = {UI_BG_RIBBON_1, UI_BG_RIBBON_2, UI_BG_RIBBON_3};
  return colours[i % UI_BG_RIBBON_COLOURS];
}

/* The blur chain, each level half the size of the one before: the wave is rendered into BASE, and
 * every later level is a 2:1 average of the previous one. Soft shows SOFT, Strong and Dark SMALL.
 */
typedef enum { TARGET_BASE = 0, TARGET_SOFT, TARGET_MID, TARGET_SMALL, TARGET_COUNT } BlurTargetId;

typedef struct {
  vita2d_texture *tex;
  unsigned int width;
  unsigned int height;
  bool valid; /* tex holds the wave for the current vertices */
} BlurTarget;

static BlurTarget s_targets[TARGET_COUNT] = {
    {NULL, UI_BG_BLUR_BASE_W, UI_BG_BLUR_BASE_H, false},
    {NULL, UI_BG_BLUR_SOFT_W, UI_BG_BLUR_SOFT_H, false},
    {NULL, UI_BG_BLUR_MID_W, UI_BG_BLUR_MID_H, false},
    {NULL, UI_BG_BLUR_STRONG_W, UI_BG_BLUR_STRONG_H, false},
};
static SceGxmFragmentProgram *s_target_colour_program = NULL;
static SceGxmFragmentProgram *s_target_texture_program = NULL;
static SceGxmShaderPatcherId s_colour_program_id = NULL;
static SceGxmShaderPatcherId s_texture_program_id = NULL;
static bool s_targets_tried = false;
static bool s_targets_ok = false;

/* The colour shaders are linked into libvita2d; vita2d does not expose their ids. */
extern const SceGxmProgram color_v_gxp_start;
extern const SceGxmProgram color_f_gxp_start;
extern const SceGxmProgram texture_v_gxp_start;
extern const SceGxmProgram texture_f_gxp_start;

static bool s_ready = false;
static bool s_geometry_valid = false;
static bool s_pool_warned = false;
static uint64_t s_last_update_us = 0;

/* ============================================================================
 * Helpers
 * ============================================================================ */

/** Returns @p colour with its alpha byte replaced by @p alpha (0..1, clamped). */
static uint32_t with_alpha(uint32_t colour, float alpha) {
  if (alpha < 0.0f)
    alpha = 0.0f;
  if (alpha > 1.0f)
    alpha = 1.0f;
  uint32_t a = (uint32_t)(alpha * COLOUR_BYTE_MAX + COLOUR_ROUND);
  return UI_COLOR_CLEAR(colour) | (a << ALPHA_SHIFT);
}

static vita2d_color_vertex vertex(float x, float y, uint32_t colour) {
  vita2d_color_vertex v = {x, y, UI_BG_Z, colour};
  return v;
}

/**
 * Copies @p count vertices into the per-frame pool and draws them. Skips the draw, and says so
 * once, when the pool is out of space.
 */
static void draw_pooled(SceGxmPrimitiveType mode, const vita2d_color_vertex *src,
                        unsigned int count) {
  size_t bytes = count * sizeof(vita2d_color_vertex);
  vita2d_color_vertex *dst = vita2d_pool_memalign(bytes, sizeof(vita2d_color_vertex));
  if (!dst) {
    if (!s_pool_warned) {
      LOGE("UI/BACKGROUND vita2d pool exhausted, skipping draw (%zu bytes)", bytes);
      s_pool_warned = true;
    }
    return;
  }
  memcpy(dst, src, bytes);
  vita2d_draw_array(mode, dst, count);
}

/** Writes two triangles covering the full width between y0 and y1, shaded from c0 to c1. */
static void put_band(vita2d_color_vertex *out, float y0, uint32_t c0, float y1, uint32_t c1) {
  const float w = (float)VITA_WIDTH;
  out[0] = vertex(0.0f, y0, c0);
  out[1] = vertex(w, y0, c0);
  out[2] = vertex(0.0f, y1, c1);
  out[3] = vertex(w, y0, c0);
  out[4] = vertex(w, y1, c1);
  out[5] = vertex(0.0f, y1, c1);
}

/**
 * Writes a triangle strip that fades across the screen through @p stops.
 * @horizontal: true when the stops are x positions (a vertical edge each), false for y positions.
 */
static void put_strip(vita2d_color_vertex *out, const GradientStop *stops, int count,
                      bool horizontal) {
  for (int i = 0; i < count; i++) {
    if (horizontal) {
      out[i * 2] = vertex(stops[i].pos, 0.0f, stops[i].colour);
      out[i * 2 + 1] = vertex(stops[i].pos, (float)VITA_HEIGHT, stops[i].colour);
    } else {
      out[i * 2] = vertex(0.0f, stops[i].pos, stops[i].colour);
      out[i * 2 + 1] = vertex((float)VITA_WIDTH, stops[i].pos, stops[i].colour);
    }
  }
}

/** Converts a percentage of the screen width to pixels. */
static float pct_of_width(int pct) {
  return (float)VITA_WIDTH * (float)pct / PCT_DIVISOR;
}

/** Converts a percentage of the screen height to pixels. */
static float pct_of_height(int pct) {
  return (float)VITA_HEIGHT * (float)pct / PCT_DIVISOR;
}

/* ============================================================================
 * Static geometry (built once)
 * ============================================================================ */

/** Gradient bands plus the horizon glow, as one triangle list (the glow blends over the bands). */
static void build_backdrop(void) {
  const float mid_y = pct_of_height(UI_BG_MID_STOP_PCT);
  put_band(&s_backdrop[0], 0.0f, UI_BG_TOP, mid_y, UI_BG_MID);
  put_band(&s_backdrop[VERTS_PER_RECT], mid_y, UI_BG_MID, (float)VITA_HEIGHT, UI_BG_BOTTOM);

  vita2d_color_vertex *fan = &s_backdrop[BACKDROP_BAND_COUNT * VERTS_PER_RECT];
  const uint32_t edge = UI_COLOR_CLEAR(UI_BG_HORIZON);
  for (int i = 0; i < UI_BG_HORIZON_SEGMENTS; i++) {
    float a0 = TWO_PI * (float)i / (float)UI_BG_HORIZON_SEGMENTS;
    float a1 = TWO_PI * (float)(i + 1) / (float)UI_BG_HORIZON_SEGMENTS;
    fan[i * 3] = vertex(UI_BG_HORIZON_X, UI_BG_HORIZON_Y, UI_BG_HORIZON);
    fan[i * 3 + 1] = vertex(UI_BG_HORIZON_X + cosf(a0) * UI_BG_HORIZON_R,
                            UI_BG_HORIZON_Y + sinf(a0) * UI_BG_HORIZON_R, edge);
    fan[i * 3 + 2] = vertex(UI_BG_HORIZON_X + cosf(a1) * UI_BG_HORIZON_R,
                            UI_BG_HORIZON_Y + sinf(a1) * UI_BG_HORIZON_R, edge);
  }
}

/** Places the dust points once, with the mock's linear generator. */
static void build_dust(void) {
  for (int i = 0; i < UI_BG_DUST_COUNT; i++) {
    float rx =
        (float)((i * UI_BG_DUST_X_MUL + UI_BG_DUST_X_ADD) % UI_BG_DUST_MOD) / (float)UI_BG_DUST_MOD;
    float ry =
        (float)((i * UI_BG_DUST_Y_MUL + UI_BG_DUST_Y_ADD) % UI_BG_DUST_MOD) / (float)UI_BG_DUST_MOD;
    s_dust[i].x = rx * (float)VITA_WIDTH;
    s_dust[i].y = ry * (float)VITA_HEIGHT;
    s_dust[i].radius =
        UI_BG_DUST_SIZE_BASE +
        (float)((i * UI_BG_DUST_SIZE_MUL) % UI_BG_DUST_SIZE_STEPS) / UI_BG_DUST_SIZE_DIV;
    s_dust[i].phase = (float)i * UI_BG_DUST_PHASE_STEP;
  }
}

/** The three Home vignette layers, nearest layer last (CSS lists the top layer first). */
static void build_vignette(void) {
  const GradientStop vertical[VIG_VERTICAL_STOPS] = {
      {0.0f, UI_VIG_TOP},
      {pct_of_height(UI_VIG_TOP_END_PCT), UI_COLOR_CLEAR(UI_VIG_TOP)},
      {pct_of_height(UI_VIG_BOTTOM_START_PCT), UI_COLOR_CLEAR(UI_VIG_BOTTOM)},
      {(float)VITA_HEIGHT, UI_VIG_BOTTOM},
  };
  put_strip(s_vig_vertical, vertical, VIG_VERTICAL_STOPS, false);

  const GradientStop left[VIG_LEFT_STOPS] = {
      {0.0f, UI_VIG_STRONG},
      {pct_of_width(UI_VIG_LEFT_MID_PCT), UI_VIG_3},
      {pct_of_width(UI_VIG_LEFT_END_PCT), UI_COLOR_CLEAR(UI_VIG_3)},
  };
  put_strip(s_vig_left, left, VIG_LEFT_STOPS, true);

  const GradientStop right[VIG_RIGHT_STOPS] = {
      {pct_of_width(100 - UI_VIG_RIGHT_END_PCT), UI_COLOR_CLEAR(UI_VIG_2)},
      {pct_of_width(100 - UI_VIG_RIGHT_MID_PCT), UI_VIG_2},
      {(float)VITA_WIDTH, UI_VIG_1},
  };
  put_strip(s_vig_right, right, VIG_RIGHT_STOPS, true);
}

/* ============================================================================
 * Per-update geometry (30 Hz)
 * ============================================================================ */

/** Horizontal fade of a ribbon fill at screen x: 0 at the edges, 1 between the two stops. */
static float fade_at(float x) {
  float u = x / (float)VITA_WIDTH;
  const float in = (float)UI_BG_FADE_IN_PCT / PCT_DIVISOR;
  const float out = (float)UI_BG_FADE_OUT_PCT / PCT_DIVISOR;
  if (u <= 0.0f || u >= 1.0f)
    return 0.0f;
  if (u < in)
    return u / in;
  if (u < out)
    return 1.0f - (u - in) / (out - in) * (1.0f - UI_BG_FADE_OUT_GAIN);
  return UI_BG_FADE_OUT_GAIN * (1.0f - u) / (1.0f - out);
}

/** Recomputes one ribbon's fill strip and highlight strip for time @p t (ms). */
static void update_ribbon(int i, float t) {
  const float k = (float)i / (float)(UI_BG_RIBBON_COUNT - 1);
  const float amp = UI_BG_AMP_BASE + k * UI_BG_AMP_PER_K;
  const float speed = UI_BG_SPEED_BASE + k * UI_BG_SPEED_PER_K;
  const float base = UI_BG_Y_BASE + (float)i * UI_BG_Y_PER_RIBBON + k * UI_BG_Y_PER_K;
  const float alpha = UI_BG_ALPHA_BASE + k * UI_BG_ALPHA_PER_K;
  const float line_w = UI_BG_LINE_W_BASE + k * UI_BG_LINE_W_PER_K;
  const uint32_t line_colour =
      with_alpha(UI_TEXT, UI_BG_LINE_ALPHA_BASE + k * UI_BG_LINE_ALPHA_PER_K);
  const uint32_t colour = ribbon_colour(i);
  const float fi = (float)i;

  for (int c = 0; c < UI_BG_COLS; c++) {
    float x = (float)(UI_BG_COL_X0 + c * UI_BG_COL_STEP);
    float yc =
        base +
        sinf(x * UI_BG_WAVE1_FREQ + t * speed * UI_BG_WAVE1_TIME_MUL + fi * UI_BG_WAVE1_PHASE) *
            amp +
        sinf(x * UI_BG_WAVE2_FREQ - t * speed + fi) * amp * UI_BG_WAVE2_AMP;
    float th = UI_BG_THICK_BASE + k * UI_BG_THICK_PER_K +
               sinf(x * UI_BG_THICK_FREQ + fi + t * UI_BG_THICK_TIME) *
                   (UI_BG_THICK_MOD_BASE + k * UI_BG_THICK_MOD_PER_K);
    float mid = yc + sinf(x * UI_BG_LINE_FREQ + t * UI_BG_LINE_TIME + fi) * th * UI_BG_LINE_WOBBLE;
    uint32_t edge = with_alpha(colour, alpha * fade_at(x) * UI_BG_BLEND_GAIN);

    s_fill[i][c * 2] = vertex(x, yc - th * HALF, edge);
    s_fill[i][c * 2 + 1] = vertex(x, yc + th * HALF, edge);
    s_line[i][c * 2] = vertex(x, mid - line_w * HALF, line_colour);
    s_line[i][c * 2 + 1] = vertex(x, mid + line_w * HALF, line_colour);
  }
}

/** Recomputes the twinkling, drifting dust quads for time @p t (ms). */
static void update_dust(float t) {
  for (int i = 0; i < UI_BG_DUST_COUNT; i++) {
    const DustPoint *d = &s_dust[i];
    float twinkle = UI_BG_DUST_TWINKLE_BASE +
                    UI_BG_DUST_TWINKLE_AMP * sinf(t * UI_BG_DUST_TWINKLE_TIME + d->phase);
    uint32_t colour = with_alpha(UI_TEXT, twinkle * UI_BG_DUST_ALPHA);
    float x = fmodf(d->x + t * UI_BG_DUST_DRIFT * d->radius, (float)VITA_WIDTH);
    float y = d->y + sinf(t * UI_BG_DUST_BOB_TIME + d->phase) * UI_BG_DUST_BOB_AMP;
    float r = d->radius;
    vita2d_color_vertex *q = &s_dust_verts[i * VERTS_PER_RECT];
    q[0] = vertex(x - r, y - r, colour);
    q[1] = vertex(x + r, y - r, colour);
    q[2] = vertex(x - r, y + r, colour);
    q[3] = vertex(x + r, y - r, colour);
    q[4] = vertex(x + r, y + r, colour);
    q[5] = vertex(x - r, y + r, colour);
  }
}

/** Recomputes every moving vertex for time @p t (ms). */
static void update_geometry(float t) {
  for (int i = 0; i < UI_BG_RIBBON_COUNT; i++)
    update_ribbon(i, t);
  update_dust(t);
  s_geometry_valid = true;
}

/**
 * Recomputes the vertices when the update interval has passed. Returns true when it did, which
 * also makes every blur target stale.
 */
static bool refresh_geometry(bool slow) {
  uint64_t now_us = sceKernelGetProcessTimeWide();
  uint64_t interval_us = (slow ? UI_BG_UPDATE_SLOW_US : UI_BG_UPDATE_US) - UI_BG_UPDATE_SLACK_US;
  if (s_geometry_valid && now_us - s_last_update_us < interval_us)
    return false;

  update_geometry((float)now_us * UI_BG_MS_PER_US);
  s_last_update_us = now_us;
  for (int i = 0; i < TARGET_COUNT; i++)
    s_targets[i].valid = false;
  return true;
}

/** Issues the wave's draws: backdrop, ribbons, dust. Used for the screen and for the base target.
 */
static void draw_wave(void) {
  draw_pooled(SCE_GXM_PRIMITIVE_TRIANGLES, s_backdrop, BACKDROP_VERTS);
  for (int i = 0; i < UI_BG_RIBBON_COUNT; i++) {
    draw_pooled(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, s_fill[i], RIBBON_STRIP_VERTS);
    draw_pooled(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, s_line[i], RIBBON_STRIP_VERTS);
  }
  draw_pooled(SCE_GXM_PRIMITIVE_TRIANGLES, s_dust_verts, DUST_VERTS);
}

/* ============================================================================
 * Blur targets
 * ============================================================================ */

/**
 * Creates one target: a render-target texture with bilinear filtering and clamped edges.
 * Returns NULL (after freeing) on failure.
 */
static vita2d_texture *create_target(unsigned int w, unsigned int h) {
  vita2d_texture *tex =
      vita2d_create_empty_texture_rendertarget(w, h, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8);
  if (!tex) {
    LOGE("UI/BACKGROUND blur target %ux%u: allocation failed", w, h);
    return NULL;
  }

  /* vita2d inits the surface with stride w, the texture reads rows padded to a multiple of 8 */
  unsigned int stride =
      (w + UI_BG_BLUR_STRIDE_ALIGN - 1) & ~(unsigned int)(UI_BG_BLUR_STRIDE_ALIGN - 1);
  int err = sceGxmColorSurfaceInit(&tex->gxm_sfc, SCE_GXM_COLOR_FORMAT_A8B8G8R8,
                                   SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
                                   SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, w, h, stride,
                                   vita2d_texture_get_datap(tex));
  if (err < 0) {
    LOGE("UI/BACKGROUND blur target %ux%u: surface init failed (0x%08x)", w, h, (unsigned)err);
    vita2d_free_texture(tex);
    return NULL;
  }

  vita2d_texture_set_filters(tex, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
  sceGxmTextureSetUAddrMode(&tex->gxm_tex, SCE_GXM_TEXTURE_ADDR_CLAMP);
  sceGxmTextureSetVAddrMode(&tex->gxm_tex, SCE_GXM_TEXTURE_ADDR_CLAMP);
  return tex;
}

/**
 * Registers @p fragment and builds an MSAA-none fragment program from it with @p vertex and
 * @p blend (NULL replaces the destination). Returns false on failure, leaving nothing registered.
 */
static bool create_program(const SceGxmProgram *fragment, const SceGxmProgram *vertex,
                           const SceGxmBlendInfo *blend, SceGxmShaderPatcherId *id,
                           SceGxmFragmentProgram **program) {
  SceGxmShaderPatcher *patcher = vita2d_get_shader_patcher();
  int err = sceGxmShaderPatcherRegisterProgram(patcher, fragment, id);
  if (err < 0) {
    LOGE("UI/BACKGROUND blur shader register failed (0x%08x)", (unsigned)err);
    *id = NULL;
    return false;
  }
  err =
      sceGxmShaderPatcherCreateFragmentProgram(patcher, *id, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
                                               SCE_GXM_MULTISAMPLE_NONE, blend, vertex, program);
  if (err < 0) {
    LOGE("UI/BACKGROUND blur shader create failed (0x%08x)", (unsigned)err);
    sceGxmShaderPatcherUnregisterProgram(patcher, *id);
    *id = NULL;
    *program = NULL;
    return false;
  }
  return true;
}

/**
 * Builds the two fragment programs the target passes use, both for MSAA none (what the targets
 * are). The colour one is vita2d's shader with colour blended as normal alpha and destination
 * alpha blended as "over" (source alpha ONE, destination ONE_MINUS_SRC_ALPHA): over an opaque
 * destination that stays exactly 1, where vita2d's SRC_ALPHA factors would drop it to a*a+(1-a)
 * and let the clear colour show through the upscaled ribbons. The texture one copies the source
 * with no blending, so a halving replaces whatever the target held. Returns false on failure.
 */
static bool create_target_programs(void) {
  static const SceGxmBlendInfo colour_blend = {
      .colorFunc = SCE_GXM_BLEND_FUNC_ADD,
      .alphaFunc = SCE_GXM_BLEND_FUNC_ADD,
      .colorSrc = SCE_GXM_BLEND_FACTOR_SRC_ALPHA,
      .colorDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
      .alphaSrc = SCE_GXM_BLEND_FACTOR_ONE,
      .alphaDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
      .colorMask = SCE_GXM_COLOR_MASK_ALL,
  };
  return create_program(&color_f_gxp_start, &color_v_gxp_start, &colour_blend, &s_colour_program_id,
                        &s_target_colour_program) &&
         create_program(&texture_f_gxp_start, &texture_v_gxp_start, NULL, &s_texture_program_id,
                        &s_target_texture_program);
}

/** Releases whichever target programs exist. */
static void free_target_programs(void) {
  SceGxmShaderPatcher *patcher = vita2d_get_shader_patcher();
  if (s_target_colour_program)
    sceGxmShaderPatcherReleaseFragmentProgram(patcher, s_target_colour_program);
  if (s_target_texture_program)
    sceGxmShaderPatcherReleaseFragmentProgram(patcher, s_target_texture_program);
  if (s_colour_program_id)
    sceGxmShaderPatcherUnregisterProgram(patcher, s_colour_program_id);
  if (s_texture_program_id)
    sceGxmShaderPatcherUnregisterProgram(patcher, s_texture_program_id);
  s_target_colour_program = NULL;
  s_target_texture_program = NULL;
  s_colour_program_id = NULL;
  s_texture_program_id = NULL;
}

/**
 * Creates every target and both programs on first use, so None costs nothing. A failure is logged
 * once, frees everything, and leaves the background at level None for the rest of the run.
 */
static bool blur_targets_ready(void) {
  if (s_targets_tried)
    return s_targets_ok;
  s_targets_tried = true;

  bool ok = create_target_programs();
  for (int i = 0; ok && i < TARGET_COUNT; i++) {
    s_targets[i].tex = create_target(s_targets[i].width, s_targets[i].height);
    ok = s_targets[i].tex != NULL;
  }
  if (!ok) {
    LOGE("UI/BACKGROUND blur unavailable (targets %ux%u, %ux%u, %ux%u, %ux%u), using blur None",
         UI_BG_BLUR_BASE_W, UI_BG_BLUR_BASE_H, UI_BG_BLUR_SOFT_W, UI_BG_BLUR_SOFT_H,
         UI_BG_BLUR_MID_W, UI_BG_BLUR_MID_H, UI_BG_BLUR_STRONG_W, UI_BG_BLUR_STRONG_H);
    for (int i = 0; i < TARGET_COUNT; i++) {
      vita2d_free_texture(s_targets[i].tex);
      s_targets[i].tex = NULL;
    }
    free_target_programs();
  }
  s_targets_ok = ok;
  return ok;
}

/** The blur level to draw this frame: the setting, or None when it is out of range or unusable. */
static VitaChiakiBackgroundBlur active_blur(void) {
  VitaChiakiBackgroundBlur blur = config_background_blur(&context.config);
  if (blur <= VITA_BACKGROUND_BLUR_NONE || blur >= VITA_BACKGROUND_BLUR_COUNT)
    return VITA_BACKGROUND_BLUR_NONE;
  return blur_targets_ready() ? blur : VITA_BACKGROUND_BLUR_NONE;
}

/** The level the main scene draws: Soft shows the 240x136 one; Strong and Dark the 60x34 one. */
static BlurTarget *target_for(VitaChiakiBackgroundBlur blur) {
  return &s_targets[blur == VITA_BACKGROUND_BLUR_SOFT ? TARGET_SOFT : TARGET_SMALL];
}

/** Marks every level after @p id stale: they were averaged from a picture that just changed. */
static void invalidate_after(int id) {
  for (int i = id + 1; i < TARGET_COUNT; i++)
    s_targets[i].valid = false;
}

/**
 * Renders the wave into the base target in a scene of its own. The scene scales the 960x544
 * geometry to the target size (the default viewport is the whole target), and the swapped-in
 * MSAA-none colour program makes the draws valid for it. Needs no clear: the opaque gradient
 * covers every pixel.
 */
static void render_base(void) {
  BlurTarget *base = &s_targets[TARGET_BASE];
  SceGxmFragmentProgram *saved_program = _vita2d_colorFragmentProgram;
  vita2d_start_drawing_advanced(base->tex, 0);
  _vita2d_colorFragmentProgram = s_target_colour_program;
  draw_wave();
  _vita2d_colorFragmentProgram = saved_program;
  vita2d_end_drawing();
  base->valid = true;
  invalidate_after(TARGET_BASE);
}

/**
 * Averages level @p id - 1 down 2:1 into level @p id, in a scene of its own. The source is drawn
 * as one quad covering the whole 960x544 space (vita2d's projection maps that to the whole
 * target) with texture coordinates 0 to 1, so each destination texel centre sits on the corner
 * shared by four source texels and bilinear filtering returns their exact mean. The no-blend
 * texture program replaces the destination.
 */
static void render_halving(int id) {
  BlurTarget *src = &s_targets[id - 1];
  BlurTarget *dst = &s_targets[id];
  SceGxmFragmentProgram *saved_program = _vita2d_textureFragmentProgram;
  vita2d_start_drawing_advanced(dst->tex, 0);
  _vita2d_textureFragmentProgram = s_target_texture_program;
  vita2d_draw_texture_scale(src->tex, 0.0f, 0.0f, (float)VITA_WIDTH / (float)src->width,
                            (float)VITA_HEIGHT / (float)src->height);
  _vita2d_textureFragmentProgram = saved_program;
  vita2d_end_drawing();
  dst->valid = true;
  invalidate_after(id);
}

/**
 * Brings the chain up to the level @p last (TARGET_SOFT or TARGET_SMALL) up to date, rendering
 * only the levels whose picture is stale. Levels after @p last are left alone (they stay stale
 * until a setting that needs them), so a live switch never shows a picture built from old
 * geometry.
 */
static void render_chain(int last) {
  if (!s_targets[TARGET_BASE].valid)
    render_base();
  for (int i = TARGET_BASE + 1; i <= last; i++) {
    if (!s_targets[i].valid)
      render_halving(i);
  }
}

/** Fills the whole screen with @p colour. */
static void draw_veil(uint32_t colour) {
  vita2d_draw_rectangle(0.0f, 0.0f, (float)VITA_WIDTH, (float)VITA_HEIGHT, colour);
}

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * Rebuilds the vertices that carry theme colours when the theme changed since they were built:
 * the backdrop gradient, the horizon glow and the vignette (same build functions, same arrays).
 * The ribbons take their colour at every geometry update, so the next frame refreshes them, and
 * every blur target is stale because it holds the old colours. A compare when nothing changed.
 */
static void sync_theme(void) {
  const UiTheme *theme = ui_theme_current();
  if (theme == s_built_theme)
    return;
  build_backdrop();
  build_vignette();
  s_built_theme = theme;
  s_geometry_valid = false;
  for (int i = 0; i < TARGET_COUNT; i++)
    s_targets[i].valid = false;
}

void ui_background_init(void) {
  if (s_ready)
    return;
  build_dust();
  sync_theme();
  s_ready = true;
}

void ui_background_prepare(bool slow) {
  vita2d_pool_reset();
  if (!s_ready)
    return;
  sync_theme();

  VitaChiakiBackgroundBlur blur = active_blur();
  if (blur == VITA_BACKGROUND_BLUR_NONE)
    return;

  refresh_geometry(slow);
  render_chain(blur == VITA_BACKGROUND_BLUR_SOFT ? TARGET_SOFT : TARGET_SMALL);
}

void ui_background_draw(bool slow) {
  if (!s_ready)
    return;
  sync_theme();

  VitaChiakiBackgroundBlur blur = active_blur();
  BlurTarget *target = blur == VITA_BACKGROUND_BLUR_NONE ? NULL : target_for(blur);
  if (!target || !target->tex) {
    refresh_geometry(slow);
    draw_wave();
    return;
  }

  vita2d_draw_texture_scale(target->tex, 0.0f, 0.0f, (float)VITA_WIDTH / (float)target->width,
                            (float)VITA_HEIGHT / (float)target->height);
  if (blur == VITA_BACKGROUND_BLUR_DARK) {
    draw_veil(UI_GLASS_VEIL_DARK);
    return;
  }
  if (blur == VITA_BACKGROUND_BLUR_STRONG)
    draw_veil(UI_GLASS_FROST);
  draw_veil(UI_GLASS_VEIL);
}

void ui_background_draw_home_vignette(void) {
  if (!s_ready)
    return;
  sync_theme();
  draw_pooled(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, s_vig_vertical,
              VIG_VERTICAL_STOPS * VERTS_PER_STRIP_STOP);
  draw_pooled(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, s_vig_left, VIG_LEFT_STOPS * VERTS_PER_STRIP_STOP);
  draw_pooled(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, s_vig_right,
              VIG_RIGHT_STOPS * VERTS_PER_STRIP_STOP);
}
