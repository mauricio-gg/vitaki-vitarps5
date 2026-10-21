/**
 * @file ui_wifi_level.h
 * @brief How many bars the top bar Wi-Fi icon shows for a signal reading (SPEC.md C23, #428)
 *
 * Pure: plain numbers in, a level out, no SDK dependency, so the rule can be checked natively.
 */

#pragma once

/** Number of lit-bar levels: 0 to UI_WIFI_LEVEL_MAX. */
#define UI_WIFI_LEVEL_MAX 3

/** Passed as the previous level when there is none (first reading, or the link was down). */
#define UI_WIFI_LEVEL_NONE (-1)

/** Lowest RSSI percentage that enters level 3, 2 and 1. Below the level 1 entry the level is 0. */
#define UI_WIFI_ENTER_3_PCT 60
#define UI_WIFI_ENTER_2_PCT 45
#define UI_WIFI_ENTER_1_PCT 25

/** A level is left only this many points below its entry, so a reading on a border cannot flicker.
 */
#define UI_WIFI_HYSTERESIS_PCT 5

/**
 * ui_wifi_level() - Bars to show for a signal reading.
 * @rssi_percent: Signal strength, 0 to 100.
 * @prev_level:   Level shown at the last reading, or UI_WIFI_LEVEL_NONE.
 * Return: 0 to UI_WIFI_LEVEL_MAX. Rising uses the entry thresholds; falling holds the previous
 * level until the reading is UI_WIFI_HYSTERESIS_PCT below that level's entry. A reading may jump
 * several levels at once. Without a previous level the entry thresholds alone decide.
 */
int ui_wifi_level(int rssi_percent, int prev_level);
