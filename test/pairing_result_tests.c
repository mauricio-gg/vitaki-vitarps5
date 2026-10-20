// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the pairing result rules in vita/src/host_registration_result.c
// and the result popup copy in vita/src/ui/ui_result_copy.c (issue #303).
// `./tools/build.sh test` only cross-compiles, so run these on the host:
//   cc -std=c99 -Wall -Wextra -I vita/include test/pairing_result_tests.c \
//      vita/src/host_registration_result.c vita/src/ui/ui_result_copy.c \
//      vita/src/psn_account_id.c lib/src/base64.c -I lib/include -o /tmp/pairing_result_tests && /tmp/pairing_result_tests

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "host_registration_result.h"
#include "psn_account_id.h"
#include "ui/ui_result_copy.h"

#define BODY_SIZE 256

/** lib reports CANCELED for both a user cancel and the Vita's own timeout stop. The reason must
 * pick the result: a timeout shown as a silent cancel or a cancel shown as a popup is wrong. */
static void test_lib_canceled_is_decided_by_why_it_was_stopped(void) {
  assert(host_registration_resolve(HOST_REGISTRATION_LIB_CANCELED, false, true, false) ==
         HOST_REGISTRATION_TIMEOUT);
  assert(host_registration_resolve(HOST_REGISTRATION_LIB_CANCELED, false, false, true) ==
         HOST_REGISTRATION_CANCELLED);
  // The user cancelled after the timeout already fired: the user's act is what they see.
  assert(host_registration_resolve(HOST_REGISTRATION_LIB_CANCELED, false, true, true) ==
         HOST_REGISTRATION_CANCELLED);
  // Nobody stopped it: not a cancel, must surface as a failure.
  assert(host_registration_resolve(HOST_REGISTRATION_LIB_CANCELED, false, false, false) ==
         HOST_REGISTRATION_FAILED);
}

/** "Console paired" must never show unless the pairing was actually stored; and a late stop
 * request must not turn a real outcome into a timeout. */
static void test_success_needs_storing_and_real_outcomes_win(void) {
  assert(host_registration_resolve(HOST_REGISTRATION_LIB_SUCCESS, true, false, false) ==
         HOST_REGISTRATION_PAIRED);
  assert(host_registration_resolve(HOST_REGISTRATION_LIB_SUCCESS, false, false, false) ==
         HOST_REGISTRATION_FAILED);
  assert(host_registration_resolve(HOST_REGISTRATION_LIB_SUCCESS, true, true, true) ==
         HOST_REGISTRATION_PAIRED);
  assert(host_registration_resolve(HOST_REGISTRATION_LIB_FAILED, false, true, false) ==
         HOST_REGISTRATION_FAILED);
}

/** Every failure result must tell the user which console, offer Close / Try again with Try again
 * primary, and say something different per cause (timeout is not "wrong PIN"). */
static void test_failure_popups(void) {
  const HostRegistrationResult failures[] = {HOST_REGISTRATION_FAILED, HOST_REGISTRATION_UNREACHABLE,
                                             HOST_REGISTRATION_TIMEOUT};
  char bodies[3][BODY_SIZE];
  for (int i = 0; i < 3; i++) {
    UiResultCopy copy;
    assert(ui_result_copy_pairing(failures[i], "PS5-Living", bodies[i], BODY_SIZE, &copy));
    assert(copy.tone == UI_RESULT_TONE_ERR);
    assert(copy.button_count == 2);
    assert(copy.primary_button == 1);
    assert(strcmp(copy.buttons[0], "Close") == 0);
    assert(strcmp(copy.buttons[1], "Try again") == 0);
    assert(strstr(bodies[i], "PS5-Living") != NULL);
    for (int j = 0; j < i; j++)
      assert(strcmp(bodies[i], bodies[j]) != 0);
  }
}

/** Success is the only OK popup with a single OK button; a user cancel shows no popup at all. */
static void test_paired_popup_and_cancel_has_none(void) {
  char body[BODY_SIZE];
  UiResultCopy copy;
  assert(ui_result_copy_pairing(HOST_REGISTRATION_PAIRED, "PS5-Living", body, BODY_SIZE, &copy));
  assert(copy.tone == UI_RESULT_TONE_OK);
  assert(copy.button_count == 1);
  assert(copy.primary_button == 0);
  assert(strstr(body, "PS5-Living") != NULL);

  assert(!ui_result_copy_pairing(HOST_REGISTRATION_CANCELLED, "PS5-Living", body, BODY_SIZE, &copy));
}

/** A very long console name or a small buffer must not write past the buffer. */
static void test_long_name_never_overflows_the_body(void) {
  char name[200];
  memset(name, 'A', sizeof(name) - 1);
  name[sizeof(name) - 1] = '\0';

  enum { SMALL = 64, GUARD = 8 };
  char area[SMALL + GUARD];
  memset(area, 0x5A, sizeof(area));
  UiResultCopy copy;
  assert(ui_result_copy_pairing(HOST_REGISTRATION_TIMEOUT, name, area, SMALL, &copy));
  assert(memchr(area, '\0', SMALL) != NULL);
  for (int i = SMALL; i < SMALL + GUARD; i++)
    assert(area[i] == 0x5A);
  assert(strlen(area) < SMALL);
}

/** catches: a long console name plus a long reason overflowing the popup body buffer, and a
 * console that left the list still offering Try again (which would connect to nothing). */
static void test_connect_failed_copy(void) {
  char name[100];
  char reason[120];
  memset(name, 'N', sizeof(name) - 1);
  name[sizeof(name) - 1] = '\0';
  memset(reason, 'R', sizeof(reason) - 1);
  reason[sizeof(reason) - 1] = '\0';

  enum { SMALL = 64, GUARD = 8 };
  char area[SMALL + GUARD];
  memset(area, 0x5A, sizeof(area));
  UiResultCopy copy;
  assert(ui_result_copy_connect_failed(name, reason, true, area, SMALL, &copy));
  assert(memchr(area, '\0', SMALL) != NULL);
  for (int i = SMALL; i < SMALL + GUARD; i++)
    assert(area[i] == 0x5A);

  char body[BODY_SIZE];
  assert(ui_result_copy_connect_failed("PS5-Living", "Console busy", true, body, BODY_SIZE, &copy));
  assert(strcmp(body, "PS5-Living: Console busy") == 0);
  assert(strcmp(copy.title, "Could not connect") == 0);
  assert(copy.tone == UI_RESULT_TONE_ERR);
  assert(copy.button_count == 2 && strcmp(copy.buttons[1], "Try again") == 0);
  assert(copy.primary_button == 1);

  assert(ui_result_copy_connect_failed("PS5-Living", "Console busy", false, body, BODY_SIZE, &copy));
  assert(copy.button_count == 1 && strcmp(copy.buttons[0], "Close") == 0);
  assert(copy.primary_button == 0);
}

/** catches: a Vita that is not signed in to PSN (all-zero ID) or a truncated ID being sent to the
 * console, which answers CE-110032-7. */
static void test_account_id_validity(void) {
  const uint8_t real[PSN_ACCOUNT_ID_BYTES] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};
  const uint8_t zeros[PSN_ACCOUNT_ID_BYTES] = {0};
  const uint8_t last_byte_only[PSN_ACCOUNT_ID_BYTES] = {0, 0, 0, 0, 0, 0, 0, 1};
  assert(psn_account_id_is_valid(real, sizeof(real)));
  assert(psn_account_id_is_valid(last_byte_only, sizeof(last_byte_only)));
  assert(!psn_account_id_is_valid(zeros, sizeof(zeros)));
  assert(!psn_account_id_is_valid(real, PSN_ACCOUNT_ID_BYTES - 1));
  assert(!psn_account_id_is_valid(real, PSN_ACCOUNT_ID_BYTES + 1));
  assert(!psn_account_id_is_valid(NULL, PSN_ACCOUNT_ID_BYTES));
}

/** catches: startup overwriting a hand-entered ID (#201) with an empty registry, ignoring a
 * newly signed-in account so a stale config.toml ID keeps failing, and a stored all-zero ID
 * (saved by older versions) being chosen as if it were valid. */
static void test_startup_choice(void) {
  // "EjRWeJq83vA=" is 12 34 56 78 9a bc de f0; "AAAAAAAAAAA=" is eight zero bytes.
  assert(psn_account_id_startup_choice("EjRWeJq83vA=", "AAAAAAAAAAA=") ==
         PSN_ACCOUNT_ID_USE_REGISTRY_NEW);
  assert(psn_account_id_startup_choice("EjRWeJq83vA=", NULL) == PSN_ACCOUNT_ID_USE_REGISTRY_NEW);
  assert(psn_account_id_startup_choice("EjRWeJq83vA=", "EjRWeJq83vA=") ==
         PSN_ACCOUNT_ID_USE_REGISTRY_SAME);
  assert(psn_account_id_startup_choice(NULL, "EjRWeJq83vA=") == PSN_ACCOUNT_ID_USE_STORED);
  assert(psn_account_id_startup_choice(NULL, NULL) == PSN_ACCOUNT_ID_USE_NONE);
  assert(psn_account_id_startup_choice(NULL, "") == PSN_ACCOUNT_ID_USE_NONE);
  // Stored values that are not a usable ID must not be reported as "from stored config".
  assert(psn_account_id_startup_choice(NULL, "AAAAAAAAAAA=") == PSN_ACCOUNT_ID_USE_NONE);
  assert(psn_account_id_startup_choice(NULL, "not base64!") == PSN_ACCOUNT_ID_USE_NONE);
  assert(psn_account_id_startup_choice(NULL, "EjRWeA==") == PSN_ACCOUNT_ID_USE_NONE);
}

/** catches: the missing-account result showing the generic "did not accept the PIN" text, which
 * sends the user to check a code that was never the problem. */
static void test_no_account_id_popup(void) {
  char body[BODY_SIZE];
  UiResultCopy copy;
  assert(ui_result_copy_pairing(HOST_REGISTRATION_NO_ACCOUNT_ID, "PS5-Living", body, BODY_SIZE,
                                &copy));
  assert(strstr(body, "Sign in to PSN on this Vita") != NULL);
  assert(strstr(body, "PIN") == NULL);
  assert(copy.tone == UI_RESULT_TONE_ERR);
  assert(copy.button_count == 2 && strcmp(copy.buttons[1], "Try again") == 0);
  assert(strcmp(host_registration_result_name(HOST_REGISTRATION_NO_ACCOUNT_ID), "NO_ACCOUNT_ID") ==
         0);
}

int main(void) {
  test_account_id_validity();
  test_startup_choice();
  test_no_account_id_popup();
  test_lib_canceled_is_decided_by_why_it_was_stopped();
  test_success_needs_storing_and_real_outcomes_win();
  test_failure_popups();
  test_paired_popup_and_cancel_has_none();
  test_long_name_never_overflows_the_body();
  test_connect_failed_copy();
  printf("pairing_result_tests: all passed\n");
  return 0;
}
