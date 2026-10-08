/**
 * @file host_registration.h
 * @brief Pairing attempt result API for the UI (SPEC.md section 3.3, flag 2)
 *
 * host_register() (declared in host.h) starts an attempt. Every attempt the UI starts ends in
 * exactly one HostRegistrationResult, including the pre-check failures that used to be silent.
 * All functions here are for the UI thread. The UI calls host_registration_poll() once per
 * frame while host_registration_in_progress(), and reads the outcome with
 * host_registration_take_result().
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "host.h"
#include "host_registration_result.h"

/**
 * host_registration_poll() - Advance a running attempt; call once per UI frame.
 *
 * Stops the attempt once HOST_REGISTRATION_TIMEOUT_MS have passed since it started, and
 * reports the result once lib has finished. Does nothing when no attempt is running.
 */
void host_registration_poll(void);

/** host_registration_cancel() - User cancel: stop a running attempt; its result is CANCELLED. */
void host_registration_cancel(void);

/** host_registration_in_progress() - True from a successful start until the result is reported. */
bool host_registration_in_progress(void);

/**
 * host_registration_take_result() - Collect the finished attempt's result, once.
 * @result:    Receives the result.
 * @host:      Receives the console it concerns (for "Try again"); may be NULL.
 * @name:      Receives a copy of the console's display name taken when the attempt finished;
 *             may be NULL.
 * @name_size: Size of @name; the copy is truncated and terminated.
 *
 * Returns true exactly once per finished attempt, false when there is nothing to collect.
 */
bool host_registration_take_result(HostRegistrationResult *result, VitaChiakiHost **host,
                                   char *name, size_t name_size);
