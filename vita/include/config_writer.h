/**
 * @file config_writer.h
 * @brief Memory-card writer for the config file (ticket #353)
 *
 * Saving the config costs about 100 ms on the card, and a Settings press used to pay it on the UI
 * thread. The save is split in two: the caller formats the whole file into memory (it owns
 * context.config), and one small low-priority thread writes those bytes with a single
 * temp-file write and rename (see config_writer_replace_file()).
 *
 * One pending slot, latest wins: a save queued while another is still waiting replaces it, so the
 * writer never writes an older config over a newer one. A save queued while the writer is busy
 * becomes the next pending one.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Suffix of the temp file a save is written to before it replaces the real file. */
#define CONFIG_TMP_SUFFIX ".tmp"
/** Suffix of the copy of the old file kept while the temp file is renamed into place. */
#define CONFIG_BACKUP_SUFFIX ".bak"

/**
 * Creates the writer's lock and condition and sets the file it writes. Call once at startup,
 * before any other thread can save. Safe to call again; later calls do nothing. Without it,
 * submit() cannot write at all and returns false.
 *
 * @param path  config file path; must stay valid for the life of the process (a string literal)
 */
void config_writer_init(const char *path);

/**
 * Queues the formatted config file for writing and takes ownership of @p data (it is freed by the
 * writer, or here, in every case).
 *
 * @param data       malloc'ed file contents
 * @param len        number of bytes in @p data
 * @param format_us  time the caller spent formatting, carried to the testing-build log line
 * @param wait       false: return as soon as the buffer is queued (the result is true; the
 *                   writer logs a failed write). true: wait until a write that includes this
 *                   save has finished and return its result.
 * @return true when the file was written (wait) or queued (no wait); false when a synchronous
 *         write failed
 */
bool config_writer_submit(char *data, size_t len, uint64_t format_us, bool wait);

/** Waits until nothing is pending and nothing is being written. Call before the process exits. */
void config_writer_flush(void);

/**
 * Replaces the file at @p path with @p data without ever leaving it empty or partial. The bytes go
 * to @p path + CONFIG_TMP_SUFFIX first; only a fully written, closed temp file is swapped in. The
 * Vita refuses to rename onto an existing file, so the old file is moved to @p path +
 * CONFIG_BACKUP_SUFFIX, the temp file is renamed to @p path, and the backup is deleted. On failure
 * the original is kept (or put back), the temp file is removed, and the cause is logged.
 *
 * Synchronous; safe to call from any thread, but two calls for the same path must not overlap.
 *
 * @param path  file to replace (the temp and backup names must also fit in 255 characters)
 * @param data  new contents
 * @param len   number of bytes in @p data
 * @return true only when @p path now holds @p data
 */
bool config_writer_replace_file(const char *path, const char *data, size_t len);

/**
 * Startup repair for a save that died between moving the old file aside and renaming the new one
 * in. If @p path is missing and its backup exists, the backup is renamed back and a warning is
 * logged. A leftover temp file is ignored: it may be half written. Call before reading the file.
 *
 * @return true when the backup was restored
 */
bool config_writer_recover_backup(const char *path);
