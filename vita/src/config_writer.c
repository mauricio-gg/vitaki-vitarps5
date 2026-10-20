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

#include <errno.h>
#include <string.h>

#ifdef __PSVITA__
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>
#endif

/* Lower priority than the UI thread, which runs at the default (160): a higher number is a lower
 * priority on Vita. Same value and reasoning as the PSN refresh worker. */
#define CONFIG_WRITER_PRIORITY 176

/* Longest config path the replace helper can build a temp or backup name for. */
#define CONFIG_PATH_MAX 256

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

/**
 * fs_rename() - Renames @p from to @p to. Fails when @p to already exists on the Vita.
 *
 * On the Vita this calls sceIoRename directly so the log carries the real SCE error code; the host
 * test build uses rename().
 *
 * @return 0 on success, otherwise the error code (SCE code on the Vita, errno on the host)
 */
static int fs_rename(const char *from, const char *to) {
#ifdef __PSVITA__
  const int ret = sceIoRename(from, to);
  return ret < 0 ? ret : 0;
#else
  return rename(from, to) == 0 ? 0 : errno;
#endif
}

/** fs_remove() - Deletes @p path. Same return convention as fs_rename(). */
static int fs_remove(const char *path) {
#ifdef __PSVITA__
  const int ret = sceIoRemove(path);
  return ret < 0 ? ret : 0;
#else
  return remove(path) == 0 ? 0 : errno;
#endif
}

/** file_exists() - True when @p path can be opened for reading. */
static bool file_exists(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp)
    return false;
  fclose(fp);
  return true;
}

/** build_sibling_path() - Writes @p path plus @p suffix into @p out; false when it does not fit. */
static bool build_sibling_path(char *out, size_t cap, const char *path, const char *suffix) {
  const int n = snprintf(out, cap, "%s%s", path, suffix);
  return n > 0 && (size_t)n < cap;
}

/** write_whole_file() - Writes @p data to @p path and closes it, checking every step. */
static bool write_whole_file(const char *path, const char *data, size_t len) {
  FILE *fp = fopen(path, "w");
  if (!fp) {
    LOGE("Failed to open %s for writing (errno %d)", path, errno);
    return false;
  }
  const bool wrote = fwrite(data, 1, len, fp) == len;
  const bool closed = fclose(fp) == 0;
  if (!wrote || !closed) {
    LOGE("Failed to write %s (errno %d)", path, errno);
    return false;
  }
  return true;
}

bool config_writer_replace_file(const char *path, const char *data, size_t len) {
  char tmp_path[CONFIG_PATH_MAX];
  char bak_path[CONFIG_PATH_MAX];
  if (!path || (!data && len > 0) ||
      !build_sibling_path(tmp_path, sizeof(tmp_path), path, CONFIG_TMP_SUFFIX) ||
      !build_sibling_path(bak_path, sizeof(bak_path), path, CONFIG_BACKUP_SUFFIX)) {
    LOGE("Cannot save %s: bad path or path too long", path ? path : "(null)");
    return false;
  }

  if (!write_whole_file(tmp_path, data, len)) {
    fs_remove(tmp_path);
    return false;
  }

  /* The Vita refuses to rename onto an existing file, so the old file is moved aside first. A
   * whole copy exists under one of the three names at every step. If the real file is already
   * missing, a backup left by an earlier crash is the only copy and must not be touched. */
  const bool had_real = file_exists(path);
  const bool had_orphan_backup = !had_real && file_exists(bak_path);
  if (had_real) {
    fs_remove(bak_path); /* stale backup from a save that died after tmp -> real */
    const int err = fs_rename(path, bak_path);
    if (err) {
      LOGE("Cannot save %s: moving it to %s failed (error 0x%X); old file kept", path, bak_path,
           (unsigned)err);
      fs_remove(tmp_path);
      return false;
    }
  }

  const int err = fs_rename(tmp_path, path);
  if (err) {
    LOGE("Cannot save %s: renaming %s onto it failed (error 0x%X)", path, tmp_path, (unsigned)err);
    if (had_real) {
      const int restore_err = fs_rename(bak_path, path);
      if (restore_err)
        LOGE("Could not restore %s from %s (error 0x%X); startup will retry", path, bak_path,
             (unsigned)restore_err);
      else
        LOGW("Restored %s from %s after the failed save", path, bak_path);
    }
    fs_remove(tmp_path);
    return false;
  }

  if (!had_real && !had_orphan_backup)
    return true; /* first save: there is no backup to delete */
  const int rm_err = fs_remove(bak_path);
  if (rm_err)
    LOGW("Saved %s but could not delete %s (error 0x%X); the next save removes it", path, bak_path,
         (unsigned)rm_err);
  return true;
}

bool config_writer_recover_backup(const char *path) {
  char bak_path[CONFIG_PATH_MAX];
  if (!path || !build_sibling_path(bak_path, sizeof(bak_path), path, CONFIG_BACKUP_SUFFIX))
    return false;
  if (file_exists(path) || !file_exists(bak_path))
    return false;
  const int err = fs_rename(bak_path, path);
  if (err) {
    LOGE("Config %s is missing and restoring %s failed (error 0x%X)", path, bak_path,
         (unsigned)err);
    return false;
  }
  LOGW("Config %s was missing; restored it from %s (the last save was interrupted)", path,
       bak_path);
  return true;
}

/** write_file() - Saves @p data over the config file through config_writer_replace_file(). */
static bool write_file(const char *data, size_t len) {
  return config_writer_replace_file(s_path, data, len);
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
