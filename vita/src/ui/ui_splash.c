/**
 * @file ui_splash.c
 * @brief Splash screen (see ui_splash.h)
 *
 * The motion is a port of docs/design/ui-mocks/splash.html; the pure maths lives in
 * ui_splash_particles.c. The clock is time based: a skip moves the clock origin so the clock
 * reads the assembly time. Particles are one triangle list (two triangles per visible particle,
 * vertex colour carrying the alpha), written into vita2d's per-frame pool, which vita2d resets
 * each frame: at most UI_SPLASH_MAX_PARTICLES * 6 * sizeof(vita2d_color_vertex) = 144000 bytes of
 * the 1 MiB default pool.
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
  bool exiting;
  bool pool_warned;
  uint64_t start_us;      /**< Real time of ui_splash_start(). */
  uint64_t clock_zero_us; /**< The splash clock reads now - this; a skip moves it back. */
  uint64_t exit_start_us;
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
    .logo_gamma = UI_SPLASH_LOGO_GAMMA,
    .assembly_ms = UI_SPLASH_ASSEMBLY_MS,
};

/** Splash clock in milliseconds. */
static float clock_ms(void) {
  return (float)(sceKernelGetProcessTimeWide() - s_splash.clock_zero_us) / US_PER_MS;
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
    LOGE("UI/SPLASH no logo texture or pixels, splash draws black only");
    return;
  }
  s_splash.particles = malloc(UI_SPLASH_MAX_PARTICLES * sizeof(UiSplashParticle));
  if (!s_splash.particles) {
    LOGE("UI/SPLASH particle allocation failed (%u bytes), splash draws black only",
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
  s_splash.clock_zero_us = now;

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

  if (pressed && !ui_splash_assembled()) {
    const uint64_t assembly_us = (uint64_t)(UI_SPLASH_ASSEMBLY_MS * US_PER_MS);
    s_splash.clock_zero_us = sceKernelGetProcessTimeWide() - assembly_us;
  }
}

bool ui_splash_assembled(void) {
  return s_splash.active && clock_ms() >= UI_SPLASH_ASSEMBLY_MS;
}

/** Draw the black cover, the crisp logo and the particles at the clock time, times @opacity. */
static void draw_layer(float opacity) {
  const float t_ms = clock_ms();
  vita2d_draw_rectangle(0.0f, 0.0f, (float)VITA_WIDTH, (float)VITA_HEIGHT,
                        RGBA8(0, 0, 0, alpha_byte(opacity)));
  s_splash.frames++;
  if (!s_splash.particles || s_splash.count <= 0)
    return;

  const float logo_alpha =
      ui_splash_logo_alpha(s_splash.particles, s_splash.count, &s_params, t_ms);
  if (logo_alpha * opacity > 0.0f) {
    const float sx = (float)UI_SPLASH_LOGO_W / (float)vita2d_texture_get_width(s_splash.logo);
    const float sy = (float)UI_SPLASH_LOGO_H / (float)vita2d_texture_get_height(s_splash.logo);
    vita2d_draw_texture_tint_scale(s_splash.logo, (float)UI_SPLASH_LOGO_X, (float)UI_SPLASH_LOGO_Y,
                                   sx, sy, rgba_white(logo_alpha * opacity));
  }

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
    const uint32_t color = rgba_white(q.alpha * opacity);
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

void ui_splash_draw(void) {
  if (s_splash.active)
    draw_layer(1.0f);
}

void ui_splash_loading_done(void) {
  s_splash.loading_done = true;
}

bool ui_splash_ready_to_exit(void) {
  return s_splash.active && s_splash.loading_done && ui_splash_assembled();
}

bool ui_splash_draw_exit(void) {
  if (!s_splash.active)
    return true;
  if (!ui_splash_ready_to_exit()) {
    draw_layer(1.0f);
    return false;
  }

  const uint64_t now = sceKernelGetProcessTimeWide();
  if (!s_splash.exiting) {
    s_splash.exiting = true;
    s_splash.exit_start_us = now;
  }
  const float opacity =
      1.0f - (float)(now - s_splash.exit_start_us) / US_PER_MS / UI_SPLASH_FADE_MS;
  if (opacity <= 0.0f) {
    free(s_splash.particles);
    s_splash.particles = NULL;
    s_splash.count = 0;
    s_splash.active = false;
    s_splash.end_us = now;
    return true;
  }
  draw_layer(opacity);
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
