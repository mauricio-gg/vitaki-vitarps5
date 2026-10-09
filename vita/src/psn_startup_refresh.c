// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

/* The startup PSN refresh off the UI thread (GH #366).
 *
 * Who writes what, and when:
 *   main thread, before the worker starts: decides whether a token refresh is needed and copies
 *     what the network calls need (the refresh request, or a copy of the access token).
 *   worker: the OAuth refresh POST (if needed), then the device-list fetch. It writes only the
 *     job fields marked "worker" below, never context, g_psn_auth, context.hosts or the config.
 *   main thread, after the worker is done: joins it, then applies the token, applies the device
 *     list to the hosts, saves the config.
 * The only field both threads touch while the worker runs is job.done, under job.mutex; the
 * main thread reads the worker's fields only after seeing done and joining the thread. */

#include "psn_startup_refresh.h"

#include <chiaki/common.h>
#include <chiaki/thread.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "context.h"
#include "psn_auth.h"
#include "psn_auth_rules.h"
#include "psn_remote.h"
#include "ui/ui_console_cards.h"
#include "ui/ui_state.h"

#if CHIAKI_CAN_USE_HOLEPUNCH

#define WORKER_NAME "PsnStartupRefresh"
/* Lower priority than the UI thread, which runs at the default (160): a higher number is a lower
 * priority on Vita. 176 keeps the worker from ever competing with the draw loop. */
#define WORKER_PRIORITY 176
/* VitaConnWorker runs the same curl/TLS calls (OAuth token, device list) on a 64 KiB stack and
 * is proven on hardware; this one is twice that for headroom: the refresh path adds a 1.4 KiB
 * request form and the TLS handshake in OpenSSL is the deepest call chain. */
#define WORKER_STACK_BYTES 0x20000
#define LABEL_MAX 64

typedef struct {
  bool running; /* a worker was started and has not been committed yet (main thread) */
  SceUID thread_id;
  ChiakiMutex mutex;
  bool done; /* guarded by mutex */

  /* Set by the main thread before the worker starts, read-only for the worker. */
  bool refresh_needed;
  PsnAuthRefreshRequest request;
  char *access_token; /* copy of the stored token, used when no refresh is needed */
  uint32_t grant_generation;
  uint64_t begin_us;

  /* Written by the worker only. */
  PsnAuthRefreshResult refresh_result;
  bool list_attempted;
  int list_failed;
  ChiakiHolepunchDeviceInfo *devices;
  size_t device_count;
  uint64_t worker_us;
} StartupJob;

static StartupJob s_job;

static uint64_t now_us(void) {
  return sceKernelGetProcessTimeWide();
}

/** save_config() - Write the config and clear the pending-persist flag, logging a failure. */
static void save_config(const char *why) {
  if (!config_serialize(&context.config))
    CHIAKI_LOGW(&(context.log), "PSN startup refresh: failed to persist config (%s)", why);
  context.config_persist_pending = false;
}

/** startup_worker() - Network half: token refresh if needed, then the device list. */
static int startup_worker(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  const uint64_t start_us = now_us();
  const char *token = s_job.access_token;
  if (s_job.refresh_needed) {
    psn_auth_refresh_fetch(&s_job.request, &s_job.refresh_result);
    token = psn_auth_refresh_result_access_token(&s_job.refresh_result);
  }
  if (token && token[0]) {
    s_job.list_attempted = true;
    s_job.list_failed = psn_remote_fetch_devices(token, &s_job.devices, &s_job.device_count);
  }
  s_job.worker_us = now_us() - start_us;

  chiaki_mutex_lock(&s_job.mutex);
  s_job.done = true;
  chiaki_mutex_unlock(&s_job.mutex);
  return 0;
}

/** job_release() - Free everything the job holds and mark it idle. Main thread only. */
static void job_release(void) {
  if (s_job.devices)
    chiaki_holepunch_free_device_list(&s_job.devices);
  psn_auth_refresh_result_free(&s_job.refresh_result);
  free(s_job.access_token);
  memset(&s_job, 0, sizeof(s_job));
  s_job.thread_id = -1;
}

/** start_worker() - Create and start the worker thread. Returns false (logged) on failure. */
static bool start_worker(void) {
  if (chiaki_mutex_init(&s_job.mutex, false) != CHIAKI_ERR_SUCCESS) {
    LOGE("PSN startup refresh: mutex init failed");
    return false;
  }
  s_job.thread_id = sceKernelCreateThread(WORKER_NAME, startup_worker, WORKER_PRIORITY,
                                          WORKER_STACK_BYTES, 0, 0, NULL);
  if (s_job.thread_id < 0) {
    LOGE("PSN startup refresh: failed to create worker thread (%d)", s_job.thread_id);
    chiaki_mutex_fini(&s_job.mutex);
    return false;
  }
  int status = sceKernelStartThread(s_job.thread_id, 0, NULL);
  if (status < 0) {
    LOGE("PSN startup refresh: failed to start worker thread (%d)", status);
    sceKernelDeleteThread(s_job.thread_id);
    chiaki_mutex_fini(&s_job.mutex);
    return false;
  }
  return true;
}

/** run_synchronously() - The fallback when no worker can be started: the old blocking path. */
static void run_synchronously(void) {
  psn_remote_refresh_hosts();
  /* Drain any token refresh that happened but didn't persist (e.g. host fetch failed after a
   * successful token refresh). */
  if (context.config_persist_pending)
    save_config("synchronous fallback");
}

void psn_startup_refresh_begin(void) {
  if (s_job.running)
    return;
  const uint64_t now_unix = (uint64_t)time(NULL);
  psn_remote_log_refresh_begin(now_unix);
  if (!psn_auth_enabled()) {
    LOGD("PSN host refresh skipped: PSN internet mode disabled");
    return;
  }

  memset(&s_job, 0, sizeof(s_job));
  s_job.thread_id = -1;
  switch (psn_auth_refresh_prepare(now_unix, false, true, &s_job.request)) {
    case PSN_AUTH_REFRESH_PREP_SKIPPED:
      LOGD("PSN host refresh skipped: OAuth token invalid and cannot be refreshed");
      return;
    case PSN_AUTH_REFRESH_PREP_VALID:
      s_job.access_token = strdup(psn_auth_access_token() ? psn_auth_access_token() : "");
      if (!s_job.access_token) {
        LOGE("PSN startup refresh: out of memory copying the access token");
        return;
      }
      break;
    case PSN_AUTH_REFRESH_PREP_READY:
      s_job.refresh_needed = true;
      break;
  }
  s_job.grant_generation = psn_auth_grant_generation();
  s_job.begin_us = now_us();

  if (!start_worker()) {
    LOGE("PSN startup refresh: falling back to a blocking refresh");
    psn_auth_refresh_background_finished();
    job_release();
    run_synchronously();
    return;
  }
  s_job.running = true;
  LOGD("PIPE/PSN_STARTUP_REFRESH begin us=%llu", (unsigned long long)s_job.begin_us);
}

/**
 * commit_hosts() - Apply the fetched device list, unless something changed meanwhile.
 *
 * @param label  receives "<added count>" or the reason nothing was applied
 * @return true when the host list was replaced
 */
static bool commit_hosts(char *label, size_t label_size) {
  if (!s_job.list_attempted || s_job.list_failed) {
    snprintf(label, label_size, "fetch_failed");
    return false;
  }
  if (!psn_auth_enabled()) {
    snprintf(label, label_size, "skipped_psn_disabled");
    LOGD("PSN host refresh skipped: PSN internet mode disabled");
    return false;
  }
  /* The user may have started a connect while the worker ran. */
  if (ui_state_connection_thread_active()) {
    snprintf(label, label_size, "deferred_connecting");
    LOGD("PSN host refresh deferred: connection thread active");
    return false;
  }
  int added = psn_remote_apply_devices(s_job.devices, s_job.device_count);
  snprintf(label, label_size, "%d", added);
  return true;
}

void psn_startup_refresh_poll(void) {
  if (!s_job.running)
    return;
  chiaki_mutex_lock(&s_job.mutex);
  const bool done = s_job.done;
  chiaki_mutex_unlock(&s_job.mutex);
  if (!done)
    return;

  const uint64_t commit_start_us = now_us();
  sceKernelWaitThreadEnd(s_job.thread_id, NULL, NULL);
  sceKernelDeleteThread(s_job.thread_id);
  chiaki_mutex_fini(&s_job.mutex);

  const uint64_t now_unix = (uint64_t)time(NULL);
  const char *token_label = "valid";
  char hosts_label[LABEL_MAX] = "not_attempted";
  bool hosts_applied = false;

  PsnAuthCommitVerdict verdict = psn_auth_rules_background_commit_verdict(
      psn_auth_state(now_unix), s_job.grant_generation, psn_auth_grant_generation());
  if (verdict != PSN_AUTH_COMMIT_APPLY) {
    const bool login = verdict == PSN_AUTH_COMMIT_DROP_LOGIN_STARTED;
    CHIAKI_LOGW(&(context.log), "PSN startup refresh: result dropped, %s while it was in flight",
                login ? "a phone login started" : "the PSN tokens were replaced or cleared");
    token_label = login ? "dropped_login" : "dropped_grant_changed";
    snprintf(hosts_label, sizeof(hosts_label), "dropped");
  } else {
    bool token_ok = true;
    if (s_job.refresh_needed) {
      switch (psn_auth_refresh_apply(&s_job.refresh_result, now_unix)) {
        case PSN_AUTH_REFRESH_REFRESHED:
          token_label = "refreshed";
          break;
        case PSN_AUTH_REFRESH_REJECTED:
          token_label = "rejected";
          token_ok = false;
          break;
        case PSN_AUTH_REFRESH_FAILED:
          token_label = "failed";
          token_ok = false;
          break;
      }
    }
    if (token_ok) {
      hosts_applied = commit_hosts(hosts_label, sizeof(hosts_label));
    } else {
      snprintf(hosts_label, sizeof(hosts_label), "skipped_token_%s", token_label);
      LOGD("PSN host refresh skipped: OAuth token invalid and refresh failed");
    }
    /* One save covers the new token and the new host list. */
    if (hosts_applied || context.config_persist_pending)
      save_config("commit");
    if (hosts_applied)
      ui_cards_update_cache(true);
  }

  psn_auth_refresh_background_finished();
  const uint64_t worker_us = s_job.worker_us;
  job_release();
  const uint64_t end_us = now_us();
  LOGD("PIPE/PSN_STARTUP_REFRESH done us=%llu worker_us=%llu commit_us=%llu token=%s hosts=%s",
       (unsigned long long)end_us, (unsigned long long)worker_us,
       (unsigned long long)(end_us - commit_start_us), token_label, hosts_label);
}

#else /* !CHIAKI_CAN_USE_HOLEPUNCH */

void psn_startup_refresh_begin(void) {
  psn_remote_refresh_hosts();
}

void psn_startup_refresh_poll(void) {}

#endif
