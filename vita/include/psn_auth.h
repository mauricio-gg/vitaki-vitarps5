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
const char *psn_auth_state_label_for(PsnAuthState state, uint64_t now_unix);
const char *psn_auth_last_error(void);

bool psn_auth_begin_device_login(uint64_t now_unix);
void psn_auth_cancel_device_login(void);
bool psn_auth_poll_device_login(uint64_t now_unix);
bool psn_auth_submit_authorization_response(const char *input, uint64_t now_unix);
bool psn_auth_device_login_active(void);
const char *psn_auth_device_user_code(void);
const char *psn_auth_device_verification_url(void);

bool psn_auth_refresh_token_if_needed(uint64_t now_unix, bool force);

/**
 * psn_auth_on_network_change
 *
 * Forces an immediate PSN token refresh after the Vita transitions to a new
 * network connection (WiFi reconnect, network switch).
 *
 * Call this when sceNetCtlGetState() transitions to SCE_NETCTL_STATE_CONNECTED
 * (3) from any non-connected state.  Without this, the 60-second idle poller
 * may fire too late if the user immediately attempts a connection after
 * changing networks, causing the stale access token to be rejected by PSN.
 *
 * Safe to call when PSN is disabled or no tokens are stored (no-op).
 *
 * @param now_unix  Current Unix timestamp from time(NULL).
 * @return true if a refresh was attempted and succeeded, false otherwise.
 */
bool psn_auth_on_network_change(uint64_t now_unix);

/**
 * psn_auth_token_seconds_remaining
 *
 * Returns the number of seconds until the current access token expires.
 * Used by the UI to display a countdown badge on the Connection card.
 *
 * @param now_unix  Current Unix timestamp from time(NULL).
 * @return Seconds until expiry (>= 0), or -1 if no token / expiry unknown.
 *         0 means the token is already expired.
 */
int64_t psn_auth_token_seconds_remaining(uint64_t now_unix);
