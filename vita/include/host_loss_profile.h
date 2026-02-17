#pragma once

#include <stdint.h>

#include "config.h"

typedef struct {
  uint64_t window_us;
  uint32_t min_frames;
  uint32_t event_threshold;
  uint32_t frame_threshold;
  uint64_t burst_window_us;
  uint32_t burst_frame_threshold;
} LossDetectionProfile;

unsigned int host_latency_mode_target_kbps(VitaChiakiLatencyMode mode);
uint32_t host_clamp_u32(uint32_t value, uint32_t min_value, uint32_t max_value);
uint32_t host_saturating_add_u32_report(uint32_t lhs,
                                        uint32_t rhs,
                                        const char *counter_name,
                                        uint32_t counter_mask_bit);
LossDetectionProfile host_loss_profile_for_mode(VitaChiakiLatencyMode mode);
void host_adjust_loss_profile_with_metrics(LossDetectionProfile *profile);
