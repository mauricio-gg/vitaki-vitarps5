/**
 * @file ui_state.h
 * @brief Global UI state management API for VitaRPS5
 *
 * This module manages high-level UI state including:
 * - Connection overlay (waking, connecting, fast connect flows)
 * - Stream cooldown (rate limiting after errors)
 * - Text width caching (performance optimization)
 * - Connection thread management
 *
 * The connection overlay system handles all connection flows:
 * - Standard connection (discovering -> connecting -> streaming)
 * - Wake & connect (waking -> discovering -> connecting -> streaming)
 * - Fast connect (skip discovery when IP is known)
 *
 * Stream cooldown prevents rapid reconnection attempts after errors,
 * implementing backoff timers for both general errors and Takion overflow.
 */

#pragma once

#include "ui_types.h"
#include <stdint.h>
#include <stdbool.h>

// Forward declaration to avoid circular dependency
struct vita_chiaki_host_t;
typedef struct vita_chiaki_host_t VitaChiakiHost;

// ============================================================================
// Initialization
// ============================================================================

/**
 * Initialize state management subsystem
 *
 * Resets all state including connection overlay, cooldowns, and caches.
 * Must be called during UI initialization.
 */
void ui_state_init(void);

// ============================================================================
// Connection Overlay Management
// ============================================================================

/**
 * Begin a connection flow with the specified initial stage
 *
 * Activates the connection overlay and sets the initial stage.
 * Resets waking timers.
 *
 * @param stage Initial connection stage (e.g., UI_CONNECTION_STAGE_DISCOVERING)
 */
void ui_connection_begin(UIConnectionStage stage);

/**
 * Update the current connection stage
 *
 * Changes the displayed stage if the overlay is active.
 * Ignores updates to the same stage.
 *
 * @param stage New connection stage to display
 */
void ui_connection_set_stage(UIConnectionStage stage);

/**
 * Complete the connection flow successfully
 *
 * Deactivates the connection overlay and clears timers.
 * Called when stream starts successfully.
 */
void ui_connection_complete(void);

/**
 * Cancel the current connection flow
 *
 * Deactivates overlay, clears timers, and terminates connection thread.
 * Called when user cancels or connection fails.
 */
void ui_connection_cancel(void);

/**
 * Check if connection overlay is currently active
 *
 * @return true if overlay is showing
 */
bool ui_connection_is_active(void);

/**
 * Get the current connection stage
 *
 * @return Current UIConnectionStage (only valid if overlay is active)
 */
UIConnectionStage ui_connection_get_stage(void);

/**
 * Clear waking wait timer
 *
 * Called when wake-up is complete and streaming should begin immediately.
 */
void ui_connection_clear_waking_wait(void);

// ============================================================================
// Connection Thread Management
// ============================================================================

/**
 * Start asynchronous connection thread
 *
 * Spawns a worker thread that calls host_stream() asynchronously.
 * Prevents UI blocking during connection handshake.
 *
 * @param host Host to connect to (NULL uses context.active_host)
 * @return true if thread started successfully, false on error
 */
bool ui_connection_start_thread(VitaChiakiHost *host);

// ============================================================================
// Stream Cooldown (Rate Limiting)
// ============================================================================

/**
 * Check if stream cooldown is currently active
 *
 * Cooldown activates after stream errors to prevent rapid reconnect attempts.
 * Checks both general cooldown and Takion overflow backoff.
 *
 * @return true if cooldown is active (stream should not start)
 */
bool ui_cooldown_active(void);

/**
 * Get remaining cooldown time in microseconds
 *
 * @return Microseconds until cooldown expires, or 0 if no cooldown
 */
uint64_t ui_cooldown_remaining_us(void);

/**
 * Check if Takion overflow gate is active
 *
 * Takion overflow is a specific error condition requiring backoff.
 * This is a convenience wrapper around ui_cooldown_active().
 *
 * @return true if Takion gate is blocking stream start
 */
bool ui_cooldown_takion_gate_active(void);

// ============================================================================
// Text Width Caching
// ============================================================================

/**
 * Get text width with caching for static strings
 *
 * Caches text width calculations to avoid expensive vita2d_font_text_width
 * calls for frequently rendered static strings (e.g., button labels).
 *
 * Uses pointer comparison, so only works for string literals and static strings.
 *
 * @param text Text to measure (must be a stable pointer, not stack/heap)
 * @param font_size Font size in pixels
 * @return Text width in pixels
 */
int ui_text_width_cached(const char* text, int font_size);

/**
 * Clear text width cache
 *
 * Invalidates all cached entries. Call when fonts are reloaded or changed.
 */
void ui_text_cache_clear(void);

// ============================================================================
// Waking & Reconnect State
// ============================================================================

/**
 * Get waking start time (for timeout tracking)
 *
 * @return Microsecond timestamp when waking started, or 0 if not waking
 */
uint64_t ui_state_get_waking_start_time_us(void);

/**
 * Set waking start time
 *
 * @param time_us Microsecond timestamp
 */
void ui_state_set_waking_start_time_us(uint64_t time_us);

/**
 * Get waking wait for stream time
 *
 * @return Microsecond timestamp to wait until before starting stream
 */
uint64_t ui_state_get_waking_wait_for_stream_us(void);

/**
 * Set waking wait for stream time
 *
 * @param time_us Microsecond timestamp
 */
void ui_state_set_waking_wait_for_stream_us(uint64_t time_us);

/**
 * Get reconnect start time
 *
 * @return Time when reconnect flow started (microseconds)
 */
uint64_t ui_state_get_reconnect_start_time(void);

/**
 * Set reconnect start time
 *
 * @param time Time value in microseconds (typically sceKernelGetProcessTimeWide())
 */
void ui_state_set_reconnect_start_time(uint64_t time);

/**
 * Get reconnect animation frame
 *
 * @return Current animation frame for reconnect spinner
 */
int ui_state_get_reconnect_animation_frame(void);

/**
 * Set reconnect animation frame
 *
 * @param frame Animation frame value
 */
void ui_state_set_reconnect_animation_frame(int frame);
