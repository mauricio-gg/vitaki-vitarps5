/**
 * @file ui_animation.c
 * @brief Animation framework and particle system implementation
 *
 * This module implements the particle background animation and timing utilities.
 * The particle system creates a subtle, animated background layer with floating
 * PlayStation symbols (triangle, circle, X, square) that fall and sway.
 *
 * Performance optimizations:
 * - Particles update at 30fps (every other frame) instead of 60fps
 * - Layer-based speed multipliers create depth effect (0.7x and 1.0x)
 * - Particles constrained to content area (avoid wave navigation overlap)
 *
 * Design notes:
 * - 8 particles total (reduced from 12 for performance)
 * - Dual-layer system (background/foreground) for parallax depth
 * - Horizontal sway adds organic motion
 * - Particles respawn at top when falling off bottom
 */

#include "ui/ui_animation.h"
#include "ui/ui_internal.h"

#include <math.h>
#include <stdlib.h>

// ============================================================================
// Particle System State (Module-Private)
// ============================================================================

/**
 * Particle array
 *
 * Fixed array of PARTICLE_COUNT (8) particles.
 * Each particle maintains position, velocity, rotation, and animation state.
 */
static Particle particles[PARTICLE_COUNT];

/**
 * Initialization flag
 *
 * Prevents re-initialization of particle system.
 * Set to true after first call to ui_particles_init().
 */
static bool particles_initialized = false;

/**
 * Frame counter for 30fps update optimization
 *
 * Incremented each call to ui_particles_update().
 * Physics updates only occur on even frames (particle_update_frame % 2 == 0).
 */
static int particle_update_frame = 0;

// ============================================================================
// Particle System Implementation
// ============================================================================

/**
 * Initialize particle system with random positions and velocities
 */
void ui_particles_init(void) {
  // Prevent re-initialization
  if (particles_initialized) {
    return;
  }

  // Seed random number generator with current time
  srand((unsigned int)sceKernelGetProcessTimeWide());

  for (int i = 0; i < PARTICLE_COUNT; i++) {
    // Constrain particles to content area only (not in wave navigation area)
    particles[i].x = CONTENT_AREA_X + (float)(rand() % CONTENT_AREA_WIDTH);
    particles[i].y = -(float)(rand() % 200);  // Start above screen (0 to -200)

    // Slight horizontal drift
    particles[i].vx = ((float)(rand() % 100) / 100.0f - 0.5f) * 0.5f;

    // Downward velocity (positive Y is down, simulates gravity)
    particles[i].vy = ((float)(rand() % 100) / 100.0f + 0.3f) * 1.2f;

    // Scale: 0.30 to 0.80 (2x bigger than original)
    particles[i].scale = 0.30f + ((float)(rand() % 100) / 100.0f) * 0.50f;

    // Random initial rotation (0-360 degrees)
    particles[i].rotation = (float)(rand() % 360);

    // Rotation speed: -0.5 to +0.5 degrees per frame (half speed)
    particles[i].rotation_speed = ((float)(rand() % 100) / 100.0f - 0.5f) * 1.0f;

    // Symbol type: 0=triangle, 1=circle, 2=x, 3=square
    particles[i].symbol_type = rand() % 4;

    // Assign color based on symbol type
    switch (particles[i].symbol_type) {
      case 0:
        particles[i].color = PARTICLE_COLOR_RED;
        break;
      case 1:
        particles[i].color = PARTICLE_COLOR_BLUE;
        break;
      case 2:
        particles[i].color = PARTICLE_COLOR_GREEN;
        break;
      case 3:
        particles[i].color = PARTICLE_COLOR_ORANGE;
        break;
      default:
        particles[i].color = PARTICLE_COLOR_BLUE;
        break;
    }

    particles[i].active = true;

    // Initialize layer and sway animation parameters
    // 50/50 split between background (0) and foreground (1)
    particles[i].layer = (rand() % 2);

    // Random starting phase for sway (0-360 degrees converted to radians)
    particles[i].sway_phase = ((float)(rand() % 360)) * (M_PI / 180.0f);

    // Sway speed randomized between min and max
    particles[i].sway_speed = PARTICLE_SWAY_SPEED_MIN +
                              ((float)(rand() % 100) / 100.0f) *
                              (PARTICLE_SWAY_SPEED_MAX - PARTICLE_SWAY_SPEED_MIN);
  }

  particles_initialized = true;
}

/**
 * Update particle positions and rotation
 *
 * Optimized to run at 30fps instead of 60fps (50% CPU reduction).
 * Imperceptible for background animation, significant performance gain.
 */
void ui_particles_update(void) {
  if (!particles_initialized) {
    return;
  }

  // Update particles every other frame (30fps instead of 60fps)
  particle_update_frame++;
  if (particle_update_frame % 2 != 0) {
    return;
  }

  for (int i = 0; i < PARTICLE_COUNT; i++) {
    if (!particles[i].active) {
      continue;
    }

    // Apply layer-based speed multiplier
    // Background layer (0) moves at 0.7x speed for depth effect
    // Foreground layer (1) moves at 1.0x speed
    float layer_speed = (particles[i].layer == 0) ?
                        PARTICLE_LAYER_BG_SPEED :
                        PARTICLE_LAYER_FG_SPEED;

    // Update position with layer speed (doubled for 30fps compensation)
    particles[i].x += particles[i].vx * 2.0f * layer_speed;
    particles[i].y += particles[i].vy * 2.0f * layer_speed;

    // Update rotation (doubled for 30fps compensation)
    particles[i].rotation += particles[i].rotation_speed * 2.0f;

    // Update sway phase (30fps update rate)
    particles[i].sway_phase += particles[i].sway_speed * (1.0f / 30.0f);

    // Wrap around screen edges
    // Respawn at top when falling off bottom
    if (particles[i].y > VITA_HEIGHT + 50) {
      particles[i].y = -(float)(rand() % 100);  // Respawn at top
      particles[i].x = CONTENT_AREA_X + (float)(rand() % CONTENT_AREA_WIDTH);
    }

    // Horizontal wrapping (keep particles constrained to content area)
    if (particles[i].x < CONTENT_AREA_X - 50) {
      particles[i].x = VITA_WIDTH + 50;
    }
    if (particles[i].x > VITA_WIDTH + 50) {
      particles[i].x = CONTENT_AREA_X - 50;
    }
  }
}

/**
 * Render all active particles
 */
void ui_particles_render(void) {
  if (!particles_initialized) {
    return;
  }

  // Symbol texture array mapping symbol_type to texture
  vita2d_texture* symbol_textures[4] = {
    symbol_triangle,
    symbol_circle,
    symbol_ex,
    symbol_square
  };

  for (int i = 0; i < PARTICLE_COUNT; i++) {
    if (!particles[i].active) {
      continue;
    }

    vita2d_texture* tex = symbol_textures[particles[i].symbol_type];
    if (!tex) {
      continue;
    }

    // Calculate sway offset for horizontal motion
    float sway_offset = sinf(particles[i].sway_phase) * PARTICLE_SWAY_AMPLITUDE;
    float render_x = particles[i].x + sway_offset;

    // Draw with scale, rotation, and sway
    vita2d_draw_texture_scale_rotate(tex,
      render_x, particles[i].y,
      particles[i].scale, particles[i].scale,
      particles[i].rotation);

    // Note: Color tinting would require custom shader
    // For now particles use texture colors
  }
}

// ============================================================================
// Animation Timing Utilities
// ============================================================================

/**
 * Get current time in microseconds
 */
uint64_t ui_anim_now_us(void) {
  return sceKernelGetProcessTimeWide();
}

/**
 * Calculate elapsed milliseconds from a start timestamp
 */
float ui_anim_elapsed_ms(uint64_t start_us) {
  uint64_t now_us = ui_anim_now_us();
  return (float)(now_us - start_us) / 1000.0f;
}
