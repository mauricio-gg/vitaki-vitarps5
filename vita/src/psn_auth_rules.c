// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "psn_auth_rules.h"

#define HTTP_STATUS_BAD_REQUEST 400L

bool psn_auth_rules_login_in_progress(PsnAuthState state) {
  return state == PSN_AUTH_STATE_DEVICE_LOGIN_PENDING ||
         state == PSN_AUTH_STATE_DEVICE_LOGIN_POLLING;
}

bool psn_auth_rules_refresh_allowed(PsnAuthState state, bool refresh_token_rejected) {
  return !psn_auth_rules_login_in_progress(state) && !refresh_token_rejected;
}

PsnAuthState psn_auth_rules_state_after_error(PsnAuthState state) {
  return psn_auth_rules_login_in_progress(state) ? PSN_AUTH_STATE_DEVICE_LOGIN_PENDING
                                                 : PSN_AUTH_STATE_ERROR;
}

bool psn_auth_rules_status_rejects_refresh_token(long http_status) {
  return http_status == HTTP_STATUS_BAD_REQUEST;
}

PsnAuthCommitVerdict psn_auth_rules_background_commit_verdict(PsnAuthState state_now,
                                                              uint32_t generation_at_start,
                                                              uint32_t generation_now) {
  if (psn_auth_rules_login_in_progress(state_now))
    return PSN_AUTH_COMMIT_DROP_LOGIN_STARTED;
  if (generation_at_start != generation_now)
    return PSN_AUTH_COMMIT_DROP_GRANT_CHANGED;
  return PSN_AUTH_COMMIT_APPLY;
}
