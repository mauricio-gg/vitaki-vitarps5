/**
 * @file ui_result_copy.h
 * @brief Pure copy for the C14 ResultPopup (SPEC.md section 3.3)
 *
 * No SDK dependency. Copy for pairing results and for a failed connection.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "host_registration_result.h"

#define UI_RESULT_MAX_BUTTONS 2

typedef enum ui_result_tone_t {
  UI_RESULT_TONE_OK = 0,
  UI_RESULT_TONE_ERR,
} UiResultTone;

/** Everything the popup shows except the body, which goes into the caller's buffer. */
typedef struct ui_result_copy_t {
  UiResultTone tone;
  const char *title;
  const char *buttons[UI_RESULT_MAX_BUTTONS];  ///< left to right
  int button_count;
  int primary_button;  ///< index of the primary button: the right-most
} UiResultCopy;

/**
 * ui_result_copy_pairing() - Popup copy for a pairing result.
 * @result:    The pairing result.
 * @name:      The console's name; NULL or empty reads as "The console".
 * @body:      Receives the body text, always terminated, truncated to fit.
 * @body_size: Size of @body in bytes.
 * @out:       Receives tone, title and buttons.
 *
 * Returns false (and writes nothing to @out) when the result has no popup (CANCELLED) or the
 * arguments are unusable; @body is then set to "" when it can be.
 */
bool ui_result_copy_pairing(HostRegistrationResult result, const char *name, char *body,
                            size_t body_size, UiResultCopy *out);

/**
 * ui_result_copy_connect_failed() - Popup copy for a connection that failed.
 * @name:      The console's name; NULL or empty reads as "The console".
 * @reason:    The raw reason the connection gave (SPEC 3.3); NULL or empty reads as "Connection
 *             failed".
 * @can_retry: The console is still in the list. When false only Close is offered.
 * @body:      Receives "<name>: <reason>", always terminated, truncated to fit.
 * @body_size: Size of @body in bytes.
 * @out:       Receives tone (ERR), title ("Could not connect") and buttons (Close, Try again).
 *
 * Returns false when the arguments are unusable; @body is then set to "" when it can be.
 */
bool ui_result_copy_connect_failed(const char *name, const char *reason, bool can_retry, char *body,
                                   size_t body_size, UiResultCopy *out);
