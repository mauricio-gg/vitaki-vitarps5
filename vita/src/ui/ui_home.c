/**
 * @file ui_home.c
 * @brief The XMB Home screen (SPEC.md section 3.1)
 *
 * One CategoryBar (Consoles / Settings / Controller / Profile) over one XmbList.
 * The list is rebuilt from its source every frame into a fixed array (no heap):
 * the console card cache for Consoles, static tables for the other categories.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <vita2d.h>
#include <psp2/kernel/processmgr.h>

#include "context.h"
#include "host.h"
#include "psn_auth.h"
#include "ui/ui_home.h"
#include "ui/ui_internal.h"
#include "ui/ui_background.h"
#include "ui/ui_category_bar.h"
#include "ui/ui_components.h"
#include "ui/ui_component.h"
#include "ui/ui_console_status.h"
#include "ui/ui_input.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"
#include "ui/ui_xmb_list.h"

/* ============================================================================
 * Categories and static items
 * ============================================================================ */

typedef enum home_category_t {
  HOME_CAT_CONSOLES = 0,
  HOME_CAT_SETTINGS,
  HOME_CAT_CONTROLLER,
  HOME_CAT_PROFILE,
} HomeCategory;

/** Item icons, in the order of HOME_ICON_FILES. */
typedef enum home_icon_t {
  HOME_ICON_TV = 0,
  HOME_ICON_VIDEO,
  HOME_ICON_NETWORK,
  HOME_ICON_DISPLAY,
  HOME_ICON_CONTROLS,
  HOME_ICON_ADVANCED,
  HOME_ICON_ACCOUNT,
  HOME_ICON_CONNECTION,
  HOME_ICON_PSN,
  HOME_ICON_SLOT1,
  HOME_ICON_SLOT2,
  HOME_ICON_SLOT3,
  HOME_ICON_COUNT,
} HomeIcon;

#define HOME_ICON_DIR "app0:/assets/icons/"

static const char *const HOME_ICON_FILES[HOME_ICON_COUNT] = {
    "tv",      "video",      "network", "display", "controls", "advanced",
    "account", "connection", "psn",     "slot1",   "slot2",    "slot3",
};

/** A non-console row: name, optional second line, icon. */
typedef struct home_entry_t {
  const char *name;
  const char *detail;
  HomeIcon icon;
} HomeEntry;

static const HomeEntry SETTINGS_ENTRIES[] = {
    {"Video", NULL, HOME_ICON_VIDEO},       {"Network", NULL, HOME_ICON_NETWORK},
    {"Display", NULL, HOME_ICON_DISPLAY},   {"Controls", NULL, HOME_ICON_CONTROLS},
    {"Advanced", NULL, HOME_ICON_ADVANCED},
};

static const HomeEntry CONTROLLER_ENTRIES[] = {
    {"Custom 1", "Your first custom mapping", HOME_ICON_SLOT1},
    {"Custom 2", "Your second custom mapping", HOME_ICON_SLOT2},
    {"Custom 3", "Your third custom mapping", HOME_ICON_SLOT3},
};

static const HomeEntry PROFILE_ENTRIES[] = {
    {"Account", "PSN Account ID", HOME_ICON_ACCOUNT},
    {"Connection", "Network, console and stream", HOME_ICON_CONNECTION},
    {"PlayStation Network", NULL, HOME_ICON_PSN},
};

static const char *const CATEGORY_LABELS[UI_CAT_COUNT] = {"Consoles", "Settings", "Controller",
                                                          "Profile"};

/** The existing (old) screen that Confirm opens for each category's items. */
static const UIScreenType CATEGORY_SCREENS[UI_CAT_COUNT] = {
    UI_SCREEN_TYPE_MAIN, UI_SCREEN_TYPE_SETTINGS, UI_SCREEN_TYPE_CONTROLLER,
    UI_SCREEN_TYPE_PROFILE};

/* Copy */
static const char STATUS_READY[] = "Ready";
static const char STATUS_STANDBY[] = "Standby";
static const char STATUS_UNPAIRED[] = "Unpaired";
static const char STATUS_UNAVAILABLE[] = "Unavailable";
static const char STATUS_COOLDOWN[] = "Please wait...";
static const char ROUTE_INTERNET[] = "\xC2\xB7 Internet";
static const char EMPTY_SEARCHING[] = "Searching for consoles...";
static const char EMPTY_NO_MATCH[] = "No consoles match filter";
static const char BANNER_FORMAT[] = "Streaming stopped: %s - Please wait a few moments";
static const char BANNER_DEFAULT_REASON[] = "Connection interrupted";
static const char FILTER_LINE_FORMAT[] = "Filter: \"%s\" (%d found) - Start to clear";
/* Interim Select toast text; the hint row replaces it. */
static const char SELECT_HINT[] = "L/R: Browse | Cross: Connect | Start: Filter";

/** Cooldown banner height: the top-bar band (TOP_Y above, 32 px bar). */
#define BANNER_H (UI_TOP_Y + UI_S4)
#define BANNER_TEXT_MAX 196
#define FILTER_LINE_MAX 96

/* ============================================================================
 * State
 * ============================================================================ */

static UiCategoryBar s_bar;
static UiXmbList s_list;
static UiXmbItem s_items[UI_LIST_MAX_ITEMS];
static vita2d_texture *s_item_icons[HOME_ICON_COUNT];
static int s_item_count = 0;

/** Confirm is being held on a console that offers both routes (long press opens the popup). */
static bool s_confirm_tracking = false;
static uint64_t s_confirm_start_us = 0;

/* ============================================================================
 * Setup
 * ============================================================================ */

void ui_home_init(void) {
  char path[96];
  for (int i = 0; i < HOME_ICON_COUNT; i++) {
    snprintf(path, sizeof(path), HOME_ICON_DIR "%s.png", HOME_ICON_FILES[i]);
    s_item_icons[i] = ui_load_png_linear(path);
  }

  vita2d_texture *controller = ui_load_png_linear(HOME_ICON_DIR "controller.png");
  vita2d_texture *profile = ui_load_png_linear(HOME_ICON_DIR "profile.png");
  vita2d_texture *category_icons[UI_CAT_COUNT] = {icon_play, icon_settings, controller, profile};

  ui_category_bar_init(&s_bar, category_icons, CATEGORY_LABELS);
  ui_xmb_list_init(&s_list);
  s_item_count = 0;
  s_confirm_tracking = false;
}

void ui_home_on_enter(void) {
  if (!ui_focus_has_modal())
    ui_focus_set_zone(FOCUS_ZONE_MAIN_CONTENT);
  ui_nav_reset_collapsed();
  s_confirm_tracking = false;
}

/* ============================================================================
 * Items
 * ============================================================================ */

/** Fill s_items from a static entry table. */
static int fill_static_items(const HomeEntry *entries, int count) {
  for (int i = 0; i < count; i++) {
    s_items[i] = (UiXmbItem){
        .icon = s_item_icons[entries[i].icon],
        .name = entries[i].name,
        .status = entries[i].detail,
        .status_color = UI_TEXT_2,
    };
  }
  return count;
}

/** Row appearance for a classified console. */
static UiXmbItem console_item(const ConsoleCardInfo *card, UiConsoleState state) {
  UiXmbItem item = {
      .icon = s_item_icons[HOME_ICON_TV],
      .name = card->name,
      .status_dot = true,
      .route = state.internet_route ? ROUTE_INTERNET : NULL,
  };

  switch (state.status) {
    case UI_CONSOLE_READY:
      item.status = STATUS_READY;
      item.status_color = UI_OK;
      break;
    case UI_CONSOLE_STANDBY:
      item.status = STATUS_STANDBY;
      item.status_color = UI_WARN;
      break;
    case UI_CONSOLE_UNPAIRED:
      item.status = STATUS_UNPAIRED;
      item.status_color = UI_IDLE;
      item.dim_icon = true;
      break;
    case UI_CONSOLE_UNAVAILABLE:
      item.status = STATUS_UNAVAILABLE;
      item.status_color = UI_TEXT_3;
      item.dim_icon = true;
      break;
    case UI_CONSOLE_COOLDOWN:
      item.status = STATUS_COOLDOWN;
      item.status_color = UI_WARN;
      item.dim_row = true;
      break;
  }
  return item;
}

/** Fill s_items from the console cache; @cooldown_host is the console in cooldown, if any. */
static int fill_console_items(const VitaChiakiHost *cooldown_host) {
  const bool token_ok = psn_auth_token_is_valid((uint64_t)time(NULL));
  const int count =
      ui_cards_get_count() < UI_LIST_MAX_ITEMS ? ui_cards_get_count() : UI_LIST_MAX_ITEMS;

  for (int i = 0; i < count; i++) {
    const ConsoleCardInfo *card = ui_cards_get_card(i);
    UiConsoleState state = ui_console_classify(
        card->is_registered, card->is_discovered, card->state == 2 /* Standby */,
        card->has_internet && token_ok, cooldown_host && card->host == cooldown_host);
    s_items[i] = console_item(card, state);
  }
  return count;
}

/** Rebuild s_items for the focused category and hand them to the list. */
static void refresh_items(const VitaChiakiHost *cooldown_host) {
  switch (s_bar.focus) {
    case HOME_CAT_SETTINGS:
      s_item_count = fill_static_items(
          SETTINGS_ENTRIES, (int)(sizeof(SETTINGS_ENTRIES) / sizeof(SETTINGS_ENTRIES[0])));
      break;
    case HOME_CAT_CONTROLLER:
      s_item_count = fill_static_items(
          CONTROLLER_ENTRIES, (int)(sizeof(CONTROLLER_ENTRIES) / sizeof(CONTROLLER_ENTRIES[0])));
      break;
    case HOME_CAT_PROFILE:
      s_item_count = fill_static_items(PROFILE_ENTRIES,
                                       (int)(sizeof(PROFILE_ENTRIES) / sizeof(PROFILE_ENTRIES[0])));
      break;
    default:
      s_item_count = fill_console_items(cooldown_host);
      break;
  }
  ui_xmb_list_set_items(&s_list, s_items, s_item_count);
}

/* ============================================================================
 * Cooldown
 * ============================================================================ */

/**
 * cooldown_host_and_banner() - Draw the cooldown banner while the stream cooldown is active.
 *
 * Interim: the banner moves into the top bar (C23) with the top-bar ticket.
 *
 * @return the console in cooldown, or NULL when no cooldown is active
 */
static const VitaChiakiHost *cooldown_host_and_banner(void) {
  const uint64_t now_us = sceKernelGetProcessTimeWide();
  const uint64_t cooldown_until_us = stream_cooldown_until_us();
  const bool active =
      cooldown_until_us && cooldown_until_us > now_us && !context.stream.post_stop_guard;

  if (!active && context.stream.disconnect_banner_until_us &&
      context.stream.disconnect_banner_until_us <= now_us) {
    context.stream.disconnect_reason[0] = '\0';
    context.stream.disconnect_banner_until_us = 0;
  }
  if (!active)
    return NULL;

  const char *reason =
      (context.stream.disconnect_reason[0] && context.stream.disconnect_banner_until_us > now_us)
          ? context.stream.disconnect_reason
          : BANNER_DEFAULT_REASON;
  char text[BANNER_TEXT_MAX];
  snprintf(text, sizeof(text), BANNER_FORMAT, reason);

  vita2d_draw_rectangle(0, 0, VITA_WIDTH, BANNER_H, UI_PANEL);
  int text_x = (VITA_WIDTH - ui_text_face_width(UI_FACE_T16, text)) / 2;
  ui_text_draw_face_centered_v(UI_FACE_T16, text_x, 0, BANNER_H, UI_TEXT, text);
  return context.active_host;
}

/* ============================================================================
 * Input
 * ============================================================================ */

/** Connect to the focused console; @force_psn routes it through the PSN holepunch. */
static UIScreenType connect_focused_console(bool force_psn) {
  ConsoleCardInfo *card = ui_cards_get_card(s_list.focus);
  if (!card)
    return UI_SCREEN_TYPE_MAIN;
  ui_cards_set_selected_index(s_list.focus);
  context.stream.force_psn_holepunch = force_psn;
  return ui_screens_connect_host(card->host);
}

/** Open the existing screen for the focused Settings, Controller or Profile item. */
static UIScreenType open_category_screen(void) {
  UIScreenType target = CATEGORY_SCREENS[s_bar.focus];
  ui_focus_move_to_content(target);
  ui_nav_set_selected_icon(s_bar.focus);
  ui_nav_reset_collapsed();
  return target;
}

/**
 * True when the focused console has both a local and an Internet route, so a long
 * press of Confirm offers "Connect via". A PSN-only console has no local route to offer.
 */
static bool focused_console_has_both_routes(void) {
  if (s_bar.focus != HOME_CAT_CONSOLES)
    return false;
  const ConsoleCardInfo *card = ui_cards_get_card(s_list.focus);
  return card && card->has_internet && card->host &&
         card->host->source != VITA_HOST_SOURCE_PSN_REMOTE;
}

/** Drive the "Connect via" popup; returns the screen to show next. */
static UIScreenType update_connect_popup(void) {
  int result = ui_connect_popup_update();
  if (result == 0)
    return connect_focused_console(false);
  if (result == 1)
    return connect_focused_console(true);
  return UI_SCREEN_TYPE_MAIN;
}

/**
 * Handle Confirm held on a dual-route console: a short press connects locally, holding
 * UI_HOME_LONG_PRESS_MS opens the "Connect via" popup.
 *
 * @return the screen to show next
 */
static UIScreenType update_dual_route_confirm(const UiInput *in) {
  if (in->pressed & UI_BTN_CONFIRM) {
    s_confirm_tracking = true;
    s_confirm_start_us = sceKernelGetProcessTimeWide();
  }
  if (!s_confirm_tracking)
    return UI_SCREEN_TYPE_MAIN;

  if (in->released & UI_BTN_CONFIRM) {
    s_confirm_tracking = false;
    return connect_focused_console(false);
  }
  if ((in->down & UI_BTN_CONFIRM) &&
      sceKernelGetProcessTimeWide() - s_confirm_start_us >= UI_HOME_LONG_PRESS_MS * 1000ULL) {
    s_confirm_tracking = false;
    ui_connect_popup_show();
  }
  return UI_SCREEN_TYPE_MAIN;
}

/** Forward input to the list and act on its event; returns the screen to show next. */
static UIScreenType update_list(const UiInput *in) {
  UiInput list_in = *in;
  UIScreenType next = UI_SCREEN_TYPE_MAIN;

  if (focused_console_has_both_routes()) {
    list_in.pressed &= ~(uint32_t)UI_BTN_CONFIRM;
    next = update_dual_route_confirm(in);
  } else {
    s_confirm_tracking = false;
  }

  UiEvent ev = ui_xmb_list_input(&s_list, &list_in);
  if (ev == UI_EVENT_MOVED) {
    s_confirm_tracking = false;
  } else if (ev == UI_EVENT_ACTIVATED) {
    next =
        s_bar.focus == HOME_CAT_CONSOLES ? connect_focused_console(false) : open_category_screen();
  }
  if (s_bar.focus == HOME_CAT_CONSOLES)
    ui_cards_set_selected_index(s_list.focus);
  return next;
}

/** Square re-pairs and Start filters the focused console. */
static UIScreenType update_console_shortcuts(const UiInput *in) {
  if ((in->pressed & UI_BTN_CLEAR) && s_item_count > 0) {
    ConsoleCardInfo *card = ui_cards_get_card(s_list.focus);
    if (card)
      return ui_screens_repair_host(card->host);
  }
  if (in->pressed & UI_BTN_FILTER)
    ui_cards_open_filter();
  return UI_SCREEN_TYPE_MAIN;
}

/* ============================================================================
 * Draw
 * ============================================================================ */

/** Draw the Consoles empty state, or the active-filter line. */
static void draw_console_status_text(void) {
  if (s_item_count == 0) {
    const char *msg = ui_cards_is_filter_active() ? EMPTY_NO_MATCH : EMPTY_SEARCHING;
    ui_text_draw_face_centered_v(UI_FACE_T16, UI_LIST_TEXT_X, UI_LIST_FOCUS_Y, UI_LIST_ROW_H,
                                 UI_TEXT_3, msg);
  }
  if (ui_cards_is_filter_active()) {
    char line[FILTER_LINE_MAX];
    snprintf(line, sizeof(line), FILTER_LINE_FORMAT, ui_cards_get_filter_text(), s_item_count);
    ui_text_draw_face_centered_v(UI_FACE_T14, UI_MARGIN_X, UI_HINT_Y, UI_ROW_H, UI_TEXT_2, line);
  }
}

/* ============================================================================
 * Frame
 * ============================================================================ */

UIScreenType ui_home_frame(void) {
  const UiInput *in = ui_input_snapshot();

  ui_cards_update_cache(false);
  ui_cards_poll_filter_ime();

  const VitaChiakiHost *cooldown_host = cooldown_host_and_banner();
  refresh_items(cooldown_host);

  UIScreenType next = UI_SCREEN_TYPE_MAIN;

  if (ui_connect_popup_is_active()) {
    next = update_connect_popup();
  } else {
    if (ui_category_bar_input(&s_bar, in) == UI_EVENT_MOVED) {
      ui_xmb_list_set_focus(&s_list, 0);
      s_confirm_tracking = false;
      refresh_items(cooldown_host);
      if (s_bar.focus == HOME_CAT_CONSOLES)
        ui_cards_set_selected_index(0);
    } else {
      next = update_list(in);
    }

    if (s_bar.focus == HOME_CAT_CONSOLES && next == UI_SCREEN_TYPE_MAIN)
      next = update_console_shortcuts(in);
    if (in->pressed & UI_BTN_BROWSER)
      trigger_hints_popup(SELECT_HINT);
  }

  ui_background_draw_home_vignette();
  ui_category_bar_draw(&s_bar);
  ui_xmb_list_draw(&s_list);
  if (s_bar.focus == HOME_CAT_CONSOLES)
    draw_console_status_text();

  return next;
}
