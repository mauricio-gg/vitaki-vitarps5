#include "context.h"
#include "host_loss_profile.h"

#include <limits.h>

#define LOSS_EVENT_WINDOW_DEFAULT_US (8 * 1000 * 1000ULL)
#define LOSS_EVENT_MIN_FRAMES_DEFAULT 4
#define LOSS_EVENT_THRESHOLD_DEFAULT 3
#define LOSS_PROFILE_BURST_BASE_US (200 * 1000ULL)
#define LOSS_PROFILE_BURST_LOW_US (220 * 1000ULL)
#define LOSS_PROFILE_BURST_BALANCED_US (240 * 1000ULL)
#define LOSS_PROFILE_BURST_HIGH_US (260 * 1000ULL)
#define LOSS_PROFILE_BURST_MAX_US (280 * 1000ULL)
#define LOSS_PROFILE_WINDOW_LOW_US (5 * 1000 * 1000ULL)
#define LOSS_PROFILE_WINDOW_BALANCED_US (7 * 1000 * 1000ULL)
#define LOSS_PROFILE_WINDOW_HIGH_US (9 * 1000 * 1000ULL)
#define LOSS_PROFILE_WINDOW_MAX_US (10 * 1000 * 1000ULL)

static uint32_t host_saturating_add_u32(uint32_t lhs, uint32_t rhs) {
  if (lhs > UINT32_MAX - rhs)
    return UINT32_MAX;
  return lhs + rhs;
}

unsigned int host_latency_mode_target_kbps(VitaChiakiLatencyMode mode) {
  switch (mode) {
    case VITA_LATENCY_MODE_ULTRA_LOW: return 1200;
    case VITA_LATENCY_MODE_LOW: return 1800;
    case VITA_LATENCY_MODE_HIGH: return 3200;
    case VITA_LATENCY_MODE_MAX: return 3800;
    case VITA_LATENCY_MODE_BALANCED:
    default:
      return 2600;
  }
}

uint32_t host_clamp_u32(uint32_t value, uint32_t min_value, uint32_t max_value) {
  if (value < min_value)
    return min_value;
  if (value > max_value)
    return max_value;
  return value;
}

uint32_t host_saturating_add_u32_report(uint32_t lhs,
                                        uint32_t rhs,
                                        const char *counter_name,
                                        uint32_t counter_mask_bit) {
  uint32_t sum = host_saturating_add_u32(lhs, rhs);
  if (sum == UINT32_MAX && lhs != UINT32_MAX &&
      !(context.stream.loss_counter_saturated_mask & counter_mask_bit)) {
    LOGE("Loss accumulator '%s' saturated at UINT32_MAX; forcing recovery reset path",
         counter_name ? counter_name : "unknown");
    context.stream.loss_counter_saturated_mask |= counter_mask_bit;
  }
  return sum;
}

LossDetectionProfile host_loss_profile_for_mode(VitaChiakiLatencyMode mode) {
  LossDetectionProfile profile = {
      .window_us = LOSS_EVENT_WINDOW_DEFAULT_US,
      .min_frames = LOSS_EVENT_MIN_FRAMES_DEFAULT,
      .event_threshold = LOSS_EVENT_THRESHOLD_DEFAULT,
      .frame_threshold = 10,
      .burst_window_us = LOSS_PROFILE_BURST_BASE_US,
      .burst_frame_threshold = 4};

  switch (mode) {
    case VITA_LATENCY_MODE_ULTRA_LOW:
      profile.window_us = LOSS_PROFILE_WINDOW_LOW_US;
      profile.min_frames = 4;
      profile.event_threshold = 2;
      profile.frame_threshold = 6;
      profile.burst_window_us = LOSS_PROFILE_BURST_LOW_US;
      profile.burst_frame_threshold = 6;
      break;
    case VITA_LATENCY_MODE_LOW:
      profile.window_us = LOSS_PROFILE_WINDOW_BALANCED_US;
      profile.min_frames = 4;
      profile.event_threshold = 3;
      profile.frame_threshold = 8;
      profile.burst_window_us = LOSS_PROFILE_BURST_BALANCED_US;
      profile.burst_frame_threshold = 5;
      break;
    case VITA_LATENCY_MODE_BALANCED:
    default:
      profile.window_us = LOSS_EVENT_WINDOW_DEFAULT_US;
      profile.min_frames = LOSS_EVENT_MIN_FRAMES_DEFAULT;
      profile.event_threshold = LOSS_EVENT_THRESHOLD_DEFAULT;
      profile.frame_threshold = 9;
      profile.burst_window_us = LOSS_PROFILE_BURST_LOW_US;
      profile.burst_frame_threshold = 5;
      break;
    case VITA_LATENCY_MODE_HIGH:
      profile.window_us = LOSS_PROFILE_WINDOW_HIGH_US;
      profile.min_frames = 5;
      profile.event_threshold = 3;
      profile.frame_threshold = 11;
      profile.burst_window_us = LOSS_PROFILE_BURST_HIGH_US;
      profile.burst_frame_threshold = 6;
      break;
    case VITA_LATENCY_MODE_MAX:
      profile.window_us = LOSS_PROFILE_WINDOW_MAX_US;
      profile.min_frames = 6;
      profile.event_threshold = 4;
      profile.frame_threshold = 13;
      profile.burst_window_us = LOSS_PROFILE_BURST_MAX_US;
      profile.burst_frame_threshold = 7;
      break;
  }

  return profile;
}

void host_adjust_loss_profile_with_metrics(LossDetectionProfile *profile) {
  if (!profile)
    return;

  if (context.config.latency_mode == VITA_LATENCY_MODE_ULTRA_LOW &&
      context.stream.loss_retry_attempts == 0 && profile->event_threshold > 1) {
    profile->event_threshold--;
  }

  float target_mbps =
      (float)host_latency_mode_target_kbps(context.config.latency_mode) / 1000.0f;
  float measured_mbps = context.stream.measured_bitrate_mbps;
  bool bitrate_known = measured_mbps > 0.01f && target_mbps > 0.0f;
  const uint64_t window_step = 2 * 1000 * 1000ULL;

  if (bitrate_known) {
    if (measured_mbps <= target_mbps * 0.85f) {
      profile->event_threshold =
          host_clamp_u32(profile->event_threshold + 1, 1, 6);
      profile->min_frames = host_clamp_u32(profile->min_frames + 1, 2, 8);
      profile->frame_threshold =
          host_clamp_u32(profile->frame_threshold + 2, 4, 24);
      profile->burst_frame_threshold =
          host_clamp_u32(profile->burst_frame_threshold + 1, 3, 16);
      profile->window_us += window_step;
    } else if (measured_mbps >= target_mbps * 1.2f) {
      if (profile->event_threshold > 1)
        profile->event_threshold--;
      if (profile->min_frames > 2)
        profile->min_frames--;
      if (profile->frame_threshold > 4)
        profile->frame_threshold -= 2;
      if (profile->burst_frame_threshold > 3)
        profile->burst_frame_threshold--;
      if (profile->window_us > window_step)
        profile->window_us -= window_step;
      if (profile->burst_window_us > 100 * 1000ULL)
        profile->burst_window_us -= 50 * 1000ULL;
    }
  }

  uint32_t measured_fps = context.stream.measured_incoming_fps ?
      context.stream.measured_incoming_fps : context.stream.negotiated_fps;
  uint32_t clamp_target = context.stream.target_fps ?
      context.stream.target_fps : context.stream.negotiated_fps;
  if (measured_fps && clamp_target && measured_fps <= clamp_target) {
    profile->event_threshold =
        host_clamp_u32(profile->event_threshold + 1, 1, 6);
    profile->frame_threshold =
        host_clamp_u32(profile->frame_threshold + 1, 4, 24);
    profile->burst_frame_threshold =
        host_clamp_u32(profile->burst_frame_threshold + 1, 3, 16);
  }
}
