/**
 * @file config_writer.h
 * @brief Memory-card writer for the config file (ticket #353)
 *
 * Saving the config costs about 100 ms on the card, and a Settings press used to pay it on the UI
 * thread. The save is split in two: the caller formats the whole file into memory (it owns
 * context.config), and one small low-priority thread writes those bytes with a single
 * open/write/close.
 *
 * One pending slot, latest wins: a save queued while another is still waiting replaces it, so the
 * writer never writes an older config over a newer one. A save queued while the writer is busy
 * becomes the next pending one.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
