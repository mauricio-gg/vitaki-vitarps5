/**
 * @file host_registration_result.h
 * @brief The outcome of a pairing attempt, and how it is decided (SPEC.md section 3.3, flag 2)
 *
 * No SDK or lib dependency: the rule that turns what lib reported plus what the Vita did into
 * the one result the UI shows lives here so it can be checked natively.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** How long a pairing attempt may run before the Vita stops it and reports a timeout. */
#define HOST_REGISTRATION_TIMEOUT_MS 30000
#define HOST_REGISTRATION_TIMEOUT_US ((uint64_t)HOST_REGISTRATION_TIMEOUT_MS * 1000ULL)

/** The one result reported per pairing attempt. */
typedef enum host_registration_result_t {
  HOST_REGISTRATION_PAIRED = 0,   ///< registered state stored and persisted
  HOST_REGISTRATION_FAILED,       ///< PIN not accepted or console unreachable (lib cannot tell)
  HOST_REGISTRATION_UNREACHABLE,  ///< console not on the network; lib was never called
  HOST_REGISTRATION_TIMEOUT,      ///< no answer within HOST_REGISTRATION_TIMEOUT_MS
  HOST_REGISTRATION_CANCELLED,    ///< the user cancelled; no popup
} HostRegistrationResult;

/** What lib reported when it finished (mirrors ChiakiRegistEventType without including lib). */
typedef enum host_registration_lib_event_t {
  HOST_REGISTRATION_LIB_CANCELED = 0,
  HOST_REGISTRATION_LIB_FAILED,
  HOST_REGISTRATION_LIB_SUCCESS,
} HostRegistrationLibEvent;

/**
 * host_registration_resolve() - Decide the result of a finished attempt.
 * @lib_event:      What lib reported.
 * @stored_ok:      On lib success: the registered state was stored and persisted.
 * @timeout_fired:  The Vita stopped the attempt because the timeout passed.
 * @user_cancelled: The user asked to cancel the attempt.
 *
 * Success counts as PAIRED only when it was stored; otherwise FAILED. A lib failure is FAILED.
 * A lib CANCELED exists only because the Vita stopped the attempt, so the reason decides:
 * user cancel is CANCELLED, timeout is TIMEOUT. A CANCELED with no reason is unexpected and
 * reports FAILED rather than being hidden as a user cancel.
 */
HostRegistrationResult host_registration_resolve(HostRegistrationLibEvent lib_event, bool stored_ok,
                                                 bool timeout_fired, bool user_cancelled);

/** host_registration_result_name() - The result's name for logs ("PAIRED", "TIMEOUT", ...). */
const char *host_registration_result_name(HostRegistrationResult result);
