/**
 * @file ui_result_copy.c
 * @brief Pure copy for the C14 ResultPopup
 */

#include "ui/ui_result_copy.h"

#include <stdio.h>

#define UI_RESULT_FALLBACK_NAME "The console"
#define UI_RESULT_TITLE_PAIRED "Console paired"
#define UI_RESULT_TITLE_PAIR_FAILED "Pairing failed"
#define UI_RESULT_FALLBACK_REASON "Connection failed"
#define UI_RESULT_TITLE_CONNECT_FAILED "Could not connect"
#define UI_RESULT_BUTTON_OK "OK"
#define UI_RESULT_BUTTON_CLOSE "Close"
#define UI_RESULT_BUTTON_RETRY "Try again"

bool ui_result_copy_pairing(HostRegistrationResult result, const char *name, char *body,
                            size_t body_size, UiResultCopy *out) {
  if (!body || body_size == 0 || !out)
    return false;
  body[0] = '\0';

  const char *fmt = NULL;
  const char *title = UI_RESULT_TITLE_PAIR_FAILED;
  switch (result) {
    case HOST_REGISTRATION_PAIRED:
      title = UI_RESULT_TITLE_PAIRED;
      fmt = "%s is paired. You can connect to it now.";
      break;
    case HOST_REGISTRATION_TIMEOUT:
      fmt = "%s did not answer in time. Open Link Device on the console again and retry.";
      break;
    case HOST_REGISTRATION_UNREACHABLE:
      fmt = "%s could not be reached. Check that it is on and on the same network.";
      break;
    case HOST_REGISTRATION_FAILED:
      fmt =
          "%s did not accept the PIN or could not be reached. Check the code and that the "
          "console is on the same network.";
      break;
    case HOST_REGISTRATION_CANCELLED:
    default:
      return false;
  }

  if (!name || !name[0])
    name = UI_RESULT_FALLBACK_NAME;
  snprintf(body, body_size, fmt, name);

  out->title = title;
  if (result == HOST_REGISTRATION_PAIRED) {
    out->tone = UI_RESULT_TONE_OK;
    out->buttons[0] = UI_RESULT_BUTTON_OK;
    out->button_count = 1;
  } else {
    out->tone = UI_RESULT_TONE_ERR;
    out->buttons[0] = UI_RESULT_BUTTON_CLOSE;
    out->buttons[1] = UI_RESULT_BUTTON_RETRY;
    out->button_count = 2;
  }
  out->primary_button = out->button_count - 1;
  return true;
}

bool ui_result_copy_connect_failed(const char *name, const char *reason, bool can_retry, char *body,
                                   size_t body_size, UiResultCopy *out) {
  if (!body || body_size == 0 || !out)
    return false;

  if (!name || !name[0])
    name = UI_RESULT_FALLBACK_NAME;
  if (!reason || !reason[0])
    reason = UI_RESULT_FALLBACK_REASON;
  snprintf(body, body_size, "%s: %s", name, reason);

  out->title = UI_RESULT_TITLE_CONNECT_FAILED;
  out->tone = UI_RESULT_TONE_ERR;
  out->buttons[0] = UI_RESULT_BUTTON_CLOSE;
  out->button_count = 1;
  if (can_retry) {
    out->buttons[1] = UI_RESULT_BUTTON_RETRY;
    out->button_count = 2;
  }
  out->primary_button = out->button_count - 1;
  return true;
}
