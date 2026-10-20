#pragma once

#include <stdint.h>

/* Host-build stand-in: the test supplies the clock (see test/manual_host_discovery_tests.c). */
uint64_t sceKernelGetProcessTimeWide(void);
