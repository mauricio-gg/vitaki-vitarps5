/**
 * @file ui_value_labels.h
 * @brief Words for config values and connection facts, shared by every screen that shows them
 *
 * The Settings and Profile screens and the Home detail panel (SPEC.md C04) show the same
 * values in the same words; the rules for them live here once.
 */

#pragma once

#include "config.h"
#include "host.h"
#include "ui/ui_console_status.h"

/** ui_label_resolution() - "360p" or "540p" (legacy values show as the Vita preset they use). */
const char *ui_label_resolution(ChiakiVideoResolutionPreset preset);

/** ui_label_fps() - "30 FPS" or "60 FPS". */
const char *ui_label_fps(ChiakiVideoFPSPreset preset);

/** ui_label_latency_mode() - The mode name with its bitrate, e.g. "Balanced (≈2.6 Mbps)". */
const char *ui_label_latency_mode(VitaChiakiLatencyMode mode);

/** ui_label_on_off() - "On" or "Off". */
const char *ui_label_on_off(bool on);

/** ui_label_background_blur() - "None", "Soft", "Strong" or "Dark". */
const char *ui_label_background_blur(VitaChiakiBackgroundBlur blur);

/** ui_label_background() - "Waves" or "Glyphs" (Waves when out of range). */
const char *ui_label_background(VitaChiakiBackground background);

/** ui_label_theme() - "Ocean", "Ember", "Orchid", "Moss" or "Graphite" (Ocean when out of range).
 */
const char *ui_label_theme(VitaChiakiTheme theme);

/**
 * ui_profile_reference_host() - The console the Profile connection facts describe.
 * @return the active host while streaming, else the console selected on Home, else the first
 *         known console; NULL when there are none
 */
VitaChiakiHost *ui_profile_reference_host(void);

/**
 * ui_connection_words() - The Network Type and Status words for a console (SPEC.md 3.7).
 * @host: May be NULL ("Unavailable" and "None").
 *
 * Reads the host's facts and hands them to ui_console_connection_words(), the one rule.
 */
UiConnectionWords ui_connection_words(const VitaChiakiHost *host);

/**
 * ui_connection_console_name() - The display name, else the hostname, else "Not selected".
 * @host: May be NULL.
 */
const char *ui_connection_console_name(const VitaChiakiHost *host);

/**
 * ui_connection_console_ip() - The console's address, or NULL when none is known.
 * @host: May be NULL.
 */
const char *ui_connection_console_ip(const VitaChiakiHost *host);
