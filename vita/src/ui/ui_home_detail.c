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
#include "ui/ui_console_cards.h"
#include "ui/ui_console_rows.h"
#include "ui/ui_console_status.h"
#include "ui/ui_controller_rules.h"
#include "ui/ui_detail_panel.h"
#include "ui/ui_internal.h"
#include "ui/ui_pair_hosts.h"
#include "ui/ui_theme.h"
#include "ui/ui_value_labels.h"

static const char UNKNOWN_ADDRESS[] = "Unknown";
static const char NOT_SET[] = "Not Set";
static const char FILTER_TITLE[] = "Filter";
static const char FILTER_DESCRIPTION[] = "Find a console by name or IP address.";

static const char PAIR_TITLE[] = "Pair new device";
static const char PAIR_DESCRIPTION[] =
    "Link a PS5 or PS4 to this Vita. Turn the console on and join the same network.";
static const char PAIR_PAIRED_LABEL[] = "Paired";
static const char PAIR_FOUND_LABEL[] = "Found nearby";
static const char PAIR_FOUND_FORMAT[] = "%d unpaired";
static const char PAIR_SEARCHING[] = "Searching...";
static const char PAIR_NONE[] = "None";
static const char PAIR_DISCOVERY_OFF[] = "Discovery off";

/** Identities of the Filter and Pair new device panels for the rise animation; no console pointer
 * can equal them. */
#define FILTER_PANEL_KEY 1u
#define PAIR_PANEL_KEY 2u

/** Room for the Pair new device panel's "<N> consoles" and "<N> unpaired" values. */
#define PAIR_VALUE_MAX 24
static char s_pair_paired[PAIR_VALUE_MAX];
static char s_pair_found[PAIR_VALUE_MAX];

/** The content handed to the panel; rebuilt every frame from pointers, no allocation. */
static UiDetailContent s_content;

/** Zone counts shown for the focused preset, formatted only when the count changes. */
static char s_front_text[16];
static char s_rear_text[16];
static int s_front_zones = -1;
static int s_rear_zones = -1;

/** The Filter panel's count line, rewritten only when its inputs change. */
/** Room for the quoted filter text plus " found of " and two counts. */
#define FILTER_COUNT_TEXT_MAX (UI_FILTER_TEXT_MAX + 48)
static char s_filter_count[FILTER_COUNT_TEXT_MAX];
static char s_filter_count_text[UI_FILTER_TEXT_MAX];
static int s_filter_count_found = -1;
static int s_filter_count_total = -1;

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
 * The status message of a console, while it has not expired, in ERR for an Error and WARN for
 * Retrying (the classification the row's status line already shows, not the raw flag).
 */
static void set_console_message(const ConsoleCardInfo *card) {
  const UiConsoleMessage kind = ui_cards_message(card);
  if (kind == UI_CONSOLE_MESSAGE_NONE)
    return;
  s_content.message = card->host->status_hint;
  s_content.message_color = kind == UI_CONSOLE_MESSAGE_ERROR ? UI_ERR : UI_WARN;
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
    s_content.logo_kind = UI_TYPE_LOGO_PS5;
  } else {
    s_content.logo = img_ps4;
    s_content.logo_kind = UI_TYPE_LOGO_PS4;
  }
  set_console_message(card);

  add_row("Address", card->ip_address[0] ? card->ip_address : UNKNOWN_ADDRESS, 0);
  add_row("Route", ui_console_route_label(card->is_discovered, internet_ok), 0);
  add_row("Pairing", card->is_registered ? "Paired" : "Unpaired", 0);
}

/** The count line: "<N> consoles", or "\"<text>\": <N> found of <M>" while a filter is active. */
static const char *filter_count_text(void) {
  const bool active = ui_cards_is_filter_active();
  const char *text = active ? ui_cards_get_filter_text() : "";
  const int found = ui_cards_get_count();
  const int total = ui_cards_get_total_count();

  if (found != s_filter_count_found || total != s_filter_count_total ||
      strcmp(text, s_filter_count_text) != 0) {
    snprintf(s_filter_count_text, sizeof(s_filter_count_text), "%s", text);
    if (active)
      snprintf(s_filter_count, sizeof(s_filter_count), "\"%s\": %d found of %d", text, found,
               total);
    else
      snprintf(s_filter_count, sizeof(s_filter_count), "%d consoles", total);
    s_filter_count_found = found;
    s_filter_count_total = total;
  }
  return s_filter_count;
}

/** Fill s_content for the Filter item: title, what it does, and the count as a label-only row. */
static void build_filter(void) {
  s_content.kind = UI_DETAIL_LIST;
  s_content.key = FILTER_PANEL_KEY;
  s_content.title = FILTER_TITLE;
  s_content.description = FILTER_DESCRIPTION;
  add_row(filter_count_text(), NULL, 0);
}

/** Fill s_content for the Pair new device item: what it does, how many consoles are paired, and
 * what discovery has found (SPEC 3.1a). */
static void build_pair(void) {
  const int paired = ui_cards_get_total_count();
  const int found = ui_pair_hosts_count();
  const char *found_text = PAIR_NONE;

  snprintf(s_pair_paired, sizeof(s_pair_paired), paired == 1 ? "%d console" : "%d consoles",
           paired);
  switch (ui_pair_hosts_item_phase(found)) {
    case UI_PAIR_PHASE_FOUND:
      snprintf(s_pair_found, sizeof(s_pair_found), PAIR_FOUND_FORMAT, found);
      found_text = s_pair_found;
      break;
    case UI_PAIR_PHASE_SEARCHING:
      found_text = PAIR_SEARCHING;
      break;
    case UI_PAIR_PHASE_OFF:
      found_text = PAIR_DISCOVERY_OFF;
      break;
    case UI_PAIR_PHASE_NONE:
      break;
  }

  s_content.kind = UI_DETAIL_LIST;
  s_content.key = PAIR_PANEL_KEY;
  s_content.title = PAIR_TITLE;
  s_content.description = PAIR_DESCRIPTION;
  add_row(PAIR_PAIRED_LABEL, s_pair_paired, 0);
  add_row(PAIR_FOUND_LABEL, found_text, 0);
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
      break;
    case UI_SETTINGS_GROUP_DISPLAY:
      add_toggle_row("Show Latency", cfg->show_latency);
      add_toggle_row("Show Network Alerts", cfg->show_network_indicator);
      add_toggle_row("Show Exit Shortcut Hint", cfg->show_stream_exit_hint);
      add_toggle_row("Show Button Hints", cfg->show_button_hints);
      break;
    case UI_SETTINGS_GROUP_APPEARANCE:
      add_row("Background", ui_label_background(cfg->background), 0);
      add_row("Theme", ui_label_theme(cfg->theme), 0);
      add_row("Background Blur", ui_label_background_blur(config_background_blur(cfg)), 0);
      break;
    case UI_SETTINGS_GROUP_CONTROLS:
      add_toggle_row("Circle Button Confirm", cfg->circle_btn_confirm);
      break;
    case UI_SETTINGS_GROUP_ADVANCED:
      add_toggle_row("Clamp Soft Restart Bitrate", cfg->clamp_soft_restart_bitrate);
      add_toggle_row("Motion during loss (artifacts) (Experimental)", cfg->submit_on_missing_ref);
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
  int front = ui_controller_mapped_zones(map, UI_CTRL_SIDE_FRONT);
  int rear = ui_controller_mapped_zones(map, UI_CTRL_SIDE_REAR);

  add_row("L1", controller_output_name((VitakiCtrlOut)map->in_out_btn[VITAKI_CTRL_IN_L1]), 0);
  add_row("R1", controller_output_name((VitakiCtrlOut)map->in_out_btn[VITAKI_CTRL_IN_R1]), 0);
  add_row("Front touch", zones_text(s_front_text, sizeof(s_front_text), &s_front_zones, front), 0);
  add_row("Rear touch", zones_text(s_rear_text, sizeof(s_rear_text), &s_rear_zones, rear), 0);
}

/* ============================================================================
 * Profile
 * ============================================================================ */

/** Rows of the Connection group (SPEC 3.7's table). */
static void build_connection_rows(void) {
  const VitaChiakiHost *host = ui_profile_reference_host();
  const UiConnectionWords words = ui_connection_words(host);
  const char *ip = ui_connection_console_ip(host);

  add_row("Network Type", words.network_type, 0);
  add_row("Console", ui_connection_console_name(host), 0);
  if (ip)
    add_row("Console IP", ip, 0);
  add_row("Status", words.status, 0);
  add_row("Quality", ui_label_resolution(context.config.resolution), 0);
}

uint32_t ui_psn_auth_color(PsnAuthState state) {
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

  add_row("PSN Auth", psn_auth_state_label_for(state, now_unix), ui_psn_auth_color(state));
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

void ui_home_detail_draw(UiHomeDetailSource source, const UiXmbList *list, bool filter_row) {
  memset(&s_content, 0, sizeof(s_content));

  if (list->count > 0) {
    const UiXmbItem *item = &list->items[list->focus];
    if (source == UI_HOME_DETAIL_CONSOLES) {
      const int index =
          ui_console_rows_console_index(filter_row, list->focus, ui_cards_get_count());
      const ConsoleCardInfo *card = ui_cards_get_card(index);
      if (card)
        build_console(item, card);
      else if (list->focus == UI_CONSOLE_ROW_PAIR)
        build_pair();
      else if (filter_row && list->focus == UI_CONSOLE_ROW_FILTER)
        build_filter();
    } else {
      build_list_item(source, item, list->focus);
    }
  }
  ui_detail_panel_draw(&s_content);
}
