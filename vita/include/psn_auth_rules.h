/**
 * @file psn_auth_rules.h
 * @brief Pure decision rules for the PSN auth state machine (GH #360)
 *
 * No dependency on context, curl or psn_auth's globals, so the rules can be checked natively.
 * psn_auth.c owns the state and calls these to decide what is allowed.
 */

#pragma once

#include <stdbool.h>

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
