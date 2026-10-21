/**
 * @file ui_wifi_level.c
 * @brief Wi-Fi bar level rule with hysteresis (SPEC.md C23, #428)
 */

#include "ui/ui_wifi_level.h"

/** Entry threshold of each level, indexed by level; level 0 has no entry (always reachable). */
static const int ENTER_PCT[UI_WIFI_LEVEL_MAX + 1] = {0, UI_WIFI_ENTER_1_PCT, UI_WIFI_ENTER_2_PCT,
                                                     UI_WIFI_ENTER_3_PCT};

int ui_wifi_level(int rssi_percent, int prev_level) {
  int level = UI_WIFI_LEVEL_MAX;
  while (level > 0 && rssi_percent < ENTER_PCT[level])
    level--;

  if (prev_level < 0 || level >= prev_level)
    return level;

  /* Falling: keep the previous level until the reading is a margin below its entry. */
  int held = prev_level > UI_WIFI_LEVEL_MAX ? UI_WIFI_LEVEL_MAX : prev_level;
  while (held > 0 && rssi_percent < ENTER_PCT[held] - UI_WIFI_HYSTERESIS_PCT)
    held--;
  return held;
}
