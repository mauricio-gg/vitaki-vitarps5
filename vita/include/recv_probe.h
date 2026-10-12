// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

/*
 * Vita side of the receive-thread probes (#413, docs/design/streaming.md sections 9.6 to 9.8):
 * the access to the Takion receive thread's accumulator for the audio, video and log paths, and
 * the reporter for PIPE/RECVTIME, PIPE/SOCKQ and PIPE/RECVTHREAD.
 *
 * Every writer here first checks that the caller IS the receive thread (its thread id against the
 * one the thread recorded at start). The check is one call and no clock read, so a log call from
 * any other thread pays no timing cost. No lock, no allocation, no log call on these paths.
 */
#pragma once

#include <chiaki/log.h>
#include <chiaki/takion.h>
#include <stdint.h>

/* An open timing of one log call; acc is NULL when the caller is not the receive thread. */
typedef struct recv_probe_log_t {
  ChiakiTakionRecvTimeAcc *acc;
  uint64_t start_us;
} RecvProbeLog;

/*
 * The receive thread's accumulator if the calling thread is the receive thread of the running
 * session, else NULL.
 */
ChiakiTakionRecvTimeAcc *recv_probe_acc(void);

/* Starts timing a log call, reading the clock itself (lib log path). */
RecvProbeLog recv_probe_log_begin(void);

/* Starts timing a log call from a clock read the caller already made (Vita LOGx macros). */
RecvProbeLog recv_probe_log_begin_at(uint64_t start_us);

/* Ends the timing: one clock read, only on the receive thread and only if the line is written. */
void recv_probe_log_end(RecvProbeLog log, ChiakiLogLevel level);

/* Forgets what the reporter printed, so the next stream prints PIPE/RECVTHREAD again. */
void recv_probe_reset_report(void);

/* Prints PIPE/RECVTIME and PIPE/SOCKQ for the window, and PIPE/RECVTHREAD when it changed. */
void recv_probe_report(const ChiakiTakionRecvbufWindow *window);
