/**
 * @file ui_home.c
 * @brief The XMB Home screen (SPEC.md section 3.1)
 *
 * One CategoryBar (Consoles / Settings / Controller / Profile) over one XmbList.
 * The list is rebuilt from its source every frame into a fixed array (no heap):
 * the console card cache for Consoles, static tables for the other categories.
 *
 * Triangle on a console opens the Options column (C05), which ui_home_options.c runs together
 * with the popups its rows open. While a popup is open the screen behind it is frozen
 * (ui_freeze.h) and Home runs only the popup, so the frame is the copy, the popup layer and the
 * popup's hint row.
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
#include "ui/ui_connect_failure.h"
#include "ui/ui_console_rows.h"
#include "ui/ui_console_status.h"
#include "ui/ui_hint_row.h"
#include "ui/ui_detail_panel.h"
#include "ui/ui_freeze.h"
#include "ui/ui_home_detail.h"
#include "ui/ui_input.h"
#include "ui/ui_home_options.h"
#include "ui/ui_page_frame.h"
#include "ui/ui_room_icons.h"
#include "ui/ui_profile.h"
#include "ui/ui_profile.h"
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

/** The screen that Confirm opens for each category's items (Settings and Profile open XMB pages,
 * Controller the old screen). */
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
static const char HINT_OPTIONS[] = "Options";
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

/** The console Home should focus on its next live frame (ui_home_focus_console), or NULL. */
static const VitaChiakiHost *s_focus_host = NULL;

static UIScreenType connect_console(VitaChiakiHost *host, bool force_psn);

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
  ui_connect_failure_init();
  ui_home_options_init(connect_console);
  s_focus_host = NULL;
}

void ui_home_on_enter(void) {
  if (!ui_focus_has_modal())
    ui_focus_set_zone(FOCUS_ZONE_MAIN_CONTENT);
  ui_nav_reset_collapsed();
  ui_home_options_reset();
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

/** Put Home on @category with item @index focused. */
static void select_category_item(int category, int index) {
  ui_category_bar_set_focus(&s_bar, category);
  refresh_items(NULL);
  ui_xmb_list_set_focus(&s_list, index);
}

void ui_home_select_settings_group(int group) {
  select_category_item(HOME_CAT_SETTINGS, group);
}

void ui_home_select_profile_group(int group) {
  select_category_item(HOME_CAT_PROFILE, group);
}

void ui_home_focus_console(const VitaChiakiHost *host) {
  s_focus_host = host;
  ui_cards_mark_dirty();
}

/** Carry out a ui_home_focus_console() request: Consoles category, the console's row. */
static void apply_focus_request(const VitaChiakiHost *cooldown) {
  if (!s_focus_host)
    return;
  const VitaChiakiHost *host = s_focus_host;
  s_focus_host = NULL;

  ui_category_bar_set_focus(&s_bar, HOME_CAT_CONSOLES);
  refresh_items(cooldown);
  for (int i = 0; i < ui_cards_get_count(); i++) {
    if (ui_cards_get_card(i)->host == host) {
      ui_xmb_list_set_focus(&s_list, i + (s_filter_row ? 1 : 0));
      ui_cards_set_selected_index(i);
      return;
    }
  }
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

/** Index of @host in the console cache, or -1 when it is not listed (gone, or filtered out). */
static int listed_console_index(const VitaChiakiHost *host) {
  for (int i = 0; i < ui_cards_get_count(); i++) {
    if (ui_cards_get_card(i)->host == host)
      return i;
  }
  return -1;
}

/** Connect to @host as Confirm on its row does; @force_psn routes it through the PSN
 * holepunch. */
static UIScreenType connect_console(VitaChiakiHost *host, bool force_psn) {
  const int index = listed_console_index(host);
  if (index < 0)
    return UI_SCREEN_TYPE_MAIN;
  ui_cards_set_selected_index(index);
  context.stream.force_psn_holepunch = force_psn;
  return ui_screens_connect_host(host);
}

/** Open the screen for the focused Settings, Controller or Profile item. */
static UIScreenType open_category_screen(void) {
  UIScreenType target = CATEGORY_SCREENS[s_bar.focus];
  if (s_bar.focus == HOME_CAT_SETTINGS)
    ui_settings_open(s_list.focus);
  else if (s_bar.focus == HOME_CAT_PROFILE)
    ui_profile_open(s_list.focus);
  ui_focus_move_to_content(target);
  ui_nav_set_selected_icon(s_bar.focus);
  ui_nav_reset_collapsed();
  return target;
}

/** Forward input to the list and act on its event; returns the screen to show next. */
static UIScreenType update_list(const UiInput *in) {
  /* One tap on the Filter row opens the keyboard at once, focused or not. */
  if (s_filter_row && ui_touch_tap(in) &&
      ui_rect_contains(s_list.hit[0], in->touch.x, in->touch.y)) {
    ui_cards_edit_filter();
    return UI_SCREEN_TYPE_MAIN;
  }

  UIScreenType next = UI_SCREEN_TYPE_MAIN;
  if (ui_xmb_list_input(&s_list, in) == UI_EVENT_ACTIVATED) {
    if (filter_row_focused()) {
      ui_cards_edit_filter();
    } else if (s_bar.focus != HOME_CAT_CONSOLES) {
      next = open_category_screen();
    } else if (focused_card()) {
      next = connect_console(focused_card()->host, false);
    }
  }
  if (focused_console_index() >= 0)
    ui_cards_set_selected_index(focused_console_index());
  return next;
}

/** The focused console and its status, for the Options column. */
static UiHomeOptionsTarget options_target(void) {
  const ConsoleCardInfo *card = focused_card();
  return (UiHomeOptionsTarget){
      .card = card,
      .status = card ? s_console_status[s_list.focus] : UI_CONSOLE_UNAVAILABLE,
  };
}

/**
 * Square clears an active filter on the Filter row; Start opens the keyboard or clears the
 * filter; Triangle on a console opens its Options column.
 */
static void update_console_shortcuts(const UiInput *in) {
  if ((in->pressed & UI_BTN_CLEAR) && filter_row_focused() && ui_cards_is_filter_active())
    ui_cards_clear_filter();
  if (in->pressed & UI_BTN_FILTER)
    ui_cards_open_filter();
  if (in->pressed & UI_BTN_OPTIONS) {
    const UiHomeOptionsTarget target = options_target();
    ui_home_options_open(&target);
  }
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
 * An open popup owns the row, then the Options column (ui_home_options_hints): Confirm Select,
 * Cancel Back. Consoles with a console focused: Confirm with the console's verb, Options, then
 * L R Category (low priority). Cooldown shows "Please wait" dimmed. The Filter row: Confirm
 * Filter, Square Clear while a filter is active, L R Category. Other categories: Confirm Open.
 * An empty console list: L R Category only.
 */
static int build_hints(UiHintItem out[UI_HINT_MAX_ITEMS]) {
  int n = ui_home_options_hints(out);
  if (n > 0)
    return n;

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
    if (consoles)
      out[n++] = (UiHintItem){.action = UI_BTN_OPTIONS, .label = HINT_OPTIONS};
  }
  out[n++] =
      (UiHintItem){.action = UI_BTN_L | UI_BTN_R, .label = HINT_CATEGORY, .low_priority = true};
  return n;
}

/* ============================================================================
 * Frame
 * ============================================================================ */

/**
 * Run one frame of input for the layer that has it: a popup or the Options column
 * (ui_home_options.c), else the category bar, the list and the shortcuts.
 *
 * @return the screen to show next
 */
static UIScreenType update_input(const UiInput *in, const VitaChiakiHost *cooldown) {
  if (ui_home_options_popup_open() || ui_home_options_column_open()) {
    const UiHomeOptionsTarget target = options_target();
    return ui_home_options_input(in, &target);
  }

  if (ui_category_bar_input(&s_bar, in) == UI_EVENT_MOVED) {
    refresh_items(cooldown);
    ui_xmb_list_set_focus(&s_list,
                          s_bar.focus == HOME_CAT_CONSOLES
                              ? ui_console_rows_initial_focus(s_filter_row, ui_cards_get_count())
                              : 0);
    ui_xmb_list_cascade_in(&s_list);
    if (focused_console_index() >= 0)
      ui_cards_set_selected_index(focused_console_index());
    return UI_SCREEN_TYPE_MAIN;
  }

  const UIScreenType next = update_list(in);
  if (s_bar.focus == HOME_CAT_CONSOLES && next == UI_SCREEN_TYPE_MAIN)
    update_console_shortcuts(in);
  return next;
}

/**
 * Open the "Could not connect" popup for a connection failure waiting in the hand-off, unless a
 * popup is already open (the failure then waits). It opens after this frame's input, so a press
 * made for something else cannot answer it. Try again is offered only while the console is in
 * the list (listed_console_index()): a console that is gone, or filtered out, cannot be
 * connected to from here.
 */
static void open_pending_failure(void) {
  if (ui_home_options_popup_open())
    return;
  UiConnectFailure failure;
  if (ui_connect_failure_take(&failure))
    ui_home_options_open_failure(&failure, listed_console_index(failure.host) >= 0);
}

/** Draw the live Home layers. The ones behind the Options column are tinted down to its dim. */
static void draw_live_layers(const char *banner_reason) {
  const bool consoles = s_bar.focus == HOME_CAT_CONSOLES;

  ui_background_draw_home_vignette();
  ui_layer_set_alpha(ui_home_options_behind_alpha());
  ui_top_bar_draw(consoles ? banner_reason : NULL);
  ui_category_bar_draw(&s_bar);
  ui_xmb_list_draw(&s_list);
  ui_home_detail_draw((UiHomeDetailSource)s_bar.focus, &s_list, consoles && s_filter_row);
  if (consoles)
    draw_empty_state();
  ui_layer_set_alpha(1.0f);
  ui_home_options_draw_column();
}

UIScreenType ui_home_frame(void) {
  /* The freeze state at the start of the frame (it changes only after the swap) decides whether
   * ui.c already drew the frozen copy: then the screen behind is not drawn live. Whether this
   * frame becomes a popup's background copy is known only after input, which is what opens the
   * popup (see "capturing" below). */
  const bool frozen = ui_freeze_is_ready();

  /* A tapped hint acts as that button pressed and released in one frame, except while the
   * Options column is open, where every tap outside it only closes it. */
  UiInput in = *ui_input_snapshot();
  const bool hints_tappable = ui_home_options_popup_open() || !ui_home_options_column_open();
  const uint32_t tapped = hints_tappable ? ui_hint_row_tap(&s_hints, &in) : 0;
  if (tapped) {
    in.pressed |= tapped;
    in.released |= tapped;
    in.touch.pressed = false;
    in.touch.released = false;
    in.touch.down = false;
  }

  /* Behind a popup the console list does not change, so the row a popup is about stays put. */
  const char *banner_reason = NULL;
  const VitaChiakiHost *cooldown = NULL;
  if (!ui_home_options_popup_open()) {
    ui_cards_update_cache(false);
    ui_cards_poll_filter_ime();
  }
  if (!frozen) {
    cooldown = cooldown_host(&banner_reason);
    refresh_items(cooldown);
    apply_focus_request(cooldown);
    if (ui_home_options_column_open()) {
      const UiHomeOptionsTarget target = options_target();
      ui_home_options_refresh(&target);
    }
  }

  const UIScreenType next = update_input(&in, cooldown);
  if (next == UI_SCREEN_TYPE_MAIN)
    open_pending_failure();

  /* True when a popup was opened this frame: the frame becomes the popup's background copy, so
   * it is drawn without the popup and without the hint row. Read after input, which is what
   * requests the freeze; read before it, the opening frame would end up in the copy with the
   * hint row on it. */
  const bool capturing = ui_freeze_is_capturing();
  if (!frozen)
    draw_live_layers(banner_reason);
  if (!capturing)
    ui_home_options_draw_popup();

  if (capturing) {
    s_hints.count = 0;
  } else {
    UiHintItem hints[UI_HINT_MAX_ITEMS];
    ui_hint_row_layout(&s_hints, hints, build_hints(hints));
    ui_hint_row_draw(&s_hints);
  }
  return next;
}
