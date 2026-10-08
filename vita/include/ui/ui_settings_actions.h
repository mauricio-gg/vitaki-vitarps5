/**
 * @file ui_settings_actions.h
 * @brief Config changes shared by the Settings screens
 *
 * The rules a setting follows when it changes (saving the config, applying it to a running
 * session) live here once, so the old Settings screen and the XMB Settings page cannot drift.
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
