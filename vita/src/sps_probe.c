// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "sps_probe.h"

#include <string.h>

#define NAL_TYPE_MASK 0x1F
#define NAL_FORBIDDEN_ZERO_BIT 0x80
#define NAL_TYPE_SPS 7

/* Written only by the receive thread (and by sps_probe_reset() while it is idle). probe_lock is
 * odd while a write is in progress. */
static SpsProbeData probe;
static volatile uint32_t probe_lock;

bool sps_probe_locate(const uint8_t *buf, size_t size, size_t *nal_off, size_t *nal_len) {
  /* Cheapest rejections first: this runs for every video buffer. */
  if (buf == NULL || size < 3 + SPS_PROBE_MIN_NAL_LEN || buf[0] != 0 || buf[1] != 0)
    return false;
  size_t off;
  if (buf[2] == 1)
    off = 3;
  else if (buf[2] == 0 && buf[3] == 1)
    off = 4;
  else
    return false;
  if ((buf[off] & NAL_FORBIDDEN_ZERO_BIT) || (buf[off] & NAL_TYPE_MASK) != NAL_TYPE_SPS)
    return false;

  size_t limit = off + SPS_PROBE_SCAN_MAX < size ? off + SPS_PROBE_SCAN_MAX : size;
  size_t end = limit;
  for (size_t p = off + 1; p + 2 < limit; p++) {
    /* 00 00 00 or 00 00 01 cannot occur inside a NAL unit (emulation prevention), so it is the
     * start of the next NAL (or trailing zero bytes). */
    if (buf[p] == 0 && buf[p + 1] == 0 && buf[p + 2] <= 1) {
      end = p;
      break;
    }
  }
  if (end - off < SPS_PROBE_MIN_NAL_LEN)
    return false;
  *nal_off = off;
  *nal_len = end - off;
  return true;
}

void sps_probe_reset(void) {
  probe_lock = 0;
  memset(&probe, 0, sizeof(probe));
}

/* Bracket every change of `probe` made while frames are flowing, so a reader never copies a
 * half-written update. */
static inline void probe_write_begin(void) {
  probe_lock++;
  __sync_synchronize();
}

static inline void probe_write_end(void) {
  __sync_synchronize();
  probe_lock++;
}

void sps_probe_observe(const uint8_t *buf, size_t size) {
  size_t off;
  size_t len;
  if (!sps_probe_locate(buf, size, &off, &len))
    return;
  const uint8_t *nal = buf + off;
  uint32_t stored = len < SPS_PROBE_CAP ? (uint32_t)len : SPS_PROBE_CAP;

  if (probe.first_len == 0) {
    probe_write_begin();
    memcpy(probe.first, nal, stored);
    probe.first_len = (uint32_t)len;
    probe_write_end();
    return;
  }
  bool same = len == probe.first_len && memcmp(nal, probe.first, stored) == 0;
  probe_write_begin();
  probe.seen_again++;
  if (!same)
    probe.differed++;
  probe_write_end();
}

uint32_t sps_probe_version(void) {
  return probe_lock;
}

bool sps_probe_snapshot(SpsProbeData *out, int attempts) {
  for (int attempt = 0; attempt < attempts; attempt++) {
    uint32_t before = probe_lock;
    if (before & 1)
      continue;
    __sync_synchronize();
    *out = probe;
    __sync_synchronize();
    if (probe_lock == before)
      return true;
  }
  return false;
}
