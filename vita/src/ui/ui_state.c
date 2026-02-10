/**
 * @file ui_state.c
 * @brief Global UI state management implementation for VitaRPS5
 *
 * This module manages high-level UI state including connection overlays,
 * stream cooldowns, text caching, and connection thread management.
 *
 * Implementation notes:
 * - Connection overlay tracks multi-stage connection flows
 * - Cooldown system prevents rapid reconnect attempts after errors
 * - Text cache optimizes repeated width calculations for static strings
 * - Connection thread allows async streaming without blocking UI
 */

// Include context.h BEFORE ui_internal.h to avoid circular dependency issues
// context.h -> ui.h has duplicate definitions with ui_types.h (included by ui_internal.h)
// Including context.h first ensures ui.h types take precedence
#include "context.h"

#include "ui/ui_state.h"
#include "ui/ui_internal.h"
#include "ui/ui_focus.h"
#include "host.h"
#include "logging.h"

#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>
#include <string.h>

// ============================================================================
// Module State
// ============================================================================

/**
 * Connection overlay state
 * Tracks waking + fast connect flows across multiple stages
 */
static ConnectionOverlayState connection_overlay = {0};

/**
 * Connection worker thread state
 * Allows async streaming without blocking UI
 */
static SceUID connection_thread_id = -1;
static VitaChiakiHost *connection_thread_host = NULL;
static bool connection_overlay_modal_pushed = false;

/**
 * Waking and reconnect timing state
 * Used for timeout tracking and animation
 * Note: Use uint64_t for microsecond timestamps to avoid overflow after ~71 minutes
 */
static uint64_t waking_start_time = 0;
static uint64_t waking_wait_for_stream_us = 0;
static uint64_t reconnect_start_time = 0;
static int reconnect_animation_frame = 0;

/**
 * Text width cache
 * Simple optimization for static strings to avoid repeated font measurements
 */
#define TEXT_WIDTH_CACHE_SIZE 16
static TextWidthCacheEntry text_width_cache[TEXT_WIDTH_CACHE_SIZE] = {0};
static int next_cache_slot = 0;

// ============================================================================
// Initialization
// ============================================================================

void ui_state_init(void) {
  // Reset connection overlay
  connection_overlay.active = false;
  connection_overlay.stage = UI_CONNECTION_STAGE_NONE;
  connection_overlay.stage_updated_us = 0;

  // Reset connection thread
  connection_thread_id = -1;
  connection_thread_host = NULL;
  connection_overlay_modal_pushed = false;

  // Reset timing state
  waking_start_time = 0;
  waking_wait_for_stream_us = 0;
  reconnect_start_time = 0;
  reconnect_animation_frame = 0;

  // Clear text cache
  ui_text_cache_clear();
}

// ============================================================================
// Connection Overlay Implementation
// ============================================================================

void ui_connection_begin(UIConnectionStage stage) {
  connection_overlay.active = true;
  connection_overlay.stage = stage;
  connection_overlay.stage_updated_us = sceKernelGetProcessTimeWide();
  waking_start_time = 0;
  waking_wait_for_stream_us = 0;

  // Push modal focus when connection overlay activates
  if (!connection_overlay_modal_pushed) {
    ui_focus_push_modal();
    connection_overlay_modal_pushed = true;
  }
}

void ui_connection_set_stage(UIConnectionStage stage) {
  if (!connection_overlay.active || connection_overlay.stage == stage)
    return;
  connection_overlay.stage = stage;
  connection_overlay.stage_updated_us = sceKernelGetProcessTimeWide();
}

void ui_connection_complete(void) {
  connection_overlay.active = false;
  waking_start_time = 0;
  waking_wait_for_stream_us = 0;

  // Pop modal focus only if this overlay owns a modal push.
  if (connection_overlay_modal_pushed) {
    ui_focus_pop_modal();
  }
  connection_overlay_modal_pushed = false;
}

void ui_connection_cancel(void) {
  connection_overlay.active = false;
  waking_start_time = 0;
  waking_wait_for_stream_us = 0;
  connection_thread_host = NULL;

  // Terminate connection thread if running
  if (connection_thread_id >= 0) {
    sceKernelWaitThreadEnd(connection_thread_id, NULL, NULL);
    sceKernelDeleteThread(connection_thread_id);
    connection_thread_id = -1;
  }

  // Pop modal focus only if this overlay owns a modal push.
  if (connection_overlay_modal_pushed) {
    ui_focus_pop_modal();
  }
  connection_overlay_modal_pushed = false;
}

bool ui_connection_is_active(void) {
  return connection_overlay.active;
}

UIConnectionStage ui_connection_get_stage(void) {
  return connection_overlay.stage;
}

void ui_connection_clear_waking_wait(void) {
  waking_wait_for_stream_us = 0;
}

// ============================================================================
// Connection Thread Implementation
// ============================================================================

/**
 * Connection thread worker function
 * Calls host_stream() asynchronously to avoid blocking UI
 */
static int connection_thread_func(SceSize args, void *argp) {
  VitaChiakiHost *host = connection_thread_host;
  if (!host)
    host = context.active_host;
  host_stream(host);
  connection_thread_host = NULL;
  connection_thread_id = -1;
  sceKernelExitDeleteThread(0);
  return 0;
}

bool ui_connection_start_thread(VitaChiakiHost *host) {
  // Already running
  if (connection_thread_id >= 0)
    return true;

  connection_thread_host = host;
  connection_thread_id = sceKernelCreateThread(
      "VitaConnWorker",
      connection_thread_func,
      0x40,
      0x10000,
      0,
      0,
      NULL);

  if (connection_thread_id < 0) {
    LOGE("Failed to create connection worker thread (%d)", connection_thread_id);
    connection_thread_host = NULL;
    connection_thread_id = -1;
    return false;
  }

  int status = sceKernelStartThread(connection_thread_id, 0, NULL);
  if (status < 0) {
    LOGE("Failed to start connection worker thread (%d)", status);
    sceKernelDeleteThread(connection_thread_id);
    connection_thread_id = -1;
    connection_thread_host = NULL;
    return false;
  }

  return true;
}

// ============================================================================
// Stream Cooldown Implementation
// ============================================================================

bool ui_cooldown_active(void) {
  uint64_t until_us = 0;

  // Check Takion overflow backoff
  if (context.stream.takion_cooldown_overlay_active &&
      context.stream.takion_overflow_backoff_until_us > until_us) {
    until_us = context.stream.takion_overflow_backoff_until_us;
  }

  // Check general stream cooldown
  if (context.stream.next_stream_allowed_us > until_us) {
    until_us = context.stream.next_stream_allowed_us;
  }

  if (!until_us)
    return false;

  uint64_t now_us = sceKernelGetProcessTimeWide();
  if (now_us >= until_us) {
    // Cooldown expired - clear the flags
    if (context.stream.takion_cooldown_overlay_active &&
        context.stream.takion_overflow_backoff_until_us <= now_us) {
      context.stream.takion_cooldown_overlay_active = false;
      context.stream.takion_overflow_backoff_until_us = 0;
    }
    if (context.stream.next_stream_allowed_us <= now_us)
      context.stream.next_stream_allowed_us = 0;
    return false;
  }

  return true;
}

uint64_t ui_cooldown_remaining_us(void) {
  uint64_t until_us = 0;

  if (context.stream.takion_cooldown_overlay_active &&
      context.stream.takion_overflow_backoff_until_us > until_us) {
    until_us = context.stream.takion_overflow_backoff_until_us;
  }
  if (context.stream.next_stream_allowed_us > until_us) {
    until_us = context.stream.next_stream_allowed_us;
  }

  return until_us;
}

bool ui_cooldown_takion_gate_active(void) {
  return ui_cooldown_active();
}

// ============================================================================
// Text Width Caching Implementation
// ============================================================================

int ui_text_width_cached(const char* text, int font_size) {
  // Try to find in cache (pointer comparison for static strings)
  for (int i = 0; i < TEXT_WIDTH_CACHE_SIZE; i++) {
    if (text_width_cache[i].valid &&
        text_width_cache[i].text == text &&  // Pointer comparison
        text_width_cache[i].font_size == font_size) {
      return text_width_cache[i].width;
    }
  }

  // Not in cache - calculate and store
  int width = vita2d_font_text_width(font, font_size, text);

  // Store in cache using simple FIFO replacement
  text_width_cache[next_cache_slot].text = text;
  text_width_cache[next_cache_slot].font_size = font_size;
  text_width_cache[next_cache_slot].width = width;
  text_width_cache[next_cache_slot].valid = true;
  next_cache_slot = (next_cache_slot + 1) % TEXT_WIDTH_CACHE_SIZE;

  return width;
}

void ui_text_cache_clear(void) {
  memset(text_width_cache, 0, sizeof(text_width_cache));
  next_cache_slot = 0;
}

// ============================================================================
// Waking & Reconnect State Accessors
// ============================================================================

uint64_t ui_state_get_waking_start_time_us(void) {
  return waking_start_time;
}

void ui_state_set_waking_start_time_us(uint64_t time_us) {
  waking_start_time = time_us;
}

uint64_t ui_state_get_waking_wait_for_stream_us(void) {
  return waking_wait_for_stream_us;
}

void ui_state_set_waking_wait_for_stream_us(uint64_t time_us) {
  waking_wait_for_stream_us = time_us;
}

uint64_t ui_state_get_reconnect_start_time(void) {
  return reconnect_start_time;
}

void ui_state_set_reconnect_start_time(uint64_t time) {
  reconnect_start_time = time;
}

int ui_state_get_reconnect_animation_frame(void) {
  return reconnect_animation_frame;
}

void ui_state_set_reconnect_animation_frame(int frame) {
  reconnect_animation_frame = frame;
}

// ============================================================================
// Internal Functions (for ui.c compatibility, exposed via ui_internal.h)
// ============================================================================

/**
 * Check if stream cooldown is active (internal alias)
 * Original function name used in ui.c
 */
bool stream_cooldown_active(void) {
  return ui_cooldown_active();
}

/**
 * Get cooldown end time (internal alias)
 * Original function name used in ui.c
 */
uint64_t stream_cooldown_until_us(void) {
  return ui_cooldown_remaining_us();
}

/**
 * Check if Takion gate is active (internal alias)
 * Original function name used in ui.c
 */
bool takion_cooldown_gate_active(void) {
  return ui_cooldown_takion_gate_active();
}

/**
 * Start connection thread (internal alias)
 * Original function name used in ui.c
 */
bool start_connection_thread(VitaChiakiHost *host) {
  return ui_connection_start_thread(host);
}

/**
 * Get cached text width (internal alias)
 * Original function name used in ui.c
 */
int get_text_width_cached(const char* text, int font_size) {
  return ui_text_width_cached(text, font_size);
}

/**
 * Check if connection overlay is active (internal alias)
 * Original function name used in ui.c
 */
bool ui_connection_overlay_active(void) {
  return ui_connection_is_active();
}

/**
 * Get connection stage (internal alias)
 * Original function name used in ui.c
 */
UIConnectionStage ui_connection_stage(void) {
  return ui_connection_get_stage();
}

/**
 * Clear waking wait (internal alias)
 * Original function name used in ui.c
 */
void ui_clear_waking_wait(void) {
  ui_connection_clear_waking_wait();
}
