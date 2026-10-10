// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) test for the splash particle sampler in vita/src/ui/ui_splash_particles.c
// (ticket #366). `./tools/build.sh test` only cross-compiles, so run it on the host:
//   cc -std=c99 -I vita/include test/ui_splash_particles_tests.c vita/src/ui/ui_splash_particles.c \
//      -lm -o /tmp/ui_splash_particles_tests && /tmp/ui_splash_particles_tests

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ui/ui_splash_particles.h"

#define IMG_W 90
#define IMG_H 54
#define LOGO_X 100
#define LOGO_Y 50
#define GUARD 4

/* Same numbers as the UI_SPLASH_* block in ui_theme.h, with the logo drawn 1:1 from the image. */
static UiSplashParams test_params(void) {
  UiSplashParams p;
  memset(&p, 0, sizeof(p));
  p.screen_w = 960;
  p.screen_h = 544;
  p.logo_x = LOGO_X;
  p.logo_y = LOGO_Y;
  p.logo_w = IMG_W;
  p.logo_h = IMG_H;
  p.cell = 9;
  p.cell_fill = 0.35f;
  p.alpha_min = 200;
  p.red_min = 200;
  p.seed = 366u;
  p.lcg_mul = 1664525u;
  p.lcg_add = 1013904223u;
  p.lcg_range = 4294967296.0;
  p.margin_min = -10.0f;
  p.margin_range = 70.0f;
  p.size_small = 2;
  p.size_large = 3;
  p.small_chance = 0.55f;
  p.bow_px = 70.0f;
  p.stagger_ms = 300.0f;
  p.flight_ms = 600.0f;
  p.absorb_ms = 120.0f;
  p.alpha_start = 0.12f;
  p.logo_gamma = 1.3f;
  p.assembly_ms = 1000.0f;
  return p;
}

static void set_px(uint8_t *img, int x, int y, uint8_t r, uint8_t a) {
  uint8_t *px = img + ((size_t)y * IMG_W + (size_t)x) * 4;
  px[0] = r;
  px[1] = r;
  px[2] = r;
  px[3] = a;
}

/* Three vertical bands: bright opaque (columns 0..29), dark opaque outline (30..59) and
 * transparent but bright-coloured (60..89). Only the first may receive particles. */
static void fill_bands(uint8_t *img) {
  for (int y = 0; y < IMG_H; y++) {
    for (int x = 0; x < IMG_W; x++) {
      if (x < 30)
        set_px(img, x, y, 255, 255);
      else if (x < 60)
        set_px(img, x, y, 20, 255);
      else
        set_px(img, x, y, 255, 0);
    }
  }
}

/** Every target is a bright, opaque pixel of the image: never the dark outline, never a gap. */
static void test_targets_land_only_on_bright_opaque_pixels(void) {
  static uint8_t img[IMG_W * IMG_H * 4];
  UiSplashParticle out[64];
  UiSplashParams p = test_params();

  fill_bands(img);
  const int n = ui_splash_sample(img, IMG_W, IMG_H, IMG_W, &p, out, 64);

  assert(n > 0);
  for (int i = 0; i < n; i++) {
    const int x = (int)out[i].tx - LOGO_X;
    const int y = (int)out[i].ty - LOGO_Y;
    assert(x >= 0 && x < 30);
    assert(y >= 0 && y < IMG_H);
  }
}

/** A row stride wider than the image is honoured: padding bytes are never read as pixels. */
static void test_row_stride_skips_padding(void) {
  enum { STRIDE = IMG_W + 10 };
  static uint8_t img[STRIDE * IMG_H * 4];
  UiSplashParticle out[64];
  UiSplashParams p = test_params();

  /* Padding is bright and opaque; the image itself is entirely dark. */
  for (int y = 0; y < IMG_H; y++)
    for (int x = 0; x < STRIDE; x++) {
      uint8_t *px = img + ((size_t)y * STRIDE + (size_t)x) * 4;
      px[0] = px[1] = px[2] = (x < IMG_W) ? 20 : 255;
      px[3] = 255;
    }
  assert(ui_splash_sample(img, IMG_W, IMG_H, STRIDE, &p, out, 64) == 0);
}

/** The output array is never overrun, even when the image would yield many more particles. */
static void test_count_never_exceeds_capacity(void) {
  static uint8_t img[IMG_W * IMG_H * 4];
  enum { CAPACITY = 5 };
  UiSplashParticle out[CAPACITY + GUARD];
  UiSplashParams p = test_params();

  for (int y = 0; y < IMG_H; y++)
    for (int x = 0; x < IMG_W; x++)
      set_px(img, x, y, 255, 255);
  memset(out, 0x5A, sizeof(out));

  const int n = ui_splash_sample(img, IMG_W, IMG_H, IMG_W, &p, out, CAPACITY);

  assert(n == CAPACITY);
  const uint8_t *guard = (const uint8_t *)&out[CAPACITY];
  for (size_t i = 0; i < GUARD * sizeof(UiSplashParticle); i++)
    assert(guard[i] == 0x5A);
}

int main(void) {
  test_targets_land_only_on_bright_opaque_pixels();
  test_row_stride_skips_padding();
  test_count_never_exceeds_capacity();
  printf("ui_splash_particles_tests: ok\n");
  return 0;
}
