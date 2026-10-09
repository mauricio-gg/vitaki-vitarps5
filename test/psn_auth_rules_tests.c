// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the PSN auth decision rules (GH #360): a background refresh must
// not end a pending phone login, an error must not end it either, and a refresh token Sony has
// rejected is not retried. `./tools/build.sh test` only cross-compiles for arm-vita-eabi and
// never executes, so run these natively:
//
//   cc -std=c99 -Wall -Wextra -I vita/include \
//      test/psn_auth_rules_tests.c vita/src/psn_auth_rules.c -o /tmp/psn_auth_rules_tests && \
//      /tmp/psn_auth_rules_tests

#include <assert.h>
#include <stdio.h>

#include "psn_auth_rules.h"

/* The once-a-minute idle refresh must leave a pending or polling login alone. */
static void test_refresh_refused_during_login(void) {
  assert(!psn_auth_rules_refresh_allowed(PSN_AUTH_STATE_DEVICE_LOGIN_PENDING, false));
  assert(!psn_auth_rules_refresh_allowed(PSN_AUTH_STATE_DEVICE_LOGIN_POLLING, false));
  assert(psn_auth_rules_refresh_allowed(PSN_AUTH_STATE_TOKEN_VALID, false));
  assert(psn_auth_rules_refresh_allowed(PSN_AUTH_STATE_LOGGED_OUT, false));
}

/* A bad paste or failed exchange must leave the login alive; outside a login it is an error. */
static void test_error_keeps_login_pending(void) {
  assert(psn_auth_rules_state_after_error(PSN_AUTH_STATE_DEVICE_LOGIN_PENDING) ==
         PSN_AUTH_STATE_DEVICE_LOGIN_PENDING);
  assert(psn_auth_rules_state_after_error(PSN_AUTH_STATE_DEVICE_LOGIN_POLLING) ==
         PSN_AUTH_STATE_DEVICE_LOGIN_PENDING);
  assert(psn_auth_rules_state_after_error(PSN_AUTH_STATE_TOKEN_REFRESHING) == PSN_AUTH_STATE_ERROR);
  assert(psn_auth_rules_state_after_error(PSN_AUTH_STATE_LOGGED_OUT) == PSN_AUTH_STATE_ERROR);
}

/* Sony's 400 marks the token rejected; from then on no state allows a refresh until re-login. */
static void test_rejected_refresh_token_not_retried(void) {
  assert(psn_auth_rules_status_rejects_refresh_token(400));
  assert(!psn_auth_rules_status_rejects_refresh_token(200));
  assert(!psn_auth_rules_status_rejects_refresh_token(500));
  assert(!psn_auth_rules_status_rejects_refresh_token(0));

  assert(!psn_auth_rules_refresh_allowed(PSN_AUTH_STATE_TOKEN_VALID, true));
  assert(!psn_auth_rules_refresh_allowed(PSN_AUTH_STATE_LOGGED_OUT, true));
  assert(!psn_auth_rules_refresh_allowed(PSN_AUTH_STATE_ERROR, true));
}

int main(void) {
  test_refresh_refused_during_login();
  test_error_keeps_login_pending();
  test_rejected_refresh_token_not_retried();
  printf("psn_auth_rules_tests: all passed\n");
  return 0;
}
