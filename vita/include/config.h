#pragma once
#include <chiaki/common.h>
#include <chiaki/session.h>

#include "host.h"
#include "logging.h"
#include "controller.h"

#ifndef CFG_VERSION
#define CFG_VERSION 1
#endif

#ifndef CFG_FILENAME
#define CFG_FILENAME "ux0:data/vita-chiaki/chiaki.toml"
#endif

typedef enum vita_chiaki_latency_mode_t {
  VITA_LATENCY_MODE_ULTRA_LOW = 0,   // Minimum bandwidth (≈1.2 Mbps)
  VITA_LATENCY_MODE_LOW,             // Low bandwidth (≈1.8 Mbps)
  VITA_LATENCY_MODE_BALANCED,        // Balanced default (≈2.6 Mbps)
  VITA_LATENCY_MODE_HIGH,            // High quality (≈3.2 Mbps)
  VITA_LATENCY_MODE_MAX,             // Near-max Vita Wi-Fi (≈3.8 Mbps)
  VITA_LATENCY_MODE_COUNT
} VitaChiakiLatencyMode;

/// Settings for the app
typedef struct vita_chiaki_config_t {
  int cfg_version;
  // We use a global PSN Account ID so users only have to enter it once
  char* psn_account_id;
  /// Whether discovery is enabled by default
  bool auto_discovery;
  ChiakiVideoResolutionPreset resolution;
  ChiakiVideoFPSPreset fps;
  size_t num_manual_hosts;
  VitaChiakiHost* manual_hosts[MAX_NUM_HOSTS];
  size_t num_registered_hosts;
  VitaChiakiHost* registered_hosts[MAX_NUM_HOSTS];
  // TODO: Logfile path
  // TODO: Loglevel
  // controller map id - corresponds to custom slot index (0, 1, or 2)
  int controller_map_id;
  ControllerMapStorage custom_maps[3];   // 3 independent custom mapping slots
  bool custom_maps_valid[3];             // Validity flags for each custom slot
  bool circle_btn_confirm;
  bool show_latency;  // Display live latency/FPS metrics in Profile + stream HUD
  bool show_network_indicator; // Display "Network unstable" overlay in stream HUD
  bool show_stream_exit_hint; // Display stream exit shortcut hint in stream HUD
  bool stretch_video;
  bool force_30fps;   // Drop frames locally to hold 30 fps presentation
  bool send_actual_start_bitrate; // Guard for RP-StartBitrate payload
  bool clamp_soft_restart_bitrate; // Keep soft restart bitrate <= ~1.5 Mbps
  VitaChiakiLatencyMode latency_mode;
  VitaLoggingConfig logging;
  bool show_nav_labels;  // Show text labels below navigation icons when selected
} VitaChiakiConfig;

void config_parse(VitaChiakiConfig* cfg);
void config_free(VitaChiakiConfig* cfg);
bool config_serialize(VitaChiakiConfig* cfg);
