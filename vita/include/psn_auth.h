#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  PSN_AUTH_STATE_DISABLED = 0,
  PSN_AUTH_STATE_LOGGED_OUT,
  PSN_AUTH_STATE_TOKEN_VALID,
  PSN_AUTH_STATE_TOKEN_REFRESHING,
  PSN_AUTH_STATE_DEVICE_LOGIN_PENDING,
  PSN_AUTH_STATE_DEVICE_LOGIN_POLLING,
  PSN_AUTH_STATE_ERROR,
} PsnAuthState;

bool psn_auth_enabled(void);
bool psn_auth_has_tokens(void);
bool psn_auth_token_is_valid(uint64_t now_unix);
const char *psn_auth_access_token(void);
void psn_auth_clear_tokens(void);

PsnAuthState psn_auth_state(uint64_t now_unix);
const char *psn_auth_state_label(void);
const char *psn_auth_last_error(void);

bool psn_auth_begin_device_login(uint64_t now_unix);
void psn_auth_cancel_device_login(void);
bool psn_auth_poll_device_login(uint64_t now_unix);
bool psn_auth_device_login_active(void);
const char *psn_auth_device_user_code(void);
const char *psn_auth_device_verification_url(void);

bool psn_auth_refresh_token_if_needed(uint64_t now_unix, bool force);
