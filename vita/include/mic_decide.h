// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
/**
 * @file mic_decide.h
 * @brief What the mic capture thread does on one pass (GH #474)
 *
 * The one place that decides whether the microphone port is open and the console is told "mic
 * on". The rule that protects the player's privacy is here, with no SDK types, so it is checked
 * natively (test/mic_decide_tests.c): while the mic is muted the answer is never "open", whatever
 * else is true. Only a change to unmuted can lead to an open.
 */

#pragma once

#include <stdbool.h>

/** The thread's view of one pass. */
typedef struct mic_decide_in_t {
  bool muted;          /**< The player's mute, read once for this pass. */
  bool stop_requested; /**< The stream is going away. */
  bool sending;        /**< The port is open and the console was told "mic on". */
  bool connected;      /**< chiaki_session_connect_microphone() already succeeded. */
} MicDecideIn;

typedef enum mic_action_t {
  MIC_ACTION_NONE,
  MIC_ACTION_OPEN,  /**< Open the port and tell the console "mic on". */
  MIC_ACTION_CLOSE, /**< Close the port; tell the console "mic off" when @announce is set. */
} MicAction;

typedef struct mic_decision_t {
  MicAction action;
  bool connect_first; /**< OPEN only: connect the microphone to the session before announcing. */
  bool announce;      /**< CLOSE only: send "mic off" (not on a stop, the session is going away). */
} MicDecision;

/** Decides this pass. OPEN only when !muted && !stop_requested && !sending. */
MicDecision mic_decide(const MicDecideIn *in);
