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
#include "ui/ui_chrome_layout.h"
#include "ui/ui_components.h"
#include "ui/ui_component.h"
#include "ui/ui_console_rows.h"
#include "ui/ui_console_status.h"
#include "ui/ui_hint_row.h"
#include "ui/ui_detail_panel.h"
#include "ui/ui_home_detail.h"
#include "ui/ui_input.h"
#include "ui/ui_room_icons.h"
#include "ui/ui_settings.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"
#include "ui/ui_top_bar.h"
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
  HOME_ICON_VIDEO = 0,
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
  HOME_ICON_SEARCH,
  HOME_ICON_COUNT,
} HomeIcon;

#define HOME_ICON_DIR "app0:/assets/icons/"

static const char *const HOME_ICON_FILES[HOME_ICON_COUNT] = {
    "video",      "network", "display", "controls", "advanced", "account",
    "connection", "psn",     "slot1",   "slot2",    "slot3",    "search",
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

/* The detail panel reads the list rows in this order (ui_home_detail.h). */
_Static_assert(HOME_CAT_CONSOLES == (int)UI_HOME_DETAIL_CONSOLES &&
                   HOME_CAT_SETTINGS == (int)UI_HOME_DETAIL_SETTINGS &&
                   HOME_CAT_CONTROLLER == (int)UI_HOME_DETAIL_CONTROLLER &&
                   HOME_CAT_PROFILE == (int)UI_HOME_DETAIL_PROFILE,
               "Home categories and detail sources must share one order");
_Static_assert(sizeof(SETTINGS_ENTRIES) / sizeof(SETTINGS_ENTRIES[0]) == UI_SETTINGS_GROUP_COUNT,
               "Settings list and detail groups must match");
_Static_assert(sizeof(CONTROLLER_ENTRIES) / sizeof(CONTROLLER_ENTRIES[0]) ==
                   UI_CONTROLLER_PRESET_COUNT,
               "Controller list and detail presets must match");
_Static_assert(sizeof(PROFILE_ENTRIES) / sizeof(PROFILE_ENTRIES[0]) == UI_PROFILE_GROUP_COUNT,
               "Profile list and detail groups must match");

static const char *const CATEGORY_LABELS[UI_CAT_COUNT] = {"Consoles", "Settings", "Controller",
                                                          "Profile"};

/** The screen that Confirm opens for each category's items (Settings opens the XMB page, the rest
 * the old screens). */
static const UIScreenType CATEGORY_SCREENS[UI_CAT_COUNT] = {
    UI_SCREEN_TYPE_MAIN, UI_SCREEN_TYPE_SETTINGS, UI_SCREEN_TYPE_CONTROLLER,
    UI_SCREEN_TYPE_PROFILE};

/* Copy */
static const char ROUTE_INTERNET[] = "\xC2\xB7 Internet";
static const char EMPTY_SEARCHING[] = "Searching for consoles...";
static const char EMPTY_NO_MATCH[] = "No consoles match filter";
static const char BANNER_DEFAULT_REASON[] = "Connection interrupted";
static const char FILTER_IDLE_NAME[] = "Filter...";
static const char FILTER_NAME_FORMAT[] = "Filter: \"%s\"";
static const char FILTER_STATUS_FORMAT[] = "%d found \xC2\xB7";
static const char FILTER_STATUS_TAIL[] = "to clear";
static const char FILTER_IDLE_STATUS[] = "";
static const char HINT_CONNECT[] = "Connect";
static const char HINT_WAKE[] = "Wake";
static const char HINT_PAIR[] = "Pair";
static const char HINT_PLEASE_WAIT[] = "Please wait";
static const char HINT_OPEN[] = "Open";
static const char HINT_CATEGORY[] = "Category";
static const char HINT_FILTER[] = "Filter";
static const char HINT_CLEAR[] = "Clear";

/** Room for the Filter row's name: the quoted filter text plus "Filter: ". */
#define FILTER_NAME_MAX (UI_FILTER_TEXT_MAX + 16)
#define FILTER_STATUS_MAX 24

/* ============================================================================
 * State
 * ============================================================================ */

static UiCategoryBar s_bar;
static UiXmbList s_list;
static UiXmbItem s_items[UI_LIST_MAX_ITEMS];
static vita2d_texture *s_item_icons[HOME_ICON_COUNT];
static int s_item_count = 0;
/** Status of each list row, parallel to s_items while Consoles is focused (drives the hint verb).
 * The Filter row's entry is unused. */
static UiConsoleStatus s_console_status[UI_LIST_MAX_ITEMS];
/** The Consoles list starts with the Filter row (ui_console_rows_has_filter), as of the last
 * refresh. Row i is console i - 1 while it is set, console i otherwise. */
static bool s_filter_row = false;
/** Category whose rows the last refresh built; -1 before the first one. */
static int s_last_category = -1;
/** The Filter row's strings, rebuilt only when the filter text or the match count changes. */
static char s_filter_name[FILTER_NAME_MAX];
static char s_filter_status[FILTER_STATUS_MAX];
static char s_filter_cached_text[UI_FILTER_TEXT_MAX];
static int s_filter_cached_found = -1;
/** Hint layout of the last drawn frame; taps are resolved against it. */
static UiHintLayout s_hints;

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
  ui_top_bar_init();
  ui_hint_row_init();
  s_item_count = 0;
  s_filter_row = false;
  s_last_category = -1;
  s_filter_cached_found = -1;
  s_hints.count = 0;
  s_confirm_tracking = false;
}

void ui_home_on_enter(void) {
  if (!ui_focus_has_modal())
    ui_focus_set_zone(FOCUS_ZONE_MAIN_CONTENT);
  ui_nav_reset_collapsed();
  s_confirm_tracking = false;
  s_hints.count = 0;
  ui_xmb_list_cascade_in(&s_list);
  ui_detail_panel_restart_rise();
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
      .icon = ui_room_icons_get(ui_room_icon_for_host(card->host), ROOM_ICON_SIZE_ROW),
      .name = card->name,
      .status = ui_console_status_label(state.status),
      .status_dot = true,
      .route = state.internet_route ? ROUTE_INTERNET : NULL,
  };

  switch (state.status) {
    case UI_CONSOLE_READY:
      item.status_color = UI_OK;
      break;
    case UI_CONSOLE_STANDBY:
      item.status_color = UI_WARN;
      break;
    case UI_CONSOLE_UNPAIRED:
      item.status_color = UI_IDLE;
      item.dim_icon = true;
      break;
    case UI_CONSOLE_UNAVAILABLE:
      item.status_color = UI_TEXT_3;
      item.dim_icon = true;
      break;
    case UI_CONSOLE_COOLDOWN:
      item.status_color = UI_WARN;
      item.dim_row = true;
      break;
    case UI_CONSOLE_ERROR:
      item.status_color = UI_ERR;
      break;
    case UI_CONSOLE_RETRYING:
      item.status_color = UI_WARN;
      break;
  }
  return item;
}

/** Width of @text in the list's name face, for ellipsizing the Filter row's name. */
static int measure_name_face(const char *text, void *ctx) {
  (void)ctx;
  return ui_text_face_width(UI_FACE_T20, text);
}

/** Rewrite the Filter row's name and status when the filter text or the match count changed. */
static void update_filter_strings(void) {
  const char *text = ui_cards_get_filter_text();
  const int found = ui_cards_get_count();
  if (found == s_filter_cached_found && strcmp(text, s_filter_cached_text) == 0)
    return;

  char full_name[FILTER_NAME_MAX];
  snprintf(full_name, sizeof(full_name), FILTER_NAME_FORMAT, text);
  ui_ellipsize_to_fit(full_name, UI_LIST_TEXT_W, measure_name_face, NULL, s_filter_name,
                      sizeof(s_filter_name));
  snprintf(s_filter_status, sizeof(s_filter_status), FILTER_STATUS_FORMAT, found);
  snprintf(s_filter_cached_text, sizeof(s_filter_cached_text), "%s", text);
  s_filter_cached_found = found;
}

/** The Filter row: idle it reads "Filter...", active it shows the text and the match count. */
static UiXmbItem filter_item(void) {
  UiXmbItem item = {
      .icon = s_item_icons[HOME_ICON_SEARCH],
      .name = FILTER_IDLE_NAME,
      .status = FILTER_IDLE_STATUS,
      .status_color = UI_TEXT_2,
  };
  if (ui_cards_is_filter_active()) {
    update_filter_strings();
    item.name = s_filter_name;
    item.status = s_filter_status;
    item.status_glyph = symbol_square;
    item.status_tail = FILTER_STATUS_TAIL;
  }
  return item;
}

/**
 * Fill s_items from the console cache, after the Filter row when @filter_row is set.
 * @cooldown_host is the console in cooldown, if any.
 */
static int fill_console_items(const VitaChiakiHost *cooldown_host, bool filter_row) {
  const bool token_ok = psn_auth_token_is_valid((uint64_t)time(NULL));
  const int first = filter_row ? 1 : 0;
  const int count = ui_cards_get_count() < UI_LIST_MAX_ITEMS - first ? ui_cards_get_count()
                                                                     : UI_LIST_MAX_ITEMS - first;

  if (filter_row)
    s_items[0] = filter_item();
  for (int i = 0; i < count; i++) {
    const ConsoleCardInfo *card = ui_cards_get_card(i);
    UiConsoleState state = ui_cards_classify(
        card, token_ok, cooldown_host && card->host == cooldown_host, ui_cards_message(card));
    s_items[first + i] = console_item(card, state);
    s_console_status[first + i] = state.status;
  }
  return first + count;
}

/**
 * Rebuild the Consoles rows. When the Filter row appears or goes away while Consoles stays
 * focused, the focus follows its console (ui_console_rows_rebase_focus).
 */
static void refresh_console_rows(const VitaChiakiHost *cooldown_host) {
  const bool had_filter_row = s_filter_row;
  s_filter_row =
      ui_console_rows_has_filter(ui_cards_get_total_count(), ui_cards_is_filter_active());
  s_item_count = fill_console_items(cooldown_host, s_filter_row);
  ui_xmb_list_set_items(&s_list, s_items, s_item_count);
  if (s_last_category == HOME_CAT_CONSOLES && had_filter_row != s_filter_row)
    ui_xmb_list_set_focus(&s_list,
                          ui_console_rows_rebase_focus(s_list.focus, had_filter_row, s_filter_row));
}

/** Rebuild s_items for the focused category and hand them to the list. */
static void refresh_items(const VitaChiakiHost *cooldown_host) {
  s_filter_row = s_filter_row && s_bar.focus == HOME_CAT_CONSOLES;
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
      refresh_console_rows(cooldown_host);
      s_last_category = s_bar.focus;
      return;
  }
  ui_xmb_list_set_items(&s_list, s_items, s_item_count);
  s_last_category = s_bar.focus;
}

void ui_home_select_settings_group(int group) {
  ui_category_bar_set_focus(&s_bar, HOME_CAT_SETTINGS);
  refresh_items(NULL);
  ui_xmb_list_set_focus(&s_list, group);
  s_confirm_tracking = false;
}

/* ============================================================================
 * Cooldown
 * ============================================================================ */

/**
 * cooldown_host() - Find the console in cooldown and the reason for the top-bar banner.
 * @banner_reason: Out: the reason text while a cooldown is active, otherwise NULL.
 *
 * Also expires a stale disconnect reason once its banner window has passed.
 *
 * @return the console in cooldown, or NULL when no cooldown is active
 */
static const VitaChiakiHost *cooldown_host(const char **banner_reason) {
  const uint64_t now_us = sceKernelGetProcessTimeWide();
  const uint64_t cooldown_until_us = stream_cooldown_until_us();
  const bool active =
      cooldown_until_us && cooldown_until_us > now_us && !context.stream.post_stop_guard;

  *banner_reason = NULL;
  if (!active && context.stream.disconnect_banner_until_us &&
      context.stream.disconnect_banner_until_us <= now_us) {
    context.stream.disconnect_reason[0] = '\0';
    context.stream.disconnect_banner_until_us = 0;
  }
  if (!active)
    return NULL;

  *banner_reason =
      (context.stream.disconnect_reason[0] && context.stream.disconnect_banner_until_us > now_us)
          ? context.stream.disconnect_reason
          : BANNER_DEFAULT_REASON;
  return context.active_host;
}

/* ============================================================================
 * Input
 * ============================================================================ */

/** The console the focused row stands for, or -1 (Filter row, other category, empty list). */
static int focused_console_index(void) {
  if (s_bar.focus != HOME_CAT_CONSOLES)
    return -1;
  return ui_console_rows_console_index(s_filter_row, s_list.focus, ui_cards_get_count());
}

/** The focused console's card, or NULL when the focus is not on a console. */
static ConsoleCardInfo *focused_card(void) {
  return ui_cards_get_card(focused_console_index());
}

/** True when the focus is on the Consoles list's Filter row. */
static bool filter_row_focused(void) {
  return s_bar.focus == HOME_CAT_CONSOLES && s_filter_row && s_list.focus == 0;
}

/** Connect to the focused console; @force_psn routes it through the PSN holepunch. */
static UIScreenType connect_focused_console(bool force_psn) {
  ConsoleCardInfo *card = focused_card();
  if (!card)
    return UI_SCREEN_TYPE_MAIN;
  ui_cards_set_selected_index(focused_console_index());
  context.stream.force_psn_holepunch = force_psn;
  return ui_screens_connect_host(card->host);
}

/** Open the screen for the focused Settings, Controller or Profile item. */
static UIScreenType open_category_screen(void) {
  UIScreenType target = CATEGORY_SCREENS[s_bar.focus];
  if (s_bar.focus == HOME_CAT_SETTINGS)
    ui_settings_open(s_list.focus);
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
  const ConsoleCardInfo *card = focused_card();
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

  /* One tap on the Filter row opens the keyboard at once, focused or not. */
  if (s_filter_row && ui_touch_tap(in) &&
      ui_rect_contains(s_list.hit[0], in->touch.x, in->touch.y)) {
    ui_cards_edit_filter();
    return next;
  }

  UiEvent ev = ui_xmb_list_input(&s_list, &list_in);
  if (ev == UI_EVENT_MOVED) {
    s_confirm_tracking = false;
  } else if (ev == UI_EVENT_ACTIVATED) {
    if (filter_row_focused())
      ui_cards_edit_filter();
    else
      next = s_bar.focus == HOME_CAT_CONSOLES ? connect_focused_console(false)
                                              : open_category_screen();
  }
  if (focused_console_index() >= 0)
    ui_cards_set_selected_index(focused_console_index());
  return next;
}

/**
 * Square clears an active filter on the Filter row and re-pairs the focused console (until
 * ticket #303 moves it into Options); Start opens the keyboard or clears the filter.
 */
static UIScreenType update_console_shortcuts(const UiInput *in) {
  if (in->pressed & UI_BTN_CLEAR) {
    if (filter_row_focused()) {
      if (ui_cards_is_filter_active())
        ui_cards_clear_filter();
    } else {
      ConsoleCardInfo *card = focused_card();
      if (card)
        return ui_screens_repair_host(card->host);
    }
  }
  if (in->pressed & UI_BTN_FILTER)
    ui_cards_open_filter();
  return UI_SCREEN_TYPE_MAIN;
}

/* ============================================================================
 * Draw
 * ============================================================================ */

/**
 * Draw the Consoles empty state (SPEC C26): below the Filter row when there is one. Text only;
 * the 16 px spinner of the mock would cost a ring of line draws every frame.
 */
static void draw_empty_state(void) {
  if (ui_cards_get_count() > 0)
    return;
  const bool no_match = ui_cards_is_filter_active() && ui_cards_get_total_count() > 0;
  const int y = UI_LIST_FOCUS_Y + (s_filter_row ? UI_LIST_ROW_H + UI_LIST_FOCUS_GAP : 0);
  ui_text_draw_face_centered_v(UI_FACE_T20, UI_LIST_TEXT_X, y, UI_LIST_ROW_H, UI_TEXT_2,
                               no_match ? EMPTY_NO_MATCH : EMPTY_SEARCHING);
}

/** Confirm verb for a console in @status (SPEC 3.1). */
static const char *console_confirm_verb(UiConsoleStatus status) {
  switch (status) {
    case UI_CONSOLE_STANDBY:
      return HINT_WAKE;
    case UI_CONSOLE_UNPAIRED:
      return HINT_PAIR;
    case UI_CONSOLE_COOLDOWN:
      return HINT_PLEASE_WAIT;
    default:
      return HINT_CONNECT;
  }
}

/**
 * build_hints() - Fill @out with the hints for what is focused (SPEC 3.1) and return how many.
 *
 * Consoles with a console focused: Confirm with the console's verb, then L R Category (low
 * priority). Cooldown shows "Please wait" dimmed. The Filter row: Confirm Filter, Square Clear
 * while a filter is active, L R Category. Other categories: Confirm Open. An empty console
 * list: L R Category only. The Options hint arrives with ticket #303.
 */
static int build_hints(UiHintItem out[UI_HINT_MAX_ITEMS]) {
  int n = 0;
  const bool consoles = s_bar.focus == HOME_CAT_CONSOLES;

  if (filter_row_focused()) {
    out[n++] = (UiHintItem){.action = UI_BTN_CONFIRM, .label = HINT_FILTER};
    if (ui_cards_is_filter_active())
      out[n++] = (UiHintItem){.action = UI_BTN_CLEAR, .label = HINT_CLEAR};
  } else if (!consoles || s_item_count > 0) {
    const UiConsoleStatus status = consoles ? s_console_status[s_list.focus] : UI_CONSOLE_READY;
    out[n++] = (UiHintItem){
        .action = UI_BTN_CONFIRM,
        .label = consoles ? console_confirm_verb(status) : HINT_OPEN,
        .dim = consoles && status == UI_CONSOLE_COOLDOWN,
    };
  }
  out[n++] =
      (UiHintItem){.action = UI_BTN_L | UI_BTN_R, .label = HINT_CATEGORY, .low_priority = true};
  return n;
}

/* ============================================================================
 * Frame
 * ============================================================================ */

UIScreenType ui_home_frame(void) {
  /* A tapped hint acts as that button pressed and released in one frame. */
  UiInput in = *ui_input_snapshot();
  const uint32_t tapped = ui_hint_row_tap(&s_hints, &in);
  if (tapped) {
    in.pressed |= tapped;
    in.released |= tapped;
    in.touch.pressed = false;
    in.touch.released = false;
    in.touch.down = false;
  }

  ui_cards_update_cache(false);
  ui_cards_poll_filter_ime();

  const char *banner_reason;
  const VitaChiakiHost *cooldown = cooldown_host(&banner_reason);
  refresh_items(cooldown);

  UIScreenType next = UI_SCREEN_TYPE_MAIN;

  if (ui_connect_popup_is_active()) {
    next = update_connect_popup();
  } else {
    if (ui_category_bar_input(&s_bar, &in) == UI_EVENT_MOVED) {
      s_confirm_tracking = false;
      refresh_items(cooldown);
      ui_xmb_list_set_focus(&s_list,
                            s_bar.focus == HOME_CAT_CONSOLES
                                ? ui_console_rows_initial_focus(s_filter_row, ui_cards_get_count())
                                : 0);
      ui_xmb_list_cascade_in(&s_list);
      if (focused_console_index() >= 0)
        ui_cards_set_selected_index(focused_console_index());
    } else {
      next = update_list(&in);
    }

    if (s_bar.focus == HOME_CAT_CONSOLES && next == UI_SCREEN_TYPE_MAIN)
      next = update_console_shortcuts(&in);
  }

  ui_background_draw_home_vignette();
  const bool consoles = s_bar.focus == HOME_CAT_CONSOLES;
  ui_top_bar_draw(consoles ? banner_reason : NULL);
  ui_category_bar_draw(&s_bar);
  ui_xmb_list_draw(&s_list);
  ui_home_detail_draw((UiHomeDetailSource)s_bar.focus, &s_list, consoles && s_filter_row);
  if (consoles)
    draw_empty_state();

  UiHintItem hints[UI_HINT_MAX_ITEMS];
  ui_hint_row_layout(&s_hints, hints, build_hints(hints));
  ui_hint_row_draw(&s_hints);

  return next;
}
