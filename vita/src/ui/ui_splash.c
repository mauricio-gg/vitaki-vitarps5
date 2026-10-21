/**
 * @file ui_splash.c
 * @brief Splash screen (see ui_splash.h)
 *
 * Timeline (SPEC.md 3.9, #398): the logo fades in over the app's own background, holds until
 * loading is done (and at least a minimum time), then dissolves into particles. The dissolve is
 * the #366 particle motion played backwards: the pure maths lives in ui_splash_particles.c (the
 * particles and the logo's opacity, fed a reversed clock) and ui_splash_timeline.c (when things
 * happen). Everything is driven by real time, never by frame counts. Particles are one triangle
 * list (two triangles per visible particle, vertex colour carrying the alpha), written into
 * vita2d's pool, which vita2d resets each frame: at most UI_SPLASH_MAX_PARTICLES * 6 *
 * sizeof(vita2d_color_vertex) = 144000 bytes of the 1 MiB default pool. The particle array is
 * allocated once in ui_splash_start() and freed when the splash ends.
 */

#include "ui/ui_splash.h"

#include <stdbool.h>
#include <stdlib.h>

#include <psp2/ctrl.h>
#include <psp2/gxm.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/touch.h>
#include <vita2d.h>

#include "context.h"
#include "ui/ui_constants.h"
#include "ui/ui_splash_particles.h"
#include "ui/ui_splash_timeline.h"
#include "ui/ui_theme.h"

#define VERTS_PER_QUAD 6
#define US_PER_MS 1000.0f
#define ALPHA_MAX 255.0f
#define ALPHA_ROUND 0.5f
#define COLOR_FULL 255

typedef struct {
  vita2d_texture *logo;
  UiSplashParticle *particles;
  int count;
  bool active;
  bool loading_done;
  bool pool_warned;
  uint64_t start_us;    /**< Real time of ui_splash_start(). */
  uint64_t loaded_us;   /**< Real time of ui_splash_loading_done() (L), 0 before. */
  uint64_t dissolve_us; /**< Real time D at which the dissolve starts, 0 until loading is done. */
  uint64_t ui_full_us;  /**< First frame at which Home took input (D + 0.70 s reached), 0 before. */
  uint64_t end_us;
  uint32_t frames;
  unsigned int prev_buttons;
  bool prev_touching;
} Splash;

static Splash s_splash;

static const UiSplashParams s_params = {
    .screen_w = VITA_WIDTH,
    .screen_h = VITA_HEIGHT,
    .logo_x = UI_SPLASH_LOGO_X,
    .logo_y = UI_SPLASH_LOGO_Y,
    .logo_w = UI_SPLASH_LOGO_W,
    .logo_h = UI_SPLASH_LOGO_H,
    .cell = UI_SPLASH_CELL,
    .cell_fill = UI_SPLASH_CELL_FILL,
    .alpha_min = UI_SPLASH_ALPHA_MIN,
    .red_min = UI_SPLASH_RED_MIN,
    .seed = UI_SPLASH_SEED,
    .lcg_mul = UI_SPLASH_LCG_MUL,
    .lcg_add = UI_SPLASH_LCG_ADD,
    .lcg_range = UI_SPLASH_LCG_RANGE,
    .margin_min = UI_SPLASH_MARGIN_MIN,
    .margin_range = UI_SPLASH_MARGIN_RANGE,
    .size_small = UI_SPLASH_SIZE_SMALL,
    .size_large = UI_SPLASH_SIZE_LARGE,
    .small_chance = UI_SPLASH_SMALL_CHANCE,
    .bow_px = UI_SPLASH_BOW_PX,
    .stagger_ms = UI_SPLASH_STAGGER_MS,
    .flight_ms = UI_SPLASH_FLIGHT_MS,
    .absorb_ms = UI_SPLASH_ABSORB_MS,
    .alpha_start = UI_SPLASH_ALPHA_START,
    .logo_gamma = UI_SPLASH_LOGO_GAMMA_OUT,
    .assembly_ms = UI_SPLASH_ASSEMBLY_MS,
};

static const UiSplashTiming s_timing = {
    .logo_in_ms = UI_SPLASH_LOGO_IN_MS,
    .logo_scale0 = UI_SPLASH_LOGO_SCALE_START,
    .min_hold_ms = UI_SPLASH_MIN_HOLD_MS,
    .dissolve_ms = UI_SPLASH_DISSOLVE_MS,
    .ui_start_ms = UI_SPLASH_UI_START_MS,
    .ui_fade_ms = UI_SPLASH_UI_FADE_MS,
};

/** Milliseconds from @from_us to @to_us (0 if @to_us is earlier). */
static float ms_between(uint64_t from_us, uint64_t to_us) {
  return to_us > from_us ? (float)(to_us - from_us) / US_PER_MS : 0.0f;
}

/** True once D is known and @now_us has reached it. */
static bool dissolve_started(uint64_t now_us) {
  return s_splash.dissolve_us != 0 && now_us >= s_splash.dissolve_us;
}

/** @alpha (0..1, clamped) as an alpha byte. */
static unsigned int alpha_byte(float alpha) {
  if (alpha < 0.0f)
    alpha = 0.0f;
  if (alpha > 1.0f)
    alpha = 1.0f;
  return (unsigned int)(alpha * ALPHA_MAX + ALPHA_ROUND);
}

/** White with @alpha (0..1) as its alpha. */
static uint32_t rgba_white(float alpha) {
  return RGBA8(COLOR_FULL, COLOR_FULL, COLOR_FULL, alpha_byte(alpha));
}

/** How long the last sampling took, for the PIPE/SPLASH_INIT line. */
static uint64_t s_sample_us;

/** Pick the particles from the logo's decoded pixels into a freshly allocated block. */
static void sample_logo(const vita2d_texture *logo, const UiAssetPixels *pixels) {
  if (!logo || !pixels || !pixels->rgba) {
    LOGE("UI/SPLASH no logo texture or pixels, splash draws the background only");
    return;
  }
  s_splash.particles = malloc(UI_SPLASH_MAX_PARTICLES * sizeof(UiSplashParticle));
  if (!s_splash.particles) {
    LOGE("UI/SPLASH particle allocation failed (%u bytes), splash draws the background only",
         (unsigned int)(UI_SPLASH_MAX_PARTICLES * sizeof(UiSplashParticle)));
    return;
  }
  /* The stride is in bytes; the pixels are 4 bytes each. */
  s_splash.count = ui_splash_sample(pixels->rgba, pixels->width, pixels->height,
                                    (int)(pixels->stride_bytes / sizeof(uint32_t)), &s_params,
                                    s_splash.particles, UI_SPLASH_MAX_PARTICLES);
  LOGD("UI/SPLASH %d particles sampled", s_splash.count);
}

void ui_splash_start(vita2d_texture *logo, const UiAssetPixels *pixels) {
  const uint64_t now = sceKernelGetProcessTimeWide();
  free(s_splash.particles);
  s_splash = (Splash){0};
  s_splash.logo = logo;
  s_splash.active = true;
  s_splash.start_us = now;

  /* Buttons or fingers already down at launch do not count as a skip. */
  SceCtrlData ctrl;
  if (sceCtrlPeekBufferPositive(0, &ctrl, 1) > 0)
    s_splash.prev_buttons = ctrl.buttons;
  SceTouchData touch;
  if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) > 0)
    s_splash.prev_touching = touch.reportNum > 0;

  const uint64_t sample_start_us = sceKernelGetProcessTimeWide();
  sample_logo(logo, pixels);
  s_sample_us = sceKernelGetProcessTimeWide() - sample_start_us;
}

uint64_t ui_splash_sample_us(void) {
  return s_sample_us;
}

bool ui_splash_active(void) {
  return s_splash.active;
}

void ui_splash_poll_skip(void) {
  if (!s_splash.active)
    return;

  bool pressed = false;
  SceCtrlData ctrl;
  if (sceCtrlPeekBufferPositive(0, &ctrl, 1) > 0) {
    pressed = (ctrl.buttons & ~s_splash.prev_buttons) != 0;
    s_splash.prev_buttons = ctrl.buttons;
  }
  SceTouchData touch;
  if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) > 0) {
    const bool touching = touch.reportNum > 0;
    pressed = pressed || (touching && !s_splash.prev_touching);
    s_splash.prev_touching = touching;
  }

  /* A press while loading is not done, or once the dissolve runs, does nothing. */
  const uint64_t now = sceKernelGetProcessTimeWide();
  if (pressed && s_splash.loading_done && !dissolve_started(now)) {
    const float d_ms = ms_between(s_splash.start_us, s_splash.dissolve_us);
    const float new_d_ms =
        ui_splash_skip_dissolve_start_ms(&s_timing, ms_between(s_splash.start_us, now), d_ms);
    s_splash.dissolve_us = s_splash.start_us + (uint64_t)(new_d_ms * US_PER_MS);
  }
}

/** Draw the crisp logo at @alpha (0..1), @scale times its size, centred on its rectangle. */
static void draw_logo(float alpha, float scale) {
  if (!s_splash.logo || alpha <= 0.0f)
    return;
  const float w = (float)UI_SPLASH_LOGO_W * scale;
  const float h = (float)UI_SPLASH_LOGO_H * scale;
  const float x = (float)UI_SPLASH_LOGO_X + ((float)UI_SPLASH_LOGO_W - w) / 2.0f;
  const float y = (float)UI_SPLASH_LOGO_Y + ((float)UI_SPLASH_LOGO_H - h) / 2.0f;
  vita2d_draw_texture_tint_scale(
      s_splash.logo, x, y, w / (float)vita2d_texture_get_width(s_splash.logo),
      h / (float)vita2d_texture_get_height(s_splash.logo), rgba_white(alpha));
}

/** Draw every visible particle at splash-clock time @t_ms as one triangle list. */
static void draw_particles(float t_ms) {
  vita2d_color_vertex *verts = vita2d_pool_memalign(
      (unsigned int)(s_splash.count * VERTS_PER_QUAD * sizeof(vita2d_color_vertex)),
      sizeof(vita2d_color_vertex));
  if (!verts) {
    if (!s_splash.pool_warned) {
      LOGE("UI/SPLASH vita2d pool exhausted, skipping particles");
      s_splash.pool_warned = true;
    }
    return;
  }
  unsigned int used = 0;
  for (int i = 0; i < s_splash.count; i++) {
    UiSplashQuad q;
    if (!ui_splash_particle_quad(&s_splash.particles[i], &s_params, t_ms, &q))
      continue;
    const uint32_t color = rgba_white(q.alpha);
    const float x0 = (float)q.x;
    const float y0 = (float)q.y;
    const float x1 = (float)(q.x + q.size);
    const float y1 = (float)(q.y + q.size);
    vita2d_color_vertex *v = &verts[used];
    v[0] = (vita2d_color_vertex){x0, y0, UI_SPLASH_Z, color};
    v[1] = (vita2d_color_vertex){x1, y0, UI_SPLASH_Z, color};
    v[2] = (vita2d_color_vertex){x0, y1, UI_SPLASH_Z, color};
    v[3] = (vita2d_color_vertex){x1, y0, UI_SPLASH_Z, color};
    v[4] = (vita2d_color_vertex){x1, y1, UI_SPLASH_Z, color};
    v[5] = (vita2d_color_vertex){x0, y1, UI_SPLASH_Z, color};
    used += VERTS_PER_QUAD;
  }
  if (used > 0)
    vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, verts, used);
}

/**
 * Draw the splash's own layers at @now_us over what is already in the scene: before D the logo
 * fading in or holding (no particles); from D the crisp logo thinning out and the particles, both
 * from the reversed #366 clock.
 */
static void draw_layer(uint64_t now_us) {
  s_splash.frames++;
  if (!dissolve_started(now_us)) {
    const float elapsed_ms = ms_between(s_splash.start_us, now_us);
    draw_logo(ui_splash_logo_in_alpha(&s_timing, elapsed_ms),
              ui_splash_logo_in_scale(&s_timing, elapsed_ms));
    return;
  }

  const float since_d_ms = ms_between(s_splash.dissolve_us, now_us);
  const float clock_ms = ui_splash_dissolve_clock_ms(&s_timing, since_d_ms);
  const bool have_particles = s_splash.particles && s_splash.count > 0;
  /* Without particles (a sampling failure, already logged) the logo simply fades out. */
  const float logo_alpha =
      have_particles ? ui_splash_logo_alpha(s_splash.particles, s_splash.count, &s_params, clock_ms)
                     : clock_ms / UI_SPLASH_DISSOLVE_MS;
  draw_logo(logo_alpha, 1.0f);
  if (have_particles)
    draw_particles(clock_ms);
}

void ui_splash_draw(void) {
  if (s_splash.active)
    draw_layer(sceKernelGetProcessTimeWide());
}

void ui_splash_loading_done(void) {
  const uint64_t now = sceKernelGetProcessTimeWide();
  s_splash.loading_done = true;
  s_splash.loaded_us = now;
  const float d_ms = ui_splash_dissolve_start_ms(&s_timing, ms_between(s_splash.start_us, now));
  s_splash.dissolve_us = s_splash.start_us + (uint64_t)(d_ms * US_PER_MS);
}

bool ui_splash_ready_to_exit(void) {
  return s_splash.active && s_splash.loading_done &&
         dissolve_started(sceKernelGetProcessTimeWide());
}

float ui_splash_home_opacity(void) {
  if (!s_splash.active)
    return 1.0f;
  const uint64_t now = sceKernelGetProcessTimeWide();
  return dissolve_started(now)
             ? ui_splash_timeline_home_opacity(&s_timing, ms_between(s_splash.dissolve_us, now))
             : 0.0f;
}

bool ui_splash_home_input_enabled(void) {
  if (!s_splash.active)
    return true;
  const uint64_t now = sceKernelGetProcessTimeWide();
  return dissolve_started(now) &&
         ui_splash_timeline_home_input_enabled(&s_timing, ms_between(s_splash.dissolve_us, now));
}

/** Log the PIPE/SPLASH_DISSOLVE line: the timeline as it actually ran, relative to the start. */
static void log_dissolve(void) {
  LOGD("PIPE/SPLASH_DISSOLVE loaded_ms=%llu dissolve_start_ms=%llu ui_full_ms=%llu end_ms=%llu",
       (unsigned long long)((s_splash.loaded_us - s_splash.start_us) / 1000),
       (unsigned long long)((s_splash.dissolve_us - s_splash.start_us) / 1000),
       (unsigned long long)((s_splash.ui_full_us - s_splash.start_us) / 1000),
       (unsigned long long)((s_splash.end_us - s_splash.start_us) / 1000));
}

bool ui_splash_draw_exit(void) {
  if (!s_splash.active)
    return true;
  const uint64_t now = sceKernelGetProcessTimeWide();
  if (!dissolve_started(now)) {
    draw_layer(now);
    return false;
  }

  const float since_d_ms = ms_between(s_splash.dissolve_us, now);
  if (s_splash.ui_full_us == 0 && ui_splash_timeline_home_input_enabled(&s_timing, since_d_ms))
    s_splash.ui_full_us = now;
  if (ui_splash_dissolve_done(&s_timing, since_d_ms)) {
    free(s_splash.particles);
    s_splash.particles = NULL;
    s_splash.count = 0;
    s_splash.active = false;
    s_splash.end_us = now;
    log_dissolve();
    return true;
  }
  draw_layer(now);
  return false;
}

uint64_t ui_splash_start_us(void) {
  return s_splash.start_us;
}

uint32_t ui_splash_frames_drawn(void) {
  return s_splash.frames;
}

uint64_t ui_splash_elapsed_us(void) {
  const uint64_t end = s_splash.active ? sceKernelGetProcessTimeWide() : s_splash.end_us;
  return end - s_splash.start_us;
}
