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

/** The presets Quality Preset offers, lowest first. */
static const ChiakiVideoResolutionPreset RESOLUTION_STEPS[] = {
    CHIAKI_VIDEO_RESOLUTION_PRESET_360p,
    CHIAKI_VIDEO_RESOLUTION_PRESET_540p,
    CHIAKI_VIDEO_RESOLUTION_PRESET_720p,
    CHIAKI_VIDEO_RESOLUTION_PRESET_1080p,
};
#define RESOLUTION_STEP_COUNT ((int)(sizeof(RESOLUTION_STEPS) / sizeof(RESOLUTION_STEPS[0])))

ChiakiVideoResolutionPreset ui_settings_next_resolution(ChiakiVideoResolutionPreset current,
                                                        int step) {
  int index = 1; /* an unsupported value is shown as 540p (RESOLUTION_STEPS[1]) */
  for (int i = 0; i < RESOLUTION_STEP_COUNT; i++) {
    if (RESOLUTION_STEPS[i] == current)
      index = i;
  }
  return RESOLUTION_STEPS[(index + step + RESOLUTION_STEP_COUNT) % RESOLUTION_STEP_COUNT];
}

void ui_settings_apply_circle_confirm(void) {
  ui_input_block_button(context.ui_state.button_state & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE));
}
