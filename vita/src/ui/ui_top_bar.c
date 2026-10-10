/**
 * @file ui_top_bar.c
 * @brief C23 TopBar (SPEC.md C23)
 */

#include "ui/ui_top_bar.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <vita2d.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/net/netctl.h>
#include <psp2/power.h>
#include <psp2/rtc.h>

#include "ui/ui_chrome_layout.h"
#include "ui/ui_component.h"
#include "ui/ui_internal.h"
#include "ui/ui_pill.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"

#define GLYPH_DIR "app0:/assets/icons/"
#define CLOCK_TEXT_MAX 8
#define PERCENT_TEXT_MAX 8
#define BANNER_TEXT_MAX 256
#define BANNER_REASON_MAX 128

static const char BANNER_PREFIX[] = "Streaming stopped: ";
static const char BANNER_SUFFIX[] = " - Please wait a few moments";
/** Widest clock the layout plans for; Roboto digits are equal width, so any time measures the same.
 */
static const char CLOCK_WIDEST[] = "00:00";

static vita2d_texture *s_wifi = NULL;
static vita2d_texture *s_battery = NULL;

/** Cached system state, refreshed every UI_TOPBAR_POLL_MS. */
static struct {
  uint64_t next_poll_us;
  bool wifi_up;
  bool battery_known;
  char clock_text[CLOCK_TEXT_MAX];
  char percent_text[PERCENT_TEXT_MAX];
  int clock_w;
  int percent_w;
} s_sys;

/** Cached banner: rebuilt only when the reason or the slot width changes. */
static struct {
  char reason[BANNER_REASON_MAX];
  char text[BANNER_TEXT_MAX];
  int slot_w;
  int pill_w;
  bool valid;
} s_banner;

void ui_top_bar_init(void) {
  s_wifi = ui_load_png_linear(GLYPH_DIR "wifi.png");
  s_battery = ui_load_png_linear(GLYPH_DIR "battery.png");
  s_sys.next_poll_us = 0;
  s_banner.valid = false;
}

/** Re-read Wi-Fi, battery and clock into the cache when the poll interval has passed. */
static void poll_system(void) {
  const uint64_t now_us = sceKernelGetProcessTimeWide();
  if (now_us < s_sys.next_poll_us)
    return;
  s_sys.next_poll_us = now_us + (uint64_t)UI_TOPBAR_POLL_MS * 1000ULL;

  int net_state = SCE_NETCTL_STATE_DISCONNECTED;
  s_sys.wifi_up = sceNetCtlInetGetState(&net_state) >= 0 && net_state == SCE_NETCTL_STATE_CONNECTED;

  const int percent = scePowerGetBatteryLifePercent();
  s_sys.battery_known = percent >= 0;
  if (s_sys.battery_known) {
    snprintf(s_sys.percent_text, sizeof(s_sys.percent_text), "%d%%", percent);
    s_sys.percent_w = ui_text_face_width(UI_FACE_T16, s_sys.percent_text);
  }

  SceDateTime time;
  if (sceRtcGetCurrentClockLocalTime(&time) >= 0) {
    snprintf(s_sys.clock_text, sizeof(s_sys.clock_text), "%02d:%02d", time.hour, time.minute);
    s_sys.clock_w = ui_text_face_width(UI_FACE_T20, s_sys.clock_text);
  }
}

/** UiMeasureFn for the banner reason: T16 width. */
static int measure_t16(const char *text, void *ctx) {
  (void)ctx;
  return ui_text_face_width(UI_FACE_T16, text);
}

/** Rebuild the cached banner text for @reason so the whole pill fits in @slot_w. */
static void build_banner(const char *reason, int slot_w) {
  const int fixed_w = UI_PILL_PAD * 2 + ui_text_face_width(UI_FACE_T16, BANNER_PREFIX) +
                      ui_text_face_width(UI_FACE_T16, BANNER_SUFFIX);
  char shortened[BANNER_REASON_MAX];
  ui_ellipsize_to_fit(reason, slot_w - fixed_w, measure_t16, NULL, shortened, sizeof(shortened));

  snprintf(s_banner.text, sizeof(s_banner.text), "%s%s%s", BANNER_PREFIX, shortened, BANNER_SUFFIX);
  snprintf(s_banner.reason, sizeof(s_banner.reason), "%s", reason);
  s_banner.slot_w = slot_w;
  s_banner.pill_w = ui_pill_width(UI_PILL_WARN, s_banner.text);
  s_banner.valid = true;
}

/** Draw the logo scaled to UI_TOPBAR_H high; returns its drawn width (0 without a logo). */
static int draw_logo(void) {
  if (!vita_rps5_logo)
    return 0;
  const float scale = (float)UI_TOPBAR_H / (float)vita2d_texture_get_height(vita_rps5_logo);
  vita2d_draw_texture_tint_scale(vita_rps5_logo, (float)UI_MARGIN_X, (float)UI_TOP_Y, scale, scale,
                                 ui_layer_color(UI_TEXT));
  return (int)((float)vita2d_texture_get_width(vita_rps5_logo) * scale + 0.5f);
}

/** Draw Wi-Fi, battery icon and percent, right-aligned so the group ends at @right. */
static void draw_status_group(int right) {
  const int icon_y = UI_TOP_Y + (UI_TOPBAR_H - UI_TOPBAR_ICON) / 2;
  int x = right;

  if (s_sys.battery_known) {
    x -= s_sys.percent_w;
    ui_text_draw_face_centered_v(UI_FACE_T16, x, UI_TOP_Y, UI_TOPBAR_H, UI_TEXT_2,
                                 s_sys.percent_text);
    x -= UI_TOPBAR_ICON_GAP + UI_TOPBAR_ICON;
    if (s_battery)
      vita2d_draw_texture_tint(s_battery, (float)x, (float)icon_y, ui_layer_color(UI_TEXT_2));
    x -= UI_TOPBAR_GAP;
  }
  x -= UI_TOPBAR_ICON;
  if (s_wifi) {
    vita2d_draw_texture_tint(
        s_wifi, (float)x, (float)icon_y,
        ui_layer_color(s_sys.wifi_up ? UI_TEXT_2
                                     : ui_color_scale_alpha(
                                           UI_TEXT_2, (float)UI_TOPBAR_OFFLINE_PCT / 100.0f)));
  }
}

void ui_top_bar_draw(const char *banner_reason) {
  poll_system();

  const int logo_w = draw_logo();
  const int clock_x = UI_CONTENT_RIGHT - s_sys.clock_w;
  ui_text_draw_face_centered_v(UI_FACE_T20, clock_x, UI_TOP_Y, UI_TOPBAR_H, UI_TEXT,
                               s_sys.clock_text);

  if (!banner_reason) {
    draw_status_group(clock_x - UI_TOPBAR_GAP);
    return;
  }

  /* The slot is sized against the widest clock so the banner never jumps between minutes. */
  const int widest_clock_x = UI_CONTENT_RIGHT - ui_text_face_width(UI_FACE_T20, CLOCK_WIDEST);
  const int slot_x = UI_MARGIN_X + logo_w + UI_TOPBAR_SLOT_PAD;
  const int slot_w = widest_clock_x - UI_TOPBAR_SLOT_PAD - slot_x;
  if (!s_banner.valid || s_banner.slot_w != slot_w || strcmp(s_banner.reason, banner_reason) != 0)
    build_banner(banner_reason, slot_w);

  ui_pill_draw(UI_PILL_WARN, slot_x + (slot_w - s_banner.pill_w) / 2,
               UI_TOP_Y + (UI_TOPBAR_H - UI_PILL_H) / 2, s_banner.pill_w, s_banner.text);
}
