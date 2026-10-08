/**
 * @file host_registration_result.c
 * @brief Pure decision of a pairing attempt's result
 */

#include "host_registration_result.h"

HostRegistrationResult host_registration_resolve(HostRegistrationLibEvent lib_event, bool stored_ok,
                                                 bool timeout_fired, bool user_cancelled) {
  switch (lib_event) {
    case HOST_REGISTRATION_LIB_SUCCESS:
      return stored_ok ? HOST_REGISTRATION_PAIRED : HOST_REGISTRATION_FAILED;
    case HOST_REGISTRATION_LIB_CANCELED:
      if (user_cancelled)
        return HOST_REGISTRATION_CANCELLED;
      if (timeout_fired)
        return HOST_REGISTRATION_TIMEOUT;
      return HOST_REGISTRATION_FAILED;
    case HOST_REGISTRATION_LIB_FAILED:
    default:
      return HOST_REGISTRATION_FAILED;
  }
}

const char *host_registration_result_name(HostRegistrationResult result) {
  switch (result) {
    case HOST_REGISTRATION_PAIRED:
      return "PAIRED";
    case HOST_REGISTRATION_FAILED:
      return "FAILED";
    case HOST_REGISTRATION_UNREACHABLE:
      return "UNREACHABLE";
    case HOST_REGISTRATION_TIMEOUT:
      return "TIMEOUT";
    case HOST_REGISTRATION_CANCELLED:
      return "CANCELLED";
    default:
      return "UNKNOWN";
  }
}
