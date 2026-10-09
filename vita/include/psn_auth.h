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

/*
 * The token refresh split in two so the network half can run on a worker thread (GH #366).
 * psn_auth_refresh_token_if_needed() runs them in sequence on the calling thread. Only the
 * main thread may call prepare, apply and the grant-generation getter; fetch touches nothing but
 * the request it is given and the result it fills.
 */

#define PSN_AUTH_REFRESH_URL_MAX 256
#define PSN_AUTH_REFRESH_CRED_MAX 128
#define PSN_AUTH_REFRESH_FORM_MAX 1400

/** Everything the refresh POST needs, copied from shared state on the main thread. */
typedef struct {
  char url[PSN_AUTH_REFRESH_URL_MAX];
  char client_id[PSN_AUTH_REFRESH_CRED_MAX];
  char client_secret[PSN_AUTH_REFRESH_CRED_MAX];
  char form[PSN_AUTH_REFRESH_FORM_MAX]; /* holds the refresh token; never log it */
} PsnAuthRefreshRequest;

/** What Sony answered. The tokens are parsed here, but only apply stores them. */
typedef struct {
  bool transport_ok; /* the request completed and a body came back */
  long http_code;
  char *response;      /* raw body, for the error fields only; never log it */
  bool grant_ok;       /* HTTP 200 with a usable access_token */
  char *access_token;  /* set when grant_ok */
  char *refresh_token; /* set when grant_ok and Sony sent a new one */
  uint64_t expires_in;
} PsnAuthRefreshResult;

typedef enum {
  PSN_AUTH_REFRESH_PREP_VALID,   /* token still valid: nothing to send */
  PSN_AUTH_REFRESH_PREP_SKIPPED, /* nothing may or can be sent; logged or recorded as an error */
  PSN_AUTH_REFRESH_PREP_READY,   /* request filled in; state is now TOKEN_REFRESHING */
} PsnAuthRefreshPrep;

typedef enum {
  PSN_AUTH_REFRESH_REFRESHED,
  PSN_AUTH_REFRESH_FAILED,
  PSN_AUTH_REFRESH_REJECTED, /* Sony answered 400: the stored refresh token is marked rejected */
} PsnAuthRefreshOutcome;

/**
 * Decide whether a refresh is needed and build the request. Applies the #360 rules.
 *
 * @param background true for the startup worker: marks a refresh as in flight (until
 *                   psn_auth_refresh_background_finished()) so synchronous callers do not send a
 *                   second one with the same refresh token. A synchronous call made while one is
 *                   in flight is SKIPPED.
 */
PsnAuthRefreshPrep psn_auth_refresh_prepare(uint64_t now_unix, bool force, bool background,
                                            PsnAuthRefreshRequest *req);

/** The network half. Thread-safe: no shared state is read or written. */
void psn_auth_refresh_fetch(const PsnAuthRefreshRequest *req, PsnAuthRefreshResult *res);

/** The access token of a successful fetch, or NULL. */
const char *psn_auth_refresh_result_access_token(const PsnAuthRefreshResult *res);

/** Store the result (tokens, state, error, rejected flag). Main thread only. */
PsnAuthRefreshOutcome psn_auth_refresh_apply(const PsnAuthRefreshResult *res, uint64_t now_unix);

void psn_auth_refresh_result_free(PsnAuthRefreshResult *res);

/** The background refresh has been committed or dropped; synchronous refreshes may run again. */
void psn_auth_refresh_background_finished(void);

/** Changes whenever the stored grant is replaced or cleared (code exchange, log out). */
uint32_t psn_auth_grant_generation(void);
