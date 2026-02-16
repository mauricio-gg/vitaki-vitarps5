#pragma once

#include "config.h"

ChiakiVideoResolutionPreset parse_resolution_preset(const char *preset);
ChiakiVideoResolutionPreset normalize_resolution_for_vita(ChiakiVideoResolutionPreset preset,
                                                          bool *was_downgraded);
VitaChiakiLatencyMode parse_latency_mode(const char *mode);
const char *serialize_latency_mode(VitaChiakiLatencyMode mode);
bool get_circle_btn_confirm_default(void);
const char *serialize_resolution_preset(ChiakiVideoResolutionPreset preset);
