/**
 * @file ui_value_labels.c
 * @brief Words for config values and connection facts
 */

#include "ui/ui_value_labels.h"

#include <stddef.h>
#include <time.h>

#include "context.h"
#include "psn_auth.h"
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

const char *ui_label_theme(VitaChiakiTheme theme) {
  switch (theme) {
    case VITA_THEME_EMBER:
      return "Ember";
    case VITA_THEME_ORCHID:
      return "Orchid";
    case VITA_THEME_MOSS:
      return "Moss";
    case VITA_THEME_GRAPHITE:
      return "Graphite";
    case VITA_THEME_OCEAN:
    default:
      return "Ocean";
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

UiConnectionWords ui_connection_words(const VitaChiakiHost *host) {
  UiConnectionFacts facts = {0};
  if (host) {
    /* The card mapping decides discovered / standby for Home's list; reuse it. */
    ConsoleCardInfo card;
    ui_cards_map_host((VitaChiakiHost *)host, &card);
    facts = (UiConnectionFacts){
        .selected = true,
        .registered = card.is_registered,
        .discovered = card.is_discovered,
        .standby = card.state == CONSOLE_CARD_STATE_STANDBY,
        .psn_source = host->source == VITA_HOST_SOURCE_PSN_REMOTE,
        .manual = (host->type & MANUALLY_ADDED) != 0,
        .internet_ok = card.has_internet && psn_auth_token_is_valid((uint64_t)time(NULL)),
    };
  }
  return ui_console_connection_words(&facts);
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
