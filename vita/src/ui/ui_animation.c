/**
 * @file ui_animation.c
 * @brief Animation timing utilities
 */

#include "ui/ui_animation.h"
#include "ui/ui_internal.h"

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
