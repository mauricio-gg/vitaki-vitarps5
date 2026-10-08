/**
 * @file ui_connect_failure.c
 * @brief Thread-safe slot for the pending connection failure (see ui_connect_failure.h)
 */

#include "ui/ui_connect_failure.h"

#include <stdio.h>
#include <psp2/kernel/threadmgr.h>

#include "context.h"

static SceKernelLwMutexWork s_lock;
static bool s_lock_ready = false;
static UiConnectFailure s_slot;
static bool s_pending = false;

/** Take the lock; false (and an error log) when init never ran, so nobody touches the slot raw. */
static bool lock(void) {
  if (!s_lock_ready) {
    LOGE("Connect failure hand-off used before ui_connect_failure_init()");
    return false;
  }
  sceKernelLockLwMutex(&s_lock, 1, NULL);
  return true;
}

static void unlock(void) {
  sceKernelUnlockLwMutex(&s_lock, 1);
}

void ui_connect_failure_init(void) {
  int res = sceKernelCreateLwMutex(&s_lock, "UiConnectFailure", 0, 0, NULL);
  if (res < 0) {
    LOGE("Connect failure hand-off: could not create its lock (0x%08x)", (unsigned)res);
    return;
  }
  s_pending = false;
  s_lock_ready = true;
}

void ui_connect_failure_post(VitaChiakiHost *host, const char *reason) {
  if (!host || !reason || !reason[0] || !lock())
    return;
  s_slot.host = host;
  snprintf(s_slot.name, sizeof(s_slot.name), "%s",
           host->display_name[0] ? host->display_name : host->hostname);
  snprintf(s_slot.reason, sizeof(s_slot.reason), "%s", reason);
  s_pending = true;
  unlock();
}

void ui_connect_failure_clear(const VitaChiakiHost *host) {
  if (!host || !lock())
    return;
  if (s_pending && s_slot.host == host)
    s_pending = false;
  unlock();
}

bool ui_connect_failure_take(UiConnectFailure *out) {
  if (!lock())
    return false;
  const bool had = s_pending;
  if (had) {
    *out = s_slot;
    s_pending = false;
  }
  unlock();
  return had;
}
