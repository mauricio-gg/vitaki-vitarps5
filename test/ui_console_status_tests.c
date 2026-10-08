// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the console status rules in vita/src/ui/ui_console_status.c
// (issue #301): which messages show as Error or Retrying, and which status a row shows when a
// message is live. `./tools/build.sh test` only cross-compiles, so run these on the host:
//   cc -std=c99 -Wall -Wextra -I vita/include test/ui_console_status_tests.c \
//      vita/src/ui/ui_console_status.c -o /tmp/ui_console_status_tests && \
//      /tmp/ui_console_status_tests

#include <assert.h>
#include <stdio.h>

#include "ui/ui_console_status.h"

#define NOW_US 5000000ULL
#define LATER_US (NOW_US + 1000000ULL)
#define EARLIER_US (NOW_US - 1000000ULL)

static UiConsoleMessage live(const char *msg, bool is_error) {
  return ui_console_message_class(msg, is_error, LATER_US, NOW_US);
}

/** The messages flag 12 calls Retrying must not show red because the code flags them as errors
 * (for the popup), and the numbered and em-dash strings must still match. */
static void test_retrying_messages_ignore_the_error_flag(void) {
  assert(live("Rebuilding stream at safer bitrate", true) == UI_CONSOLE_MESSAGE_RETRYING);
  assert(live("Persistent video desync - rebuilding session", true) == UI_CONSOLE_MESSAGE_RETRYING);
  assert(live("Console busy - retrying in 12s...", false) == UI_CONSOLE_MESSAGE_RETRYING);
  assert(live("Console releasing session... ready in 3s", false) == UI_CONSOLE_MESSAGE_RETRYING);
  assert(live("Packet loss burst \xE2\x80\x94 requesting keyframe", false) ==
         UI_CONSOLE_MESSAGE_RETRYING);
  assert(live("Wake signal failed; attempting connection anyway.", false) ==
         UI_CONSOLE_MESSAGE_RETRYING);
}

/** The failures the user must act on stay Error, including the one whose first words match the
 * Retrying wake message. */
static void test_error_messages_stay_error(void) {
  assert(live("Wake signal failed. Check pairing and network.", true) == UI_CONSOLE_MESSAGE_ERROR);
  assert(live("Remote Play already active on console", true) == UI_CONSOLE_MESSAGE_ERROR);
  assert(live("Missing console credentials. Re-pair may be required.", true) ==
         UI_CONSOLE_MESSAGE_ERROR);
}

/** A message on neither list (PSN error text, "Could not reconnect") keeps the flag's meaning. */
static void test_unknown_message_keeps_its_flag(void) {
  assert(live("PSN internet remote play failed.", true) == UI_CONSOLE_MESSAGE_ERROR);
  assert(live("Something new", false) == UI_CONSOLE_MESSAGE_RETRYING);
}

/** An expired or cleared message must not keep the row red; 0 means it never expires. */
static void test_expired_or_empty_message_is_none(void) {
  assert(ui_console_message_class("Remote Play already active on console", true, EARLIER_US,
                                  NOW_US) == UI_CONSOLE_MESSAGE_NONE);
  assert(ui_console_message_class("", true, 0, NOW_US) == UI_CONSOLE_MESSAGE_NONE);
  assert(ui_console_message_class(NULL, true, 0, NOW_US) == UI_CONSOLE_MESSAGE_NONE);
  assert(ui_console_message_class("Remote Play already active on console", true, 0, NOW_US) ==
         UI_CONSOLE_MESSAGE_ERROR);
}

/** Cooldown wins over a message, a message wins over the base state, no message leaves the
 * base state alone. */
static void test_status_precedence(void) {
  assert(ui_console_classify(true, true, false, false, true, UI_CONSOLE_MESSAGE_ERROR).status ==
         UI_CONSOLE_COOLDOWN);
  assert(ui_console_classify(true, true, false, false, false, UI_CONSOLE_MESSAGE_ERROR).status ==
         UI_CONSOLE_ERROR);
  assert(ui_console_classify(false, false, false, false, false, UI_CONSOLE_MESSAGE_RETRYING)
             .status == UI_CONSOLE_RETRYING);
  assert(ui_console_classify(true, true, false, false, false, UI_CONSOLE_MESSAGE_NONE).status ==
         UI_CONSOLE_READY);
  assert(ui_console_classify(true, true, true, false, false, UI_CONSOLE_MESSAGE_NONE).status ==
         UI_CONSOLE_STANDBY);
}

int main(void) {
  test_retrying_messages_ignore_the_error_flag();
  test_error_messages_stay_error();
  test_unknown_message_keeps_its_flag();
  test_expired_or_empty_message_is_none();
  test_status_precedence();
  printf("ui_console_status_tests: all passed\n");
  return 0;
}
