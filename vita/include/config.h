#pragma once
#include <chiaki/common.h>
#include <chiaki/session.h>
#include <stdint.h>

#include "host.h"
#include "logging.h"
#include "controller.h"
#include "room_icons.h"

#ifndef CFG_VERSION
#define CFG_VERSION 1
#endif

#ifndef CFG_FILENAME
#define CFG_FILENAME "ux0:data/vita-chiaki/chiaki.toml"
#endif

/* 1 = tokens minted with 48-char upstream duid in authorize URL (GH #204).
 * Configs at a lower provenance value predate the duid fix and have their
 * PSN OAuth tokens invalidated by migrate_auth_provenance() in config.c. */
#define PSN_AUTH_PROVENANCE_CURRENT 1

typedef enum vita_chiaki_latency_mode_t {
  VITA_LATENCY_MODE_ULTRA_LOW = 0,  // Minimum bandwidth (≈1.2 Mbps)
  VITA_LATENCY_MODE_LOW,            // Low bandwidth (≈1.8 Mbps)
  VITA_LATENCY_MODE_BALANCED,       // Balanced default (≈2.6 Mbps)
  VITA_LATENCY_MODE_HIGH,           // High quality (≈3.2 Mbps)
  VITA_LATENCY_MODE_MAX,            // Near-max Vita Wi-Fi (≈3.8 Mbps)
  VITA_LATENCY_MODE_COUNT
} VitaChiakiLatencyMode;

/// Strength of the blur / darkening drawn behind menus (SPEC C27). Stored as an int in the config.
typedef enum vita_chiaki_background_blur_t {
  VITA_BACKGROUND_BLUR_NONE = 0,
  VITA_BACKGROUND_BLUR_SOFT,
  VITA_BACKGROUND_BLUR_STRONG,
  VITA_BACKGROUND_BLUR_DARK,
  VITA_BACKGROUND_BLUR_COUNT
} VitaChiakiBackgroundBlur;

/// Colour theme of the XMB UI (issue #348). The integer is what is saved under settings.theme, so
/// presets may only be appended before THEME_COUNT; reordering or inserting would recolour every
/// existing user's saved choice.
typedef enum vita_chiaki_theme_t {
  VITA_THEME_OCEAN = 0,
  VITA_THEME_EMBER,
  VITA_THEME_ORCHID,
  VITA_THEME_MOSS,
  VITA_THEME_GRAPHITE,
  VITA_THEME_COUNT
} VitaChiakiTheme;

/// Which picture is drawn behind menus (issue #375). The integer is what is saved under
/// settings.background, so backgrounds may only be appended before BACKGROUND_COUNT; reordering or
/// inserting would switch every existing user's saved choice.
typedef enum vita_chiaki_background_t {
  VITA_BACKGROUND_WAVES = 0,
  VITA_BACKGROUND_GLYPHS,
  VITA_BACKGROUND_COUNT
} VitaChiakiBackground;

/// Settings for the app
typedef struct vita_chiaki_config_t {
  int cfg_version;
  // We use a global PSN Account ID so users only have to enter it once
  char *psn_account_id;
  // PSN OAuth tokens used for internet remote play.
  char *psn_oauth_access_token;
  char *psn_oauth_refresh_token;
  uint64_t psn_oauth_expires_at_unix;
  char *psn_oauth_device_code_url;
  char *psn_oauth_authorize_url;
  char *psn_oauth_token_url;
  char *psn_oauth_client_id;
  char *psn_oauth_client_secret;
  char *psn_oauth_scope;
  char *psn_oauth_redirect_uri;
  char *psn_client_duid;
  int psn_auth_provenance;
  bool psn_remoteplay_enabled;
  /// Whether discovery is enabled by default
  bool auto_discovery;
  ChiakiVideoResolutionPreset resolution;
  ChiakiVideoFPSPreset fps;
  size_t num_manual_hosts;
  VitaChiakiHost *manual_hosts[MAX_MANUAL_HOSTS];
  size_t num_registered_hosts;
  VitaChiakiHost *registered_hosts[MAX_REGISTERED_HOSTS];
  // TODO: Logfile path
  // TODO: Loglevel
  // controller map id - corresponds to custom slot index (0, 1, or 2)
  int controller_map_id;
  ControllerMapStorage custom_maps[3];  // 3 independent custom mapping slots
  bool custom_maps_valid[3];            // Validity flags for each custom slot
  bool circle_btn_confirm;
  bool show_latency;            // Display live latency/FPS metrics in Profile + stream HUD
  bool show_network_indicator;  // Display "Network unstable" overlay in stream HUD
  bool show_stream_exit_hint;   // Display stream exit shortcut hint in stream HUD
  bool stretch_video;
  bool force_30fps;                 // Drop frames locally to hold 30 fps presentation
  bool send_actual_start_bitrate;   // Guard for RP-StartBitrate payload
  bool clamp_soft_restart_bitrate;  // Keep soft restart bitrate <= ~1.5 Mbps
  bool submit_on_missing_ref;       // GH #251 A/B: feed missing-ref P-frames to the decoder (drift)
                                    // instead of freezing for the IDR
  VitaChiakiLatencyMode latency_mode;
  VitaLoggingConfig logging;
  VitaChiakiBackground background;                  // Picture behind menus, default Waves
  VitaChiakiBackgroundBlur background_blur;         // Blur of the Waves background, default None
  VitaChiakiBackgroundBlur background_blur_glyphs;  // Blur of the Glyphs background, default None
  VitaChiakiTheme theme;                            // XMB colour theme, default Ocean
  bool show_button_hints;    // Draw the button hint row on menus (not the in-stream exit hint)
  RoomIconTable room_icons;  // Per-console room icon choices, keyed by console MAC
} VitaChiakiConfig;

void config_parse(VitaChiakiConfig *cfg);

/**
 * config_background_blur() - The blur of the background currently selected (Waves or Glyphs).
 * @param cfg  Config to read; NULL gives None.
 * @return     The blur level to draw and to show in Settings.
 */
VitaChiakiBackgroundBlur config_background_blur(const VitaChiakiConfig *cfg);

/**
 * config_set_background_blur() - Stores the blur of the background currently selected, leaving the
 * other background's remembered blur untouched.
 * @param cfg   Config to change; NULL is ignored.
 * @param blur  New blur level; a value outside the enum is ignored.
 */
void config_set_background_blur(VitaChiakiConfig *cfg, VitaChiakiBackgroundBlur blur);
void config_free(VitaChiakiConfig *cfg);

/**
 * Saves the config and waits for the write: true only when the file was really written. For
 * callers that act on the result (pairing, host edits). Waits behind any save already queued, so
 * order with config_serialize_async() is kept. Costs ~100 ms on the caller.
 */
bool config_serialize(VitaChiakiConfig *cfg);

/**
 * Saves the config without waiting for the memory card: formats it now (on the caller's thread,
 * which owns @p cfg) and queues the write. For UI-thread callers that only log a failure; the
 * writer logs a failed write. A later save replaces one still waiting (see config_writer.h).
 */
void config_serialize_async(VitaChiakiConfig *cfg);
