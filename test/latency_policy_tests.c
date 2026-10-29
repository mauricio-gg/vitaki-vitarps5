// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the Latency Mode start-bitrate policy (GH #420): each mode asks
// the console for its own bitrate, the Internet/PSN cap wins when lower, and the one-time "move
// everyone to Max" migration fires once without undoing a later user choice.
// `./tools/build.sh test` only cross-compiles for arm-vita-eabi and never executes, so run
// these natively:
//
//   cc -std=c99 -Wall -Wextra -I vita/include \
//      test/latency_policy_tests.c vita/src/latency_policy.c -o /tmp/latency_policy_tests && \
//      /tmp/latency_policy_tests

#include <assert.h>
#include <stdio.h>

#include "host_constants.h"
#include "latency_policy.h"

/* Home network: every mode asks for exactly its ladder value, nothing is capped. */
static void test_home_network_uses_the_ladder(void) {
  const unsigned int expected[] = {1500, 2500, 3500, 4500, 6000, 10000};
  for (int mode = 0; mode < VITA_LATENCY_MODE_COUNT; mode++) {
    unsigned int uncapped = 99;
    assert(latency_policy_start_kbps((VitaChiakiLatencyMode)mode, false, &uncapped) ==
           expected[mode]);
    assert(uncapped == 0);
  }
}

/* Internet/PSN: the lower of mode and cap wins; the uncapped value is reported only when the
 * cap actually lowered it (the Retrying-at screen and the log use that). */
static void test_psn_cap_lowest_wins(void) {
  unsigned int uncapped = 0;
  assert(latency_policy_start_kbps(VITA_LATENCY_MODE_ULTRA_LOW, true, &uncapped) == 1500);
  assert(uncapped == 0);
  assert(latency_policy_start_kbps(VITA_LATENCY_MODE_BALANCED, true, &uncapped) ==
         PSN_REMOTE_BITRATE_CAP_KBPS);
  assert(uncapped == 0);
  assert(latency_policy_start_kbps(VITA_LATENCY_MODE_MAX, true, &uncapped) ==
         PSN_REMOTE_BITRATE_CAP_KBPS);
  assert(uncapped == 6000);
  assert(latency_policy_start_kbps(VITA_LATENCY_MODE_PROBE_10MBPS, true, &uncapped) ==
         PSN_REMOTE_BITRATE_CAP_KBPS);
  assert(uncapped == 10000);
  assert(latency_policy_start_kbps(VITA_LATENCY_MODE_HIGH, true, NULL) ==
         PSN_REMOTE_BITRATE_CAP_KBPS);
}

/* An old config (marker 0) with any saved mode goes to Max once; after the marker is saved, the
 * user's later choice survives every following load. */
static void test_migration_fires_once_and_choice_sticks(void) {
  VitaChiakiLatencyMode mode = VITA_LATENCY_MODE_ULTRA_LOW;
  int marker = 0;
  assert(latency_policy_migrate(&mode, &marker));
  assert(mode == VITA_LATENCY_MODE_MAX);
  assert(marker == LATENCY_MODE_MIGRATION_CURRENT);

  mode = VITA_LATENCY_MODE_LOW; /* user picks Low, config saved, next boot loads it */
  assert(!latency_policy_migrate(&mode, &marker));
  assert(mode == VITA_LATENCY_MODE_LOW);
  assert(marker == LATENCY_MODE_MIGRATION_CURRENT);
}

int main(void) {
  test_home_network_uses_the_ladder();
  test_psn_cap_lowest_wins();
  test_migration_fires_once_and_choice_sticks();
  printf("latency_policy_tests: all passed\n");
  return 0;
}
