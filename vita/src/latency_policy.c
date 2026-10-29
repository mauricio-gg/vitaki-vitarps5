// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "latency_policy.h"

#include "host_constants.h"

#define LATENCY_KBPS_ULTRA_LOW 1500
#define LATENCY_KBPS_LOW 2500
#define LATENCY_KBPS_BALANCED 3500
#define LATENCY_KBPS_HIGH 4500
#define LATENCY_KBPS_MAX 6000
#define LATENCY_KBPS_PROBE_10MBPS 10000

unsigned int latency_policy_kbps(VitaChiakiLatencyMode mode) {
  switch (mode) {
    case VITA_LATENCY_MODE_ULTRA_LOW:
      return LATENCY_KBPS_ULTRA_LOW;
    case VITA_LATENCY_MODE_LOW:
      return LATENCY_KBPS_LOW;
    case VITA_LATENCY_MODE_HIGH:
      return LATENCY_KBPS_HIGH;
    case VITA_LATENCY_MODE_MAX:
      return LATENCY_KBPS_MAX;
    case VITA_LATENCY_MODE_PROBE_10MBPS:
      return LATENCY_KBPS_PROBE_10MBPS;
    case VITA_LATENCY_MODE_BALANCED:
    default:
      return LATENCY_KBPS_BALANCED;
  }
}

unsigned int latency_policy_start_kbps(VitaChiakiLatencyMode mode, bool psn_remote,
                                       unsigned int *uncapped_kbps) {
  unsigned int kbps = latency_policy_kbps(mode);
  if (uncapped_kbps)
    *uncapped_kbps = 0;
  if (psn_remote && kbps > PSN_REMOTE_BITRATE_CAP_KBPS) {
    if (uncapped_kbps)
      *uncapped_kbps = kbps;
    return PSN_REMOTE_BITRATE_CAP_KBPS;
  }
  return kbps;
}

bool latency_policy_migrate(VitaChiakiLatencyMode *mode, int *marker) {
  if (*marker >= LATENCY_MODE_MIGRATION_CURRENT)
    return false;
  *mode = VITA_LATENCY_MODE_MAX;
  *marker = LATENCY_MODE_MIGRATION_CURRENT;
  return true;
}
