// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the config writer, vita/src/config_writer.c (issue #353).
// `./tools/build.sh test` only cross-compiles, so run these on the host (from the repo root):
//   cc -std=gnu11 -I test/stubs -I vita/include -I lib/include -I lib/src \
//      -DCFG_FILENAME='"/tmp/config_writer_test.toml"' test/config_writer_tests.c \
//      vita/src/config_writer.c lib/src/thread.c lib/src/time.c -lpthread -o /tmp/config_writer_tests && \
//      /tmp/config_writer_tests

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config_writer.h"
#include "context.h"

#define QUEUED_SAVES 500
#define FILE_TEXT_MAX 64

/* config_writer.c logs through the context's log; the host build has no Vita log sink. */
VitaChiakiContext context;
void chiaki_log(ChiakiLog *log, ChiakiLogLevel level, const char *fmt, ...) {
  (void)log;
  (void)level;
  (void)fmt;
}

/** Submits a heap copy of @p text, as the caller of the writer does. */
static bool submit_text(const char *text, bool wait) {
  return config_writer_submit(strdup(text), strlen(text), 0, wait);
}

/** Reads the config file into @p out (NUL-terminated). */
static void read_file(char *out, size_t cap) {
  FILE *fp = fopen(CFG_FILENAME, "r");
  assert(fp != NULL);
  size_t n = fread(out, 1, cap - 1, fp);
  out[n] = '\0';
  assert(fclose(fp) == 0);
}

/* catches: an older queued save landing on disk after a newer one (the file would hold stale
 * settings or a stale token), and a synchronous save returning before its own write finished
 * (the read below would see an older file). */
static void test_latest_save_wins_and_sync_save_waits(void) {
  remove(CFG_FILENAME);
  char text[FILE_TEXT_MAX];
  for (int i = 0; i < QUEUED_SAVES; i++) {
    snprintf(text, sizeof(text), "queued-%d\n", i);
    assert(submit_text(text, false));
  }
  assert(submit_text("final\n", true));

  char saved[FILE_TEXT_MAX];
  read_file(saved, sizeof(saved));
  assert(strcmp(saved, "final\n") == 0);

  /* An async save after the sync one still lands, and flush waits for it. */
  assert(submit_text("after\n", false));
  config_writer_flush();
  read_file(saved, sizeof(saved));
  assert(strcmp(saved, "after\n") == 0);
}

#define CFG_TMP CFG_FILENAME CONFIG_TMP_SUFFIX
#define CFG_BAK CFG_FILENAME CONFIG_BACKUP_SUFFIX

/** Writes @p text to @p path, replacing it. */
static void put_file(const char *path, const char *text) {
  FILE *fp = fopen(path, "w");
  assert(fp != NULL);
  assert(fputs(text, fp) >= 0);
  assert(fclose(fp) == 0);
}

static bool exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

/* catches: a save that fails (here the temp file cannot be created) emptying or damaging the
 * existing config, which is the pairing loss in #395; a synchronous save reporting success when
 * the file could not be written; and a failed write wedging the writer so later saves never land. */
static void test_failed_save_keeps_the_old_config(void) {
  remove(CFG_FILENAME);
  remove(CFG_BAK);
  put_file(CFG_FILENAME, "old-config\n");
  assert(mkdir(CFG_TMP, 0700) == 0); /* fopen on the temp path now fails */
  assert(!submit_text("lost\n", true));

  char saved[FILE_TEXT_MAX];
  read_file(saved, sizeof(saved));
  assert(strcmp(saved, "old-config\n") == 0);
  assert(!exists(CFG_BAK));
  rmdir(CFG_TMP); /* the failed save may already have removed the empty directory */

  assert(submit_text("kept\n", true));
  read_file(saved, sizeof(saved));
  assert(strcmp(saved, "kept\n") == 0);
}

/* catches: a save over an existing config leaving the backup behind, which a later startup
 * could wrongly restore over newer settings, or leaving the old contents in place. */
static void test_save_over_existing_config_leaves_no_backup(void) {
  put_file(CFG_FILENAME, "old-config\n");
  put_file(CFG_BAK, "stale-backup\n"); /* left by a save that died after the rename */
  assert(submit_text("new-config\n", true));

  char saved[FILE_TEXT_MAX];
  read_file(saved, sizeof(saved));
  assert(strcmp(saved, "new-config\n") == 0);
  assert(!exists(CFG_BAK));
  assert(!exists(CFG_TMP));
}

/* catches: a power cut between "old file moved aside" and "new file renamed in" leaving the user
 * with no config at all on the next start; and recovery clobbering a config that is there. */
static void test_startup_restores_backup_only_when_config_is_missing(void) {
  remove(CFG_FILENAME);
  put_file(CFG_BAK, "backup-config\n");
  put_file(CFG_TMP, "half-written"); /* must never be promoted */
  assert(config_writer_recover_backup(CFG_FILENAME));

  char saved[FILE_TEXT_MAX];
  read_file(saved, sizeof(saved));
  assert(strcmp(saved, "backup-config\n") == 0);
  assert(!exists(CFG_BAK));

  put_file(CFG_BAK, "older-backup\n");
  assert(!config_writer_recover_backup(CFG_FILENAME));
  read_file(saved, sizeof(saved));
  assert(strcmp(saved, "backup-config\n") == 0);
  remove(CFG_BAK);
  remove(CFG_TMP);
}

int main(void) {
  config_writer_init(CFG_FILENAME);
  test_latest_save_wins_and_sync_save_waits();
  test_failed_save_keeps_the_old_config();
  test_save_over_existing_config_leaves_no_backup();
  test_startup_restores_backup_only_when_config_is_missing();
  remove(CFG_FILENAME);
  puts("config writer tests passed");
  return 0;
}
