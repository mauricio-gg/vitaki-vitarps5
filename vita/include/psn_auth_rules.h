/**
 * @file psn_auth_rules.h
 * @brief Pure decision rules for the PSN auth state machine (GH #360)
 *
 * No dependency on context, curl or psn_auth's globals, so the rules can be checked natively.
 * psn_auth.c owns the state and calls these to decide what is allowed.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "psn_auth.h"

/**
 * True while a phone login is under way (waiting for the code, or exchanging it).
 * Only a cancel, a successful exchange or the login's own expiry may end it.
 */
bool psn_auth_rules_login_in_progress(PsnAuthState state);

/**
 * Whether the stored refresh token may be sent to Sony now.
 *
 * @param state                  current auth state
 * @param refresh_token_rejected true once Sony answered the stored token with a rejection
 * @return false during a phone login (a refresh would clobber it) or after a rejection
 */
bool psn_auth_rules_refresh_allowed(PsnAuthState state, bool refresh_token_rejected);

/**
 * The state to leave behind when an error is recorded.
 *
 * @return PENDING while a login is in progress (it stays alive and still accepts a code),
 *         ERROR otherwise
 */
PsnAuthState psn_auth_rules_state_after_error(PsnAuthState state);

/**
 * Whether an HTTP status from the refresh request means Sony rejected the stored refresh
 * token (HTTP 400, invalid_grant), as opposed to a transient failure worth retrying.
 */
bool psn_auth_rules_status_rejects_refresh_token(long http_status);

/** What to do with the result of a refresh that ran on a worker thread (GH #366). */
typedef enum {
  PSN_AUTH_COMMIT_APPLY = 0,
  /** A phone login started while the worker ran; applying would clobber it. */
  PSN_AUTH_COMMIT_DROP_LOGIN_STARTED,
  /** The tokens were replaced or cleared (login finished, log out) while the worker ran; the
   *  result belongs to a grant that is gone. */
  PSN_AUTH_COMMIT_DROP_GRANT_CHANGED,
} PsnAuthCommitVerdict;

/**
 * Whether a worker's refresh result may still be applied on the main thread.
 *
 * @param state_now             auth state at commit time
 * @param generation_at_start   grant generation when the worker was started
 * @param generation_now        grant generation at commit time
 * @return PSN_AUTH_COMMIT_APPLY only if no login is open and the grant is unchanged
 */
PsnAuthCommitVerdict psn_auth_rules_background_commit_verdict(PsnAuthState state_now,
                                                              uint32_t generation_at_start,
                                                              uint32_t generation_now);
