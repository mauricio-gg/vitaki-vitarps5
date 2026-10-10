// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the console status rules in vita/src/ui/ui_console_status.c
// (issue #301): which messages show as Error or Retrying, and which status a row shows when a
// message is live. `./tools/build.sh test` only cross-compiles, so run these on the host:
//   cc -std=c99 -Wall -Wextra -I vita/include test/ui_console_status_tests.c \
//      vita/src/ui/ui_console_status.c vita/src/ip_address.c -o /tmp/ui_console_status_tests && \
//      /tmp/ui_console_status_tests

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/** catches: a recovery message the code flags as an error ("Rebuilding stream at safer bitrate")
 * opening a "Could not connect" dialog on every recovery, or a real failure ("Remote Play already
 * active on console") showing no dialog at all. */
static void test_only_error_class_hints_open_the_failure_popup(void) {
  assert(ui_console_hint_is_failure("Remote Play already active on console", true));
  assert(ui_console_hint_is_failure("Wake signal failed. Check pairing and network.", true));
  assert(ui_console_hint_is_failure("Missing console credentials. Re-pair may be required.", true));

  assert(!ui_console_hint_is_failure("Rebuilding stream at safer bitrate", true));
  assert(!ui_console_hint_is_failure("Persistent video desync - rebuilding session", true));
  assert(!ui_console_hint_is_failure("Console busy - retrying in 12s...", true));
  assert(!ui_console_hint_is_failure("Remote Play already active on console", false));
  assert(!ui_console_hint_is_failure("", true));
  assert(!ui_console_hint_is_failure(NULL, true));
}

static void check_words(const UiConnectionFacts *facts, const char *network, const char *status) {
  const UiConnectionWords words = ui_console_connection_words(facts);
  if (strcmp(words.network_type, network) != 0 || strcmp(words.status, status) != 0) {
    fprintf(stderr, "connection words: got \"%s\" / \"%s\", wanted \"%s\" / \"%s\"\n",
            words.network_type, words.status, network, status);
    abort();
  }
}

/** One check per row of the SPEC 3.7 Connection table.
 * catches: a sleeping or unreachable console shown as "Ready" on Profile (flag 4), "None" shown
 * for a console that is selected, or the Network Type precedence (discovered, then PSN, then
 * manual) coming out in the wrong order. */
static void test_connection_words_follow_the_spec_table(void) {
  const UiConnectionFacts lan = {.selected = true, .registered = true, .discovered = true};
  check_words(&lan, "Local Wi-Fi", "Ready");

  UiConnectionFacts standby = lan;
  standby.standby = true;
  check_words(&standby, "Local Wi-Fi", "Standby");

  UiConnectionFacts unpaired = lan;
  unpaired.registered = false;
  check_words(&unpaired, "Local Wi-Fi", "Unpaired");

  const UiConnectionFacts psn = {
      .selected = true, .registered = true, .psn_source = true, .internet_ok = true};
  check_words(&psn, "PSN Internet", "Ready");

  UiConnectionFacts psn_no_token = psn;
  psn_no_token.internet_ok = false;
  check_words(&psn_no_token, "PSN Internet", "Unavailable");

  const UiConnectionFacts manual = {.selected = true, .registered = true, .manual = true};
  check_words(&manual, "Manual Host", "Unavailable");

  const UiConnectionFacts unreachable = {.selected = true, .registered = true};
  check_words(&unreachable, "Unavailable", "Unavailable");

  const UiConnectionFacts none = {0};
  check_words(&none, "Unavailable", "None");
  check_words(NULL, "Unavailable", "None");

  UiConnectionFacts psn_on_lan = psn;
  psn_on_lan.discovered = true;
  psn_on_lan.manual = true;
  check_words(&psn_on_lan, "Local Wi-Fi", "Ready");

  UiConnectionFacts psn_manual = psn;
  psn_manual.manual = true;
  check_words(&psn_manual, "PSN Internet", "Ready");
}

/* catches: the pairing popup listing consoles in an order that changes between frames or jumps
 * around: names compare ignoring case, and two consoles with the same name sit in numeric IP
 * order (192.168.1.4 before 192.168.1.10, which a plain text compare gets wrong). */
static void test_discovered_order_is_name_then_numeric_ip(void) {
  assert(ui_console_discovered_order_before("bedroom", "192.168.1.9", "Den", "192.168.1.1"));
  assert(!ui_console_discovered_order_before("Den", "192.168.1.1", "bedroom", "192.168.1.9"));
  assert(ui_console_discovered_order_before("PS5", "192.168.1.4", "PS5", "192.168.1.10"));
  assert(!ui_console_discovered_order_before("PS5", "192.168.1.10", "PS5", "192.168.1.4"));
  assert(ui_console_discovered_order_before("ps5", "10.0.0.2", "PS5", "10.0.0.3"));
  assert(ui_console_discovered_order_before("PS5", "9.0.0.1", "PS5", "10.0.0.1"));
  /* An identical console is never before itself, so the sort stays stable. */
  assert(!ui_console_discovered_order_before("PS5", "10.0.0.2", "PS5", "10.0.0.2"));
  /* Text that is not a dotted address still has a fixed order. */
  assert(ui_console_discovered_order_before("PS5", "alpha", "PS5", "beta"));
}

int main(void) {
  test_retrying_messages_ignore_the_error_flag();
  test_error_messages_stay_error();
  test_unknown_message_keeps_its_flag();
  test_expired_or_empty_message_is_none();
  test_status_precedence();
  test_only_error_class_hints_open_the_failure_popup();
  test_connection_words_follow_the_spec_table();
  test_discovered_order_is_name_then_numeric_ip();
  printf("ui_console_status_tests: all passed\n");
  return 0;
}
