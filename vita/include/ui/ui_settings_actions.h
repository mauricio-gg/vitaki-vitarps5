/**
 * @file ui_settings_actions.h
 * @brief Config changes shared by the Settings screens
 *
 * The rules a setting follows when it changes (saving the config, applying it to a running
 * session) live here once, so every screen that changes a setting follows the same rules.
 */

#pragma once

#include "config.h"

/** ui_settings_persist_config() - Save the config to disk; logs an error when saving fails. */
void ui_settings_persist_config(void);

/**
 * ui_settings_next_resolution() - The quality preset one press of Quality Preset leads to.
 * @current: The stored preset.
 *
 * 360p goes to 540p and 540p to 360p. A legacy 720p or 1080p value (shown as 540p) goes to 540p,
 * and any unknown value goes to 360p, exactly as the old screen did.
 */
ChiakiVideoResolutionPreset ui_settings_next_resolution(ChiakiVideoResolutionPreset current);

/**
 * ui_settings_apply_force_30fps() - Apply the Force 30 FPS Output setting to a running session.
 * Does nothing when no session is initialised.
 */
void ui_settings_apply_force_30fps(void);

/**
 * ui_settings_apply_circle_confirm() - Make the Circle Button Confirm setting take effect: the
 * legacy button names follow the config (the UiInput snapshot already reads the config every
 * frame). When a face button is held, it is blocked until released, so the press that flipped the
 * setting is not read again as the swapped button. Also called once at startup, to set the names
 * from the loaded config.
 */
void ui_settings_apply_circle_confirm(void);
