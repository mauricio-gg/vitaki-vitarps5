// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/time.h>

#include <time.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __PSVITA__
#include <psp2/kernel/processmgr.h>
#endif

CHIAKI_EXPORT uint64_t chiaki_time_now_monotonic_us()
{
#if _WIN32
	LARGE_INTEGER f;
	if(!QueryPerformanceFrequency(&f))
		return 0;
	LARGE_INTEGER v;
	if(!QueryPerformanceCounter(&v))
		return 0;
	v.QuadPart *= 1000000;
	v.QuadPart /= f.QuadPart;
	return v.QuadPart;
#elif __PSVITA__
	SceKernelSysClock clk;
	sceKernelGetProcessTime(&clk);
	return clk;
#else
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	return time.tv_sec * 1000000 + time.tv_nsec / 1000;
#endif
}

CHIAKI_EXPORT void chiaki_sleep_ms(uint64_t ms)
{
#if _WIN32
	// Sleep() takes a 32-bit DWORD millisecond count, and the all-ones value
	// (0xFFFFFFFF) is reserved as the INFINITE sentinel, so clamp one below
	// the DWORD max rather than let an oversized ms silently truncate into
	// either a much shorter sleep or, worse, an infinite one.
	DWORD clamped_ms = ms > (UINT32_MAX - 1) ? (UINT32_MAX - 1) : (DWORD)ms;
	Sleep(clamped_ms);
#elif __PSVITA__
	// sceKernelDelayThread() takes microseconds in an unsigned int, and this
	// function multiplies ms by 1000 before the cast -- ms beyond ~4,294,967
	// (~71.5 min) would silently wrap to a near-zero delay. Clamp instead.
	uint64_t clamped_ms = ms > (UINT32_MAX / 1000) ? (UINT32_MAX / 1000) : ms;
	sceKernelDelayThread((unsigned int)(clamped_ms * 1000));
#else
	struct timespec req;
	req.tv_sec = (time_t)(ms / 1000);
	req.tv_nsec = (long)((ms % 1000) * 1000000);
	// A signal can interrupt nanosleep() before the full duration elapses.
	// nanosleep() fills `rem` with the remaining time in that case, so retry
	// with it until the requested duration has fully elapsed -- a partial
	// sleep here would defeat the purpose of this function.
	struct timespec rem;
	while(nanosleep(&req, &rem) == -1 && errno == EINTR)
		req = rem;
#endif
}
