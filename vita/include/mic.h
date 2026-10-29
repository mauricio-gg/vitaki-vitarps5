// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
/**
 * @file mic.h
 * @brief GH #467: hardware probe that sends the Vita microphone to the console.
 *
 * Testing builds only (VITARPS5_DEBUG_TOOLS=1); in a release build every call is an empty stub.
 * The player turns the mic on and off from the debug bar (touch) or L + R + Triangle held
 * (controller). The first "on" of a stream starts a capture thread that opens sceAudioIn, builds
 * 480-sample frames, encodes them with the core's Opus encoder and sends them through the
 * session. Every line it logs starts with "MIC/".
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "debug_tools.h"

#if VITARPS5_DEBUG_TOOLS

/** L + R + Triangle held this long toggles the mic. */
#define MIC_COMBO_HOLD_MS DEBUG_PANEL_COMBO_HOLD_MS

/** UI thread: the player asked for a toggle. @p source is DEBUG_PANEL_REQUEST_TOUCH or _COMBO. */
void vitamic_request_toggle(uint8_t source);

/** True while the mic is requested on (what the button shows). */
bool vitamic_is_on(void);

/** Stops and joins the capture thread and frees the encoder, then leaves the mic off for the next
 * stream. Idempotent; call before chiaki_session_fini(). */
void vitamic_stop(void);

#else

static inline bool vitamic_is_on(void) {
  return false;
}
static inline void vitamic_stop(void) {}

#endif
