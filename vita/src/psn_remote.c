#include "context.h"
#include "psn_remote.h"

int psn_remote_prepare_connect_host(VitaChiakiHost *host) {
  if (!host) {
    LOGE("PSN remote prepare failed: missing host");
    return 1;
  }

  // Core PSN holepunch stack is not yet enabled for Vita builds.
  LOGE("PSN internet remote play stack is not enabled in this build");
  return 1;
}
