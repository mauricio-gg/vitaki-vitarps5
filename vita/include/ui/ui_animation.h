/**
 * @file ui_animation.h
 * @brief Animation timing utilities and easing functions for VitaRPS5 UI
 *
 * This module provides:
 * - Animation timing utilities
 * - Easing functions (defined inline in ui_internal.h)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

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
