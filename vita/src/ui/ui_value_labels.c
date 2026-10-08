/**
 * @file ui_value_labels.c
 * @brief Words for config values and connection facts
 */

#include "ui/ui_value_labels.h"

#include <stddef.h>

#include "context.h"
#include "ui/ui_console_cards.h"

const char *ui_label_resolution(ChiakiVideoResolutionPreset preset) {
  switch (preset) {
    case CHIAKI_VIDEO_RESOLUTION_PRESET_360p:
      return "360p";
    case CHIAKI_VIDEO_RESOLUTION_PRESET_540p:
    case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
    case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p:
    default:
      /* Legacy and unsupported values are shown as the Vita preset they actually use. */
      return "540p";
  }
}

const char *ui_label_fps(ChiakiVideoFPSPreset preset) {
  switch (preset) {
    case CHIAKI_VIDEO_FPS_PRESET_30:
      return "30 FPS";
    case CHIAKI_VIDEO_FPS_PRESET_60:
    default:
      return "60 FPS";
  }
}

const char *ui_label_latency_mode(VitaChiakiLatencyMode mode) {
  switch (mode) {
    case VITA_LATENCY_MODE_ULTRA_LOW:
      return "Ultra Low (≈1.2 Mbps)";
    case VITA_LATENCY_MODE_LOW:
      return "Low (≈1.8 Mbps)";
    case VITA_LATENCY_MODE_HIGH:
      return "High (≈3.2 Mbps)";
    case VITA_LATENCY_MODE_MAX:
      return "Max (≈3.8 Mbps)";
    case VITA_LATENCY_MODE_BALANCED:
    default:
      return "Balanced (≈2.6 Mbps)";
  }
}

const char *ui_label_on_off(bool on) {
  return on ? "On" : "Off";
}

const char *ui_label_background_blur(VitaChiakiBackgroundBlur blur) {
  switch (blur) {
    case VITA_BACKGROUND_BLUR_SOFT:
      return "Soft";
    case VITA_BACKGROUND_BLUR_STRONG:
      return "Strong";
    case VITA_BACKGROUND_BLUR_DARK:
      return "Dark";
    case VITA_BACKGROUND_BLUR_NONE:
    default:
      return "None";
  }
}

VitaChiakiHost *ui_profile_reference_host(void) {
  if (context.active_host) {
    return context.active_host;
  }

  int selected = ui_cards_get_selected_index();
  int host_idx = 0;
  VitaChiakiHost *first_host = NULL;
  for (int i = 0; i < MAX_CONTEXT_HOSTS; i++) {
    VitaChiakiHost *host = context.hosts[i];
    if (!host) {
      continue;
    }
    if (!first_host) {
      first_host = host;
    }
    if (host_idx == selected) {
      return host;
    }
    host_idx++;
  }

  return first_host;
}

const char *ui_connection_network_type(const VitaChiakiHost *host) {
  if (host && host->discovery_state) {
    return "Local Wi-Fi";
  }
  if (host && host->source == VITA_HOST_SOURCE_PSN_REMOTE) {
    return "PSN Internet";
  }
  if (host && (host->type & MANUALLY_ADDED)) {
    return "Manual Host";
  }
  return "Unavailable";
}

/* Read only the host's inline snapshot fields (display_name/hostname) -- never
 * discovery_state->host_name/host_addr or registered_state->server_nickname, which are
 * upstream heap structs the discovery thread may free/re-strdup concurrently. display_name
 * already encodes the discovery-name > registered-nickname > hostname precedence. */
const char *ui_connection_console_name(const VitaChiakiHost *host) {
  if (host && host->display_name[0]) {
    return host->display_name;
  }
  if (host && host->hostname[0]) {
    return host->hostname;
  }
  return "Not selected";
}

const char *ui_connection_console_ip(const VitaChiakiHost *host) {
  return (host && host->hostname[0]) ? host->hostname : NULL;
}
