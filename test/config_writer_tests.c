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

/* catches: a synchronous save reporting success when the file could not be written, which would
 * let a pairing be reported as stored when it was lost; and a failed write wedging the writer so
 * later saves never land. */
static void test_sync_save_reports_a_failed_write(void) {
  remove(CFG_FILENAME);
  assert(mkdir(CFG_FILENAME, 0700) == 0); /* fopen("w") on a directory fails */
  assert(!submit_text("lost\n", true));
  assert(rmdir(CFG_FILENAME) == 0);

  assert(submit_text("kept\n", true));
  char saved[FILE_TEXT_MAX];
  read_file(saved, sizeof(saved));
  assert(strcmp(saved, "kept\n") == 0);
}

int main(void) {
  config_writer_init(CFG_FILENAME);
  test_latest_save_wins_and_sync_save_waits();
  test_sync_save_reports_a_failed_write();
  remove(CFG_FILENAME);
  puts("config writer tests passed");
  return 0;
}
