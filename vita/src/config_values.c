#include <string.h>
#include <psp2/registrymgr.h>

#include "config_internal.h"

ChiakiVideoResolutionPreset parse_resolution_preset(const char *preset) {
  if (!preset)
    return CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
  if (strcmp(preset, "360p") == 0)
    return CHIAKI_VIDEO_RESOLUTION_PRESET_360p;
  if (strcmp(preset, "720p") == 0)
    return CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
  if (strcmp(preset, "1080p") == 0)
    return CHIAKI_VIDEO_RESOLUTION_PRESET_1080p;
  return CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
}

ChiakiVideoResolutionPreset normalize_resolution_for_vita(ChiakiVideoResolutionPreset preset,
                                                          bool *was_downgraded) {
  if (was_downgraded)
    *was_downgraded = false;
  if (preset == CHIAKI_VIDEO_RESOLUTION_PRESET_1080p ||
      preset == CHIAKI_VIDEO_RESOLUTION_PRESET_720p) {
    if (was_downgraded)
      *was_downgraded = true;
    return CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
  }
  return preset;
}

VitaChiakiLatencyMode parse_latency_mode(const char *mode) {
  if (!mode)
    return VITA_LATENCY_MODE_BALANCED;
  if (strcmp(mode, "ultra_low") == 0)
    return VITA_LATENCY_MODE_ULTRA_LOW;
  if (strcmp(mode, "low") == 0)
    return VITA_LATENCY_MODE_LOW;
  if (strcmp(mode, "high") == 0)
    return VITA_LATENCY_MODE_HIGH;
  if (strcmp(mode, "max") == 0)
    return VITA_LATENCY_MODE_MAX;
  return VITA_LATENCY_MODE_BALANCED;
}

const char *serialize_latency_mode(VitaChiakiLatencyMode mode) {
  switch (mode) {
    case VITA_LATENCY_MODE_ULTRA_LOW: return "ultra_low";
    case VITA_LATENCY_MODE_LOW: return "low";
    case VITA_LATENCY_MODE_HIGH: return "high";
    case VITA_LATENCY_MODE_MAX: return "max";
    case VITA_LATENCY_MODE_BALANCED:
    default:
      return "balanced";
  }
}

bool get_circle_btn_confirm_default(void) {
  // Check system settings to see if circle should be select instead of cross.
  int button_assign = -1;
  int ret = sceRegMgrGetKeyInt("/CONFIG/SYSTEM", "button_assign", &button_assign);
  if (ret < 0)
    return false;
  // 0 => circle select; 1 => cross select.
  return (button_assign == 0);
}

const char *serialize_resolution_preset(ChiakiVideoResolutionPreset preset) {
  switch (preset) {
    case CHIAKI_VIDEO_RESOLUTION_PRESET_360p:
      return "360p";
    case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
      return "720p";
    case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p:
      return "1080p";
    case CHIAKI_VIDEO_RESOLUTION_PRESET_540p:
    default:
      return "540p";
  }
}
