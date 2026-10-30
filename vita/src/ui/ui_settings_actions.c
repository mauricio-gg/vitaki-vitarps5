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

ChiakiVideoResolutionPreset ui_settings_next_resolution(ChiakiVideoResolutionPreset current) {
  switch (current) {
    case CHIAKI_VIDEO_RESOLUTION_PRESET_360p:
      return CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
    case CHIAKI_VIDEO_RESOLUTION_PRESET_540p:
      return CHIAKI_VIDEO_RESOLUTION_PRESET_360p;
    case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p:
    case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
      return CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
    default:
      return CHIAKI_VIDEO_RESOLUTION_PRESET_360p;
  }
}

void ui_settings_apply_circle_confirm(void) {
  ui_input_block_button(context.ui_state.button_state & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE));
}
