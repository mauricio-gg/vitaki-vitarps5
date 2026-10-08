// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the pairing result rules in vita/src/host_registration_result.c
// and the result popup copy in vita/src/ui/ui_result_copy.c (issue #303).
// `./tools/build.sh test` only cross-compiles, so run these on the host:
//   cc -std=c99 -Wall -Wextra -I vita/include test/pairing_result_tests.c \
//      vita/src/host_registration_result.c vita/src/ui/ui_result_copy.c \
//      -o /tmp/pairing_result_tests && /tmp/pairing_result_tests

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "host_registration_result.h"
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

int main(void) {
  test_lib_canceled_is_decided_by_why_it_was_stopped();
  test_success_needs_storing_and_real_outcomes_win();
  test_failure_popups();
  test_paired_popup_and_cancel_has_none();
  test_long_name_never_overflows_the_body();
  printf("pairing_result_tests: all passed\n");
  return 0;
}
