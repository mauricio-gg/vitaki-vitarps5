// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

/* Memory-card writer for the config file (ticket #353). See config_writer.h.
 *
 * The only state shared between callers and the writer thread is the slot below, under s_mutex.
 * The thread is created on the first save and lives until the process exits. */

#include "config_writer.h"

#include <chiaki/thread.h>
#include <stdio.h>
#include <stdlib.h>

#include "context.h"
#include "ui/ui_draw_stats.h"

#ifdef __PSVITA__
#include <psp2/kernel/threadmgr.h>
#endif

/* Lower priority than the UI thread, which runs at the default (160): a higher number is a lower
 * priority on Vita. Same value and reasoning as the PSN refresh worker. */
#define CONFIG_WRITER_PRIORITY 176

typedef enum {
  WRITER_NOT_STARTED,
  WRITER_RUNNING,
  WRITER_FAILED, /* thread creation failed: every save is written on the caller's thread */
} WriterState;

typedef struct {
  char *data; /* NULL when nothing is pending */
  size_t len;
  uint64_t seq;       /* submission number of the save in this slot */
  uint64_t format_us; /* caller-side formatting time, for the log line */
  unsigned coalesced; /* queued saves this one replaced */
} PendingSave;

static const char *s_path; /* config file path, set by config_writer_init() */
static bool s_inited;
static ChiakiMutex s_mutex;
static ChiakiCond s_work_cond; /* the writer waits here for a pending save */
static ChiakiCond s_done_cond; /* callers wait here for a finished write */
static ChiakiThread s_thread;
static WriterState s_state = WRITER_NOT_STARTED;

/* All below guarded by s_mutex. */
static PendingSave s_pending;
static bool s_writing;
static uint64_t s_next_seq;
static uint64_t s_done_seq; /* highest submission number covered by a finished write */
static bool s_last_ok;      /* result of the write that set s_done_seq */

/** write_file() - Writes @p data to the config file with one open/write/close. */
static bool write_file(const char *data, size_t len) {
  FILE *fp = fopen(s_path, "w");
  if (!fp) {
    LOGE("Failed to open %s for writing", s_path);
    return false;
  }
  const bool wrote = fwrite(data, 1, len, fp) == len;
  const bool closed = fclose(fp) == 0;
  if (!wrote || !closed) {
    LOGE("Failed to flush %s", s_path);
    return false;
  }
  return true;
}

/** writer_main() - Thread body: sleeps until a save is pending, writes it, reports the result. */
static void *writer_main(void *arg) {
  (void)arg;
#ifdef __PSVITA__
  sceKernelChangeThreadPriority(SCE_KERNEL_THREAD_ID_SELF, CONFIG_WRITER_PRIORITY);
#endif
  chiaki_mutex_lock(&s_mutex);
  for (;;) {
    while (!s_pending.data)
      chiaki_cond_wait(&s_work_cond, &s_mutex);
    PendingSave job = s_pending;
    s_pending.data = NULL;
    s_writing = true;
    chiaki_mutex_unlock(&s_mutex);

#if VITARPS5_DEBUG_TOOLS
    const uint64_t start_us = ui_draw_stats_now_us();
#endif
    const bool ok = write_file(job.data, job.len);
#if VITARPS5_DEBUG_TOOLS
    LOGD("PIPE/CONFIG_SAVE bytes=%lu format_us=%llu write_us=%llu coalesced=%u ok=%d",
         (unsigned long)job.len, (unsigned long long)job.format_us,
         (unsigned long long)(ui_draw_stats_now_us() - start_us), job.coalesced, ok ? 1 : 0);
#endif
    free(job.data);

    chiaki_mutex_lock(&s_mutex);
    s_writing = false;
    s_done_seq = job.seq;
    s_last_ok = ok;
    chiaki_cond_broadcast(&s_done_cond);
  }
  return NULL;
}

void config_writer_init(const char *path) {
  if (s_inited)
    return;
  s_path = path;
  if (chiaki_mutex_init(&s_mutex, false) != CHIAKI_ERR_SUCCESS) {
    LOGE("Config writer: could not create its lock; saving on the caller's thread");
    return;
  }
  if (chiaki_cond_init(&s_work_cond, &s_mutex) != CHIAKI_ERR_SUCCESS ||
      chiaki_cond_init(&s_done_cond, &s_mutex) != CHIAKI_ERR_SUCCESS) {
    LOGE("Config writer: could not create its condition; saves will run on the caller's thread");
    return;
  }
  s_inited = true;
}

/** start_thread_locked() - Creates the writer thread on first use. Call with s_mutex held. */
static void start_thread_locked(void) {
  if (s_state != WRITER_NOT_STARTED)
    return;
  if (chiaki_thread_create(&s_thread, writer_main, NULL) == CHIAKI_ERR_SUCCESS) {
    s_state = WRITER_RUNNING;
  } else {
    s_state = WRITER_FAILED;
    LOGE("Config writer: could not create its thread; saving on the caller's thread instead");
  }
}

bool config_writer_submit(char *data, size_t len, uint64_t format_us, bool wait) {
  if (!data)
    return false;
  if (!s_path) {
    LOGE("Config writer used before config_writer_init(); config not saved");
    free(data);
    return false;
  }
  if (!s_inited) {
    const bool ok = write_file(data, len);
    free(data);
    return ok;
  }

  chiaki_mutex_lock(&s_mutex);
  start_thread_locked();
  if (s_state != WRITER_RUNNING) {
    chiaki_mutex_unlock(&s_mutex);
    const bool ok = write_file(data, len);
    free(data);
    return ok;
  }

  const uint64_t seq = ++s_next_seq;
  unsigned coalesced = 0;
  if (s_pending.data) {
    /* An older save is still waiting: this one replaces it, so it is never written. */
    free(s_pending.data);
    coalesced = s_pending.coalesced + 1;
  }
  s_pending = (PendingSave){data, len, seq, format_us, coalesced};
  chiaki_cond_signal(&s_work_cond);

  bool ok = true;
  if (wait) {
    while (s_done_seq < seq)
      chiaki_cond_wait(&s_done_cond, &s_mutex);
    ok = s_last_ok;
  }
  chiaki_mutex_unlock(&s_mutex);
  return ok;
}

void config_writer_flush(void) {
  if (!s_inited)
    return;
  chiaki_mutex_lock(&s_mutex);
  while (s_pending.data || s_writing)
    chiaki_cond_wait(&s_done_cond, &s_mutex);
  chiaki_mutex_unlock(&s_mutex);
}
