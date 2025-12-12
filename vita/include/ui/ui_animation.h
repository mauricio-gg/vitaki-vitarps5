/**
 * @file ui_animation.h
 * @brief Animation framework and particle system for VitaRPS5 UI
 *
 * This module provides:
 * - Particle background animation system (8-particle floating symbols)
 * - Animation timing utilities
 * - Easing functions (defined inline in ui_internal.h)
 *
 * Performance notes:
 * - Particle updates run at 30fps (every other frame) for CPU efficiency
 * - Particles are constrained to content area to avoid wave nav overlap
 * - Dual-layer particle system with different speeds for depth effect
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Particle System Public API
// ============================================================================

/**
 * Initialize the particle background animation system
 *
 * Creates 8 particles with randomized:
 * - Initial positions (above screen, constrained to content area)
 * - Velocities (downward with slight horizontal drift)
 * - Scales (0.30 to 0.80)
 * - Rotation speeds
 * - Symbol types (triangle, circle, X, square)
 * - Layer assignment (background 0.7x speed, foreground 1.0x speed)
 * - Sway animation parameters
 *
 * Safe to call multiple times (no-op if already initialized).
 */
void ui_particles_init(void);

/**
 * Update particle physics and animation
 *
 * Call once per frame. Internally updates at 30fps (every other frame)
 * for performance optimization. Updates:
 * - Position based on velocity and layer speed
 * - Rotation based on rotation speed
 * - Sway phase for horizontal oscillation
 * - Wraps particles that fall off screen
 *
 * Must call ui_particles_init() first.
 */
void ui_particles_update(void);

/**
 * Render all active particles
 *
 * Draws particles with:
 * - Scale transformation
 * - Rotation transformation
 * - Horizontal sway offset
 *
 * Renders in layer order (background particles first).
 * Must call ui_particles_init() first.
 */
void ui_particles_render(void);

// ============================================================================
// Animation Timing Utilities
// ============================================================================

/**
 * Get current time in microseconds
 *
 * Uses sceKernelGetProcessTimeWide() for high-precision timing.
 * Suitable for animation timestamping and delta calculations.
 *
 * @return Current time in microseconds since process start
 */
uint64_t ui_anim_now_us(void);

/**
 * Calculate elapsed milliseconds from a start timestamp
 *
 * @param start_us Start time in microseconds (from ui_anim_now_us())
 * @return Elapsed time in milliseconds as float
 */
float ui_anim_elapsed_ms(uint64_t start_us);

// ============================================================================
// Easing Functions (available as inline functions in ui_internal.h)
// ============================================================================

// Note: The following functions are defined inline in ui_internal.h
// for performance. They are listed here for documentation purposes.
//
// float ui_lerp(float a, float b, float t)
//   - Linear interpolation between a and b by factor t (0.0 to 1.0)
//
// float ui_ease_in_out_cubic(float t)
//   - Cubic ease-in-out function for smooth animations
//   - Input t: 0.0 to 1.0
//   - Returns smoothed value with slow start and slow end
//
// float ui_clamp(float val, float min, float max)
//   - Clamps value between min and max
