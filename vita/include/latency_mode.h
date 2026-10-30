// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#pragma once

/* The Settings "Latency Mode" choices, ordered lowest to highest start bitrate. Kept in its own
 * header so the pure policy in latency_policy.h can be built without the rest of config.h. */
typedef enum vita_chiaki_latency_mode_t {
  VITA_LATENCY_MODE_ULTRA_LOW = 0,  // Start bitrate 1.5 Mbps
  VITA_LATENCY_MODE_LOW,            // Start bitrate 2.5 Mbps
  VITA_LATENCY_MODE_BALANCED,       // Start bitrate 3.5 Mbps
  VITA_LATENCY_MODE_HIGH,           // Start bitrate 4.5 Mbps
  VITA_LATENCY_MODE_MAX,            // Start bitrate 6 Mbps (default)
  VITA_LATENCY_MODE_EXTREME,        // Start bitrate 10 Mbps (the highest step)
  VITA_LATENCY_MODE_COUNT
} VitaChiakiLatencyMode;
