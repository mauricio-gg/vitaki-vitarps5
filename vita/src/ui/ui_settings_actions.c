/**
 * @file ui_settings_actions.c
 * @brief Config changes shared by the Settings screens
 */

#include "ui/ui_settings_actions.h"

#include "context.h"
#include "ui/ui_input.h"
#include "ui/ui_internal.h"

void ui_settings_persist_config(void) {
  config_serialize_async(&context.config);
}

ChiakiVideoResolutionPreset ui_settings_next_resolution(ChiakiVideoResolutionPreset current,
                                                        int step) {
  static const ChiakiVideoResolutionPreset ORDER[] = {
      CHIAKI_VIDEO_RESOLUTION_PRESET_360p, CHIAKI_VIDEO_RESOLUTION_PRESET_540p,
      CHIAKI_VIDEO_RESOLUTION_PRESET_720p, CHIAKI_VIDEO_RESOLUTION_PRESET_1080p};
  const int count = (int)(sizeof(ORDER) / sizeof(ORDER[0]));
  int index = 1; /* unknown values count as 540p, the default */
  for (int i = 0; i < count; i++) {
    if (ORDER[i] == current)
      index = i;
  }
  return ORDER[(index + (step < 0 ? -1 : 1) + count) % count];
}

void ui_settings_apply_force_30fps(void) {
  if (!context.stream.session_init)
    return;
  uint32_t clamp = context.stream.negotiated_fps ? context.stream.negotiated_fps : 60;
  if (context.config.force_30fps && clamp > 30)
    clamp = 30;
  context.stream.target_fps = clamp;
  context.stream.pacing_accumulator = 0;
}

void ui_settings_apply_circle_confirm(void) {
  ui_input_block_button(context.ui_state.button_state & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE));
}
