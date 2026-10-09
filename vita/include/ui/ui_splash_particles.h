/**
 * @file ui_splash_particles.h
 * @brief Particles of the splash screen: sampling them from the logo and their motion in time
 *
 * A port of build() and render() in docs/design/ui-mocks/splash.html. Pure: no SDK dependency, so
 * the sampling can be checked natively. Every tunable number arrives in UiSplashParams (the splash
 * module fills it from the UI_SPLASH_* constants in ui_theme.h, which pulls in vita2d.h). Times are
 * milliseconds since the splash clock started.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Every tunable of the sampling and the motion; see the UI_SPLASH_* block in ui_theme.h. */
typedef struct {
  int screen_w, screen_h;             /**< Screen the particles start around. */
  int logo_x, logo_y, logo_w, logo_h; /**< Where the logo is drawn, in screen pixels. */
  int cell;                           /**< Side of a sampling cell, in pixels. */
  float cell_fill;                    /**< A cell needs more than cell * cell * this many hits. */
  int alpha_min, red_min;             /**< A pixel counts when its alpha and red exceed these. */
  uint32_t seed, lcg_mul, lcg_add;    /**< The random generator. */
  double lcg_range;                   /**< 2^32. */
  float margin_min, margin_range;     /**< Start distance outside the screen edge. */
  int size_small, size_large;         /**< The two particle sizes in pixels. */
  float small_chance;                 /**< Chance of the small size. */
  float bow_px;                       /**< Largest sideways bow, in pixels. */
  float stagger_ms, flight_ms, absorb_ms;
  float alpha_start; /**< Alpha at the start of the flight. */
  float logo_gamma;  /**< Exponent of the crisp logo's fade-in. */
  float assembly_ms; /**< From here on the crisp logo is whole. */
} UiSplashParams;

/** One particle, with everything that does not change over time worked out once. */
typedef struct {
  float sx, sy; /**< Start, in screen pixels. */
  float tx, ty; /**< Target on the logo, in screen pixels. */
  float bow_x,
      bow_y; /**< Sideways bow at the middle of the flight: normal of start->target times curve. */
  float delay_ms; /**< Wait before the flight starts. */
  int size;       /**< Side in pixels while flying. */
} UiSplashParticle;

/** A particle at one moment: the square to draw. x and y are whole pixels, the top-left corner. */
typedef struct {
  int x, y, size;
  float alpha; /**< 0..1 */
} UiSplashQuad;

/**
 * ui_splash_sample() - Choose the particles for a logo.
 * @rgba:      Logo pixels, 4 bytes each in the order R, G, B, A.
 * @src_w:     Logo width in pixels.
 * @src_h:     Logo height in pixels.
 * @stride_px: Pixels from the start of one row to the next (at least @src_w).
 * @params:    Tunables; the logo is sampled at @params->logo_w x logo_h by nearest neighbour.
 * @out:       Receives the particles.
 * @capacity:  Room in @out; never exceeded.
 *
 * Returns the number of particles written, 0 on bad arguments. Only bright opaque pixels get a
 * target. Nothing is allocated.
 */
int ui_splash_sample(const uint8_t *rgba, int src_w, int src_h, int stride_px,
                     const UiSplashParams *params, UiSplashParticle *out, int capacity);

/**
 * ui_splash_particle_quad() - Where a particle is, how big and how opaque, at time @t_ms.
 *
 * Returns false when it is not visible yet or has been absorbed.
 */
bool ui_splash_particle_quad(const UiSplashParticle *p, const UiSplashParams *params, float t_ms,
                             UiSplashQuad *quad);

/**
 * ui_splash_logo_alpha() - Opacity of the crisp logo drawn under the particles at time @t_ms.
 *
 * Follows the mean flight progress of the @count particles, and is 1 from the assembly time on.
 */
float ui_splash_logo_alpha(const UiSplashParticle *particles, int count,
                           const UiSplashParams *params, float t_ms);
