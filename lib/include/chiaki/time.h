// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_TIME_H
#define CHIAKI_TIME_H

#include "common.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

CHIAKI_EXPORT uint64_t chiaki_time_now_monotonic_us();

static inline uint64_t chiaki_time_now_monotonic_ms() { return chiaki_time_now_monotonic_us() / 1000; }

/**
 * Portable blocking sleep, independent of any socket/fd (unlike
 * chiaki_stop_pipe_sleep(), which is a select() on the stop pipe and can
 * itself fail with a transient network error on platforms where the stop
 * pipe is backed by a socket -- see lib/src/stoppipe.c). Use this whenever a
 * caller needs a real elapsed-time delay regardless of stop-pipe state.
 *
 * Guarantees the full requested duration elapses (retries internally on
 * EINTR on POSIX), i.e. no partial sleeps.
 *
 * Valid range: the underlying primitive is 32-bit on some platforms, so
 * very large ms values are clamped (not silently wrapped) rather than
 * producing a near-zero sleep. On __PSVITA__, sceKernelDelayThread() takes
 * microseconds in an unsigned int, so ms is multiplied by 1000 before the
 * cast -- the safe ceiling is ~4,294,967ms (~71.5 minutes). On _WIN32,
 * Sleep() takes milliseconds directly in a DWORD, so the ceiling is
 * ~4,294,967,295ms (~49.7 days); the DWORD value 0xFFFFFFFF is also
 * reserved as the INFINITE sentinel, so the clamp stays one below it.
 * POSIX nanosleep()'s time_t/long fields do not have this limit at these
 * scales.
 *
 * @param ms Milliseconds to sleep for.
 */
CHIAKI_EXPORT void chiaki_sleep_ms(uint64_t ms);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_TIME_H
