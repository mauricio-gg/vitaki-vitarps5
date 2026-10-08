/**
 * @file ui_home_detail.c
 * @brief The Home screen's detail panel content (SPEC.md C04, section 3.1)
 */

#include "ui/ui_home_detail.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "context.h"
#include "controller.h"
#include "host.h"
#include "psn_auth.h"
#include "ui/ui_animation.h"
#include "ui/ui_console_cards.h"
#include "ui/ui_console_status.h"
#include "ui/ui_detail_panel.h"
#include "ui/ui_internal.h"
#include "ui/ui_theme.h"
#include "ui/ui_value_labels.h"

static const char UNKNOWN_ADDRESS[] = "Unknown";
static const char NOT_SET[] = "Not Set";

/** The content handed to the panel; rebuilt every frame from pointers, no allocation. */
static UiDetailContent s_content;

/** Zone counts shown for the focused preset, formatted only when the count changes. */
static char s_front_text[16];
static char s_rear_text[16];
static int s_front_zones = -1;
static int s_rear_zones = -1;

/** Defaults a preset slot starts from while it has never been saved. */
static ControllerMapStorage s_default_map;
static bool s_default_map_ready = false;

/* ============================================================================
 * Rows
 * ============================================================================ */

/** Append a label and value row; a full panel ignores further rows. */
static void add_row(const char *label, const char *value, uint32_t value_color) {
  if (s_content.row_count >= UI_DETAIL_MAX_ROWS)
    return;
  s_content.rows[s_content.row_count++] = (UiDetailRow){label, value, value_color};
}

static void add_toggle_row(const char *label, bool on) {
  add_row(label, ui_label_on_off(on), 0);
}

/* ============================================================================
 * Console
 * ============================================================================ */

/**
 * The status message of a console: the hint the old cards showed under the status, while it
 * has not expired. Error versus Retrying is ticket #301; until then a hint flagged as an
 * error is ERR and any other is WARN.
 */
static void set_console_message(const VitaChiakiHost *host) {
  if (!host || !host->status_hint[0])
    return;
  if (host->status_hint_expire_us != 0 && ui_anim_now_us() > host->status_hint_expire_us)
    return;
  s_content.message = host->status_hint;
  s_content.message_color = host->status_hint_is_error ? UI_ERR : UI_WARN;
}

/** Fill s_content for the console @card whose list row is @item. */
static void build_console(const UiXmbItem *item, const ConsoleCardInfo *card) {
  const bool internet_ok = card->has_internet && psn_auth_token_is_valid((uint64_t)time(NULL));

  s_content.kind = UI_DETAIL_CONSOLE;
  s_content.key = (uint32_t)(uintptr_t)card->host;
  s_content.title = card->name;
  s_content.status = item->status;
  s_content.status_color = item->status_color;
  if (card->host && chiaki_target_is_ps5(card->host->target)) {
    s_content.logo = ps5_logo;
    s_content.logo_kind = UI_DETAIL_LOGO_PS5;
  } else {
    s_content.logo = img_ps4;
    s_content.logo_kind = UI_DETAIL_LOGO_PS4;
  }
  set_console_message(card->host);

  add_row("Address", card->ip_address[0] ? card->ip_address : UNKNOWN_ADDRESS, 0);
  add_row("Route", ui_console_route_label(card->is_discovered, internet_ok), 0);
  add_row("Pairing", card->is_registered ? "Paired" : "Unpaired", 0);
}

/* ============================================================================
 * Settings
 * ============================================================================ */

/** Fill the rows of a Settings group from the config (SPEC 3.6). */
static void build_settings_rows(int group) {
  const VitaChiakiConfig *cfg = &context.config;
  switch (group) {
    case UI_SETTINGS_GROUP_VIDEO:
      add_row("Quality Preset", ui_label_resolution(cfg->resolution), 0);
      add_row("Latency Mode", ui_label_latency_mode(cfg->latency_mode), 0);
      add_row("FPS Target", ui_label_fps(cfg->fps), 0);
      add_toggle_row("Force 30 FPS Output", cfg->force_30fps);
      add_toggle_row("Fill Screen", cfg->stretch_video);
      break;
    case UI_SETTINGS_GROUP_NETWORK:
      add_toggle_row("Auto Discovery", cfg->auto_discovery);
      add_toggle_row("Enable PSN Internet Mode", cfg->psn_remoteplay_enabled);
      add_toggle_row("Show Only Paired", cfg->show_only_paired);
      break;
    case UI_SETTINGS_GROUP_DISPLAY:
      add_toggle_row("Show Latency", cfg->show_latency);
      add_toggle_row("Show Network Alerts", cfg->show_network_indicator);
      add_toggle_row("Show Exit Shortcut Hint", cfg->show_stream_exit_hint);
      break;
    case UI_SETTINGS_GROUP_CONTROLS:
      add_toggle_row("Circle Button Confirm", cfg->circle_btn_confirm);
      break;
    case UI_SETTINGS_GROUP_ADVANCED:
      add_toggle_row("Clamp Soft Restart Bitrate", cfg->clamp_soft_restart_bitrate);
      add_toggle_row("Motion during loss (Experimental)", cfg->submit_on_missing_ref);
      add_toggle_row("Enable Logging", cfg->logging.enabled);
      break;
    default:
      break;
  }
}

/* ============================================================================
 * Controller preset
 * ============================================================================ */

/** The mapping of preset @slot: its saved map, or the defaults it starts from. */
static const ControllerMapStorage *preset_map(int slot) {
  if (context.config.custom_maps_valid[slot])
    return &context.config.custom_maps[slot];
  if (!s_default_map_ready) {
    controller_map_storage_set_defaults(&s_default_map);
    s_default_map_ready = true;
  }
  return &s_default_map;
}

/** Number of the @count inputs from @first that are mapped to something. */
static int mapped_zones(const ControllerMapStorage *map, int first, int count) {
  int mapped = 0;
  for (int i = 0; i < count; i++) {
    if (map->in_out_btn[first + i] != VITAKI_CTRL_OUT_NONE)
      mapped++;
  }
  return mapped;
}

/** Rewrite @text as "N zones" when @zones differs from the count it shows. */
static const char *zones_text(char *text, size_t size, int *shown, int zones) {
  if (*shown != zones) {
    snprintf(text, size, zones == 1 ? "%d zone" : "%d zones", zones);
    *shown = zones;
  }
  return text;
}

/** Fill the rows of controller preset @slot: shoulder buttons and touch zones. */
static void build_preset_rows(int slot) {
  const ControllerMapStorage *map = preset_map(slot);
  int front =
      mapped_zones(map, VITAKI_CTRL_IN_FRONTTOUCH_GRID_START, VITAKI_CTRL_IN_FRONTTOUCH_GRID_COUNT);
  int rear =
      mapped_zones(map, VITAKI_CTRL_IN_REARTOUCH_GRID_START, VITAKI_CTRL_IN_REARTOUCH_GRID_COUNT);

  add_row("L1", controller_output_name((VitakiCtrlOut)map->in_out_btn[VITAKI_CTRL_IN_L1]), 0);
  add_row("R1", controller_output_name((VitakiCtrlOut)map->in_out_btn[VITAKI_CTRL_IN_R1]), 0);
  add_row("Front touch", zones_text(s_front_text, sizeof(s_front_text), &s_front_zones, front), 0);
  add_row("Rear touch", zones_text(s_rear_text, sizeof(s_rear_text), &s_rear_zones, rear), 0);
}

/* ============================================================================
 * Profile
 * ============================================================================ */

/** The status word of the reference console, on Home's words (SPEC 3.7), or "None". */
static const char *connection_status(const VitaChiakiHost *host) {
  if (!host)
    return "None";
  ConsoleCardInfo card;
  ui_cards_map_host((VitaChiakiHost *)host, &card);
  UiConsoleState state =
      ui_cards_classify(&card, psn_auth_token_is_valid((uint64_t)time(NULL)), false);
  return ui_console_status_label(state.status);
}

/** Rows of the Connection group: the first four of SPEC 3.7's table. */
static void build_connection_rows(void) {
  const VitaChiakiHost *host = ui_profile_reference_host();
  const char *ip = ui_connection_console_ip(host);

  add_row("Network Type", ui_connection_network_type(host), 0);
  add_row("Console", ui_connection_console_name(host), 0);
  if (ip)
    add_row("Console IP", ip, 0);
  add_row("Status", connection_status(host), 0);
  add_row("Quality", ui_label_resolution(context.config.resolution), 0);
}

/** Colour of the PSN Auth value: green when signed in, red for the states that need action. */
static uint32_t psn_auth_color(PsnAuthState state) {
  switch (state) {
    case PSN_AUTH_STATE_TOKEN_VALID:
      return UI_OK;
    case PSN_AUTH_STATE_TOKEN_REFRESHING:
      return UI_WARN;
    case PSN_AUTH_STATE_LOGGED_OUT:
    case PSN_AUTH_STATE_ERROR:
      return UI_ERR;
    default:
      return 0;
  }
}

/** Rows of the PlayStation Network group: the auth status, then the actions it offers. */
static void build_psn_rows(void) {
  const uint64_t now_unix = (uint64_t)time(NULL);
  const PsnAuthState state = psn_auth_state(now_unix);

  add_row("PSN Auth", psn_auth_state_label_for(state, now_unix), psn_auth_color(state));
  switch (state) {
    case PSN_AUTH_STATE_TOKEN_VALID:
      add_row("Refresh hosts", NULL, 0);
      add_row("Log out", NULL, 0);
      break;
    case PSN_AUTH_STATE_TOKEN_REFRESHING:
    case PSN_AUTH_STATE_DEVICE_LOGIN_PENDING:
    case PSN_AUTH_STATE_DEVICE_LOGIN_POLLING:
      break;
    default:
      add_row("Log in", NULL, 0);
      break;
  }
}

/** Fill the rows of a Profile group (SPEC 3.7). */
static void build_profile_rows(int group) {
  switch (group) {
    case UI_PROFILE_GROUP_ACCOUNT:
      add_row("Account ID", context.config.psn_account_id ? context.config.psn_account_id : NOT_SET,
              0);
      add_row("Refresh Account ID", NULL, 0);
      break;
    case UI_PROFILE_GROUP_CONNECTION:
      build_connection_rows();
      break;
    case UI_PROFILE_GROUP_PSN:
      build_psn_rows();
      break;
    default:
      break;
  }
}

/* ============================================================================
 * Entry point
 * ============================================================================ */

/** Fill s_content for a Settings, Controller or Profile item. */
static void build_list_item(UiHomeDetailSource source, const UiXmbItem *item, int index) {
  s_content.kind = UI_DETAIL_LIST;
  s_content.key = ((uint32_t)source << 8) | (uint32_t)index;
  s_content.title = item->name;

  switch (source) {
    case UI_HOME_DETAIL_SETTINGS:
      build_settings_rows(index);
      break;
    case UI_HOME_DETAIL_CONTROLLER:
      s_content.description = item->status;
      if (index < UI_CONTROLLER_PRESET_COUNT)
        build_preset_rows(index);
      break;
    case UI_HOME_DETAIL_PROFILE:
      build_profile_rows(index);
      break;
    default:
      break;
  }
}

void ui_home_detail_draw(UiHomeDetailSource source, const UiXmbList *list) {
  memset(&s_content, 0, sizeof(s_content));

  if (list->count > 0) {
    const UiXmbItem *item = &list->items[list->focus];
    if (source == UI_HOME_DETAIL_CONSOLES) {
      const ConsoleCardInfo *card = ui_cards_get_card(list->focus);
      if (card)
        build_console(item, card);
    } else {
      build_list_item(source, item, list->focus);
    }
  }
  ui_detail_panel_draw(&s_content);
}
