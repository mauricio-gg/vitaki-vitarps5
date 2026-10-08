#include "context.h"
#include "config.h"
#include "host.h"
#include "host_registration.h"

#include <chiaki/base64.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void persist_config_or_warn(void) {
  if (!config_serialize(&context.config)) {
    LOGE("Failed to persist config changes");
  }
}

static ChiakiRegist regist = {};

/* Attempt phases. Only the UI thread starts, stops, reaps and fini()s the attempt; the lib
 * thread only records its event in regist_cb. So a stop is only ever issued while the phase is
 * RUNNING (lib has not yet reported, nothing has been fini'd) and fini can never race a stop. */
typedef enum {
  ATTEMPT_IDLE = 0,
  ATTEMPT_RUNNING,       // lib thread alive, no event yet
  ATTEMPT_LIB_DONE,      // regist_cb recorded the event; UI thread must fini and report
  ATTEMPT_RESULT_READY,  // result reported, waiting for host_registration_take_result()
} AttemptPhase;

typedef struct {
  AttemptPhase phase;
  VitaChiakiHost *host;
  char name[VITA_HOST_DISPLAY_NAME_LEN];  // snapshot taken at start, for the result
  uint64_t start_us;
  bool timeout_fired;
  bool user_cancelled;
  HostRegistrationLibEvent lib_event;
  bool stored_ok;
  HostRegistrationResult result;
} RegistrationAttempt;

static RegistrationAttempt attempt;
static ChiakiMutex attempt_mutex;
static bool attempt_mutex_ready = false;

static const char *lib_event_label(HostRegistrationLibEvent event) {
  switch (event) {
    case HOST_REGISTRATION_LIB_SUCCESS:
      return "success";
    case HOST_REGISTRATION_LIB_FAILED:
      return "failed";
    case HOST_REGISTRATION_LIB_CANCELED:
      return "canceled";
    default:
      return "unknown";
  }
}

static bool host_update_registered_state_from_event(VitaChiakiHost *host,
                                                    const ChiakiRegisteredHost *event_host) {
  if (!host || !event_host) {
    LOGE("Registration callback missing host data");
    return false;
  }

  if (!host->registered_state) {
    host->registered_state = malloc(sizeof(ChiakiRegisteredHost));
    if (!host->registered_state) {
      LOGE("Out of memory while storing registration state");
      return false;
    }
  }

  copy_host_registered_state(host->registered_state, event_host);
  memcpy(&host->server_mac, &event_host->server_mac, sizeof(host->server_mac));
  return true;
}

/* Stores a successful registration on the active host and persists the config.
 * Returns false when it could not be stored (the caller reports FAILED, not PAIRED). */
static bool store_registration(const ChiakiRegistEvent *event) {
  if (!context.active_host) {
    LOGE("Registration callback missing active host");
    return false;
  }
  context.active_host->type |= REGISTERED;

  if (!host_update_registered_state_from_event(context.active_host, event->registered_host))
    return false;

  bool updated_existing_host = false;
  for (int rhost_idx = 0; rhost_idx < context.config.num_registered_hosts; rhost_idx++) {
    VitaChiakiHost *rhost = context.config.registered_hosts[rhost_idx];
    if (!rhost)
      continue;
    if (mac_addrs_match(&(rhost->server_mac), &(context.active_host->server_mac))) {
      context.config.registered_hosts[rhost_idx] = context.active_host;
      updated_existing_host = true;
      break;
    }
  }

  if (!updated_existing_host) {
    if (context.config.num_registered_hosts >= MAX_REGISTERED_HOSTS) {
      LOGE("Max registered hosts reached; could not persist new registration.");
    } else {
      context.config.registered_hosts[context.config.num_registered_hosts++] = context.active_host;
    }
  }

  persist_config_or_warn();
  return true;
}

/* Runs on lib's registration thread, as the last thing it does. Records the event for the UI
 * thread, which does the stop-free cleanup (fini) and reports the result. */
static void regist_cb(ChiakiRegistEvent *event, void *user) {
  LOGD("regist event %d", event->type);
  HostRegistrationLibEvent lib_event = HOST_REGISTRATION_LIB_FAILED;
  bool stored_ok = false;

  if (event->type == CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS) {
    lib_event = HOST_REGISTRATION_LIB_SUCCESS;
    stored_ok = store_registration(event);
  } else if (event->type == CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED) {
    lib_event = HOST_REGISTRATION_LIB_CANCELED;
  }

  chiaki_mutex_lock(&attempt_mutex);
  attempt.lib_event = lib_event;
  attempt.stored_ok = stored_ok;
  attempt.phase = ATTEMPT_LIB_DONE;
  chiaki_mutex_unlock(&attempt_mutex);
}

/* Records the result of the attempt and logs it once. Caller must be the UI thread. */
static void report_result(HostRegistrationResult result, const char *lib_label) {
  chiaki_mutex_lock(&attempt_mutex);
  attempt.result = result;
  attempt.phase = ATTEMPT_RESULT_READY;
  chiaki_mutex_unlock(&attempt_mutex);
  LOGD("Pairing result %s for \"%s\" (lib event: %s)", host_registration_result_name(result),
       attempt.name, lib_label);
}

/* Begins an attempt record for @host. Returns false (with a warning) when one is still running. */
static bool begin_attempt(VitaChiakiHost *host) {
  if (!attempt_mutex_ready) {
    if (chiaki_mutex_init(&attempt_mutex, false) != CHIAKI_ERR_SUCCESS) {
      LOGE("Failed to create registration mutex; cannot register host.");
      return false;
    }
    attempt_mutex_ready = true;
  }
  if (host_registration_in_progress()) {
    LOGE("Pairing already in progress; refusing a second attempt.");
    return false;
  }

  chiaki_mutex_lock(&attempt_mutex);
  memset(&attempt, 0, sizeof(attempt));
  attempt.host = host;
  if (host) {
    const char *name = host->display_name[0] ? host->display_name : host->hostname;
    snprintf(attempt.name, sizeof(attempt.name), "%s", name);
  }
  chiaki_mutex_unlock(&attempt_mutex);
  return true;
}

void host_registration_poll(void) {
  if (!attempt_mutex_ready)
    return;

  chiaki_mutex_lock(&attempt_mutex);
  AttemptPhase phase = attempt.phase;
  if (phase == ATTEMPT_RUNNING && !attempt.timeout_fired && !attempt.user_cancelled &&
      sceKernelGetProcessTimeWide() - attempt.start_us >= HOST_REGISTRATION_TIMEOUT_US) {
    attempt.timeout_fired = true;
    chiaki_regist_stop(&regist);
    LOGD("Pairing for \"%s\" timed out after %d ms; stopping the attempt.", attempt.name,
         HOST_REGISTRATION_TIMEOUT_MS);
  }
  chiaki_mutex_unlock(&attempt_mutex);

  if (phase == ATTEMPT_LIB_DONE) {
    // regist_cb has returned from its locked section and lib's thread is exiting: joining is brief.
    chiaki_regist_fini(&regist);
    report_result(host_registration_resolve(attempt.lib_event, attempt.stored_ok,
                                            attempt.timeout_fired, attempt.user_cancelled),
                  lib_event_label(attempt.lib_event));
  }
}

void host_registration_cancel(void) {
  if (!attempt_mutex_ready)
    return;

  chiaki_mutex_lock(&attempt_mutex);
  if (attempt.phase == ATTEMPT_RUNNING && !attempt.user_cancelled && !attempt.timeout_fired) {
    attempt.user_cancelled = true;
    chiaki_regist_stop(&regist);
    LOGD("Pairing for \"%s\" cancelled by the user.", attempt.name);
  }
  chiaki_mutex_unlock(&attempt_mutex);
}

bool host_registration_in_progress(void) {
  if (!attempt_mutex_ready)
    return false;

  chiaki_mutex_lock(&attempt_mutex);
  bool running = attempt.phase == ATTEMPT_RUNNING || attempt.phase == ATTEMPT_LIB_DONE;
  chiaki_mutex_unlock(&attempt_mutex);
  return running;
}

bool host_registration_take_result(HostRegistrationResult *result, VitaChiakiHost **host,
                                   char *name, size_t name_size) {
  if (!attempt_mutex_ready || !result)
    return false;

  bool taken = false;
  chiaki_mutex_lock(&attempt_mutex);
  if (attempt.phase == ATTEMPT_RESULT_READY) {
    *result = attempt.result;
    if (host)
      *host = attempt.host;
    if (name && name_size > 0)
      snprintf(name, name_size, "%s", attempt.name);
    attempt.phase = ATTEMPT_IDLE;
    taken = true;
  }
  chiaki_mutex_unlock(&attempt_mutex);
  return taken;
}

/* Reports a result for an attempt that never reached lib. Returns 1 for host_register()'s caller.
 */
static int fail_before_start(HostRegistrationResult result, const char *reason) {
  LOGE("Pairing could not start: %s", reason);
  report_result(result, "not started");
  return 1;
}

int host_register(VitaChiakiHost *host, int pin) {
  if (!begin_attempt(host))
    return 1;
  if (!host)
    return fail_before_start(HOST_REGISTRATION_FAILED, "missing host");
  if (!host->hostname[0] || !host->discovery_state)
    return fail_before_start(HOST_REGISTRATION_UNREACHABLE,
                             "console has no hostname or is not on the network");
  if (!context.config.psn_account_id[0])
    return fail_before_start(HOST_REGISTRATION_FAILED, "missing PSN account id");

  ChiakiRegistInfo regist_info = {};
  regist_info.target = host->target;
  size_t account_id_size = sizeof(uint8_t[CHIAKI_PSN_ACCOUNT_ID_SIZE]);
  ChiakiErrorCode decode_err =
      chiaki_base64_decode(context.config.psn_account_id, strlen(context.config.psn_account_id),
                           regist_info.psn_account_id, &(account_id_size));
  if (decode_err != CHIAKI_ERR_SUCCESS || account_id_size != CHIAKI_PSN_ACCOUNT_ID_SIZE) {
    LOGE("Failed to decode PSN account id for registration: %s", chiaki_error_string(decode_err));
    return fail_before_start(HOST_REGISTRATION_FAILED, "undecodable PSN account id");
  }
  regist_info.psn_online_id = NULL;
  regist_info.pin = pin;
  regist_info.host = host->hostname;
  regist_info.broadcast = false;

  // Mark the attempt running before lib's thread can call regist_cb.
  chiaki_mutex_lock(&attempt_mutex);
  attempt.phase = ATTEMPT_RUNNING;
  attempt.start_us = sceKernelGetProcessTimeWide();
  chiaki_mutex_unlock(&attempt_mutex);

  ChiakiErrorCode start_err =
      chiaki_regist_start(&regist, &context.log, &regist_info, regist_cb, NULL);
  if (start_err != CHIAKI_ERR_SUCCESS) {
    LOGE("chiaki_regist_start failed: %s", chiaki_error_string(start_err));
    return fail_before_start(HOST_REGISTRATION_FAILED, "registration thread did not start");
  }
  return 0;
}

int host_wakeup(VitaChiakiHost *host) {
  if (!host) {
    LOGE("Missing host. Cannot send wakeup signal.");
    return 1;
  }
  if (!host->hostname[0]) {
    LOGE("Missing hostname. Cannot send wakeup signal.");
    return 1;
  }
  if (!host->registered_state) {
    LOGE("Missing registered host state for %s. Cannot send wakeup signal.", host->hostname);
    return 1;
  }
  if (!host->registered_state->rp_regist_key[0]) {
    LOGE("Missing registration credential for %s. Cannot send wakeup signal.", host->hostname);
    return 1;
  }

  char *parse_end = NULL;
  uint64_t credential = (uint64_t)strtoull(host->registered_state->rp_regist_key, &parse_end, 16);
  if (parse_end == host->registered_state->rp_regist_key || *parse_end != '\0') {
    LOGE("Invalid wake credential format for %s: \"%s\"", host->hostname,
         host->registered_state->rp_regist_key);
    return 1;
  }

  bool is_ps5 = chiaki_target_is_ps5(host->target);
  LOGD("Attempting wake signal to %s (target=%s, discovery_enabled=%d)", host->hostname,
       is_ps5 ? "PS5" : "PS4", context.discovery_enabled ? 1 : 0);

  ChiakiErrorCode wake_err = chiaki_discovery_wakeup(
      &context.log, context.discovery_enabled ? &context.discovery.discovery : NULL, host->hostname,
      credential, is_ps5);
  if (wake_err != CHIAKI_ERR_SUCCESS) {
    LOGE("Wake signal failed for %s: %s", host->hostname, chiaki_error_string(wake_err));
    return 1;
  }

  LOGD("Wake signal sent successfully to %s", host->hostname);
  return 0;
}
