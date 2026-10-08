/**
 * @file ui_home_options.c
 * @brief Home's Options column and the popups its rows open (see ui_home_options.h)
 */

#include "ui/ui_home_options.h"

#include <stdio.h>

#include "context.h"
#include "ui/ui_console_rows.h"
#include "ui/ui_list_popup.h"
#include "ui/ui_options_column.h"
#include "ui/ui_page_frame.h"
#include "ui/ui_popup.h"
#include "ui/ui_room_icons.h"
#include "ui/ui_screens.h"
#include "ui/ui_theme.h"

/* Copy (SPEC section 5) */
static const char HINT_SELECT[] = "Select";
static const char HINT_BACK[] = "Back";
static const char OPTION_CONNECT[] = "Connect";
static const char OPTION_WAKE_CONNECT[] = "Wake and connect";
static const char OPTION_CONNECT_VIA[] = "Connect via";
static const char OPTION_REPAIR[] = "Re-pair";
static const char OPTION_PAIR[] = "Pair";
static const char OPTION_CHANGE_ICON[] = "Change icon";
static const char REPAIR_TITLE_FORMAT[] = "Re-pair %s?";
static const char REPAIR_BODY[] = "You will need to enter a new 8-digit PIN from the console.";
static const char REPAIR_BUTTON_CANCEL[] = "Cancel";
static const char REPAIR_BUTTON_CONFIRM[] = "Re-pair";
static const char VIA_TITLE[] = "Connect via";
static const char VIA_CONFIRM[] = "Connect";
static const char VIA_LOCAL[] = "Local Network";
static const char VIA_INTERNET[] = "Internet";
static const char VIA_INTERNET_DETAIL[] = "PSN";
static const char ICON_TITLE[] = "Change icon";
static const char ICON_CONFIRM[] = "Choose";
static const char ICON_CANCEL[] = "Cancel";

/** Buttons of the Re-pair confirm popup, left to right; Cancel is the default focus. */
enum { REPAIR_CANCEL = 0, REPAIR_CONFIRM = 1 };

/** Rows of the Connect via popup, in order. */
enum { VIA_ROW_LOCAL = 0, VIA_ROW_INTERNET = 1, VIA_ROW_COUNT };

/** Which popup is open; at most one is. */
typedef enum active_popup_t {
  POPUP_NONE = 0,
  POPUP_REPAIR,
  POPUP_VIA,
  POPUP_ICON,
} ActivePopup;

static UiOptionsColumn s_col;
static UiPopup s_repair;
static UiListPopup s_via;
static UiListPopup s_icon;
static ActivePopup s_active = POPUP_NONE;
/** The console the open popup is about. */
static VitaChiakiHost *s_host = NULL;
static UiHomeConnectFn s_connect = NULL;

/** The words on an Options row, indexed by UiConsoleOption. */
static const char *const OPTION_LABELS[] = {
    [UI_CONSOLE_OPTION_CONNECT] = OPTION_CONNECT,
    [UI_CONSOLE_OPTION_WAKE_CONNECT] = OPTION_WAKE_CONNECT,
    [UI_CONSOLE_OPTION_CONNECT_VIA] = OPTION_CONNECT_VIA,
    [UI_CONSOLE_OPTION_REPAIR] = OPTION_REPAIR,
    [UI_CONSOLE_OPTION_PAIR] = OPTION_PAIR,
    [UI_CONSOLE_OPTION_CHANGE_ICON] = OPTION_CHANGE_ICON,
};

/* ============================================================================
 * Setup and state
 * ============================================================================ */

static void close_popups(void) {
  ui_popup_close(&s_repair);
  ui_list_popup_close(&s_via);
  ui_list_popup_close(&s_icon);
  s_active = POPUP_NONE;
  s_host = NULL;
}

void ui_home_options_init(UiHomeConnectFn connect) {
  ui_options_column_init(&s_col);
  close_popups();
  s_connect = connect;
}

void ui_home_options_reset(void) {
  ui_options_column_close_now(&s_col);
  close_popups();
}

bool ui_home_options_column_open(void) {
  return ui_options_column_is_open(&s_col);
}

bool ui_home_options_popup_open(void) {
  return s_active != POPUP_NONE;
}

/* ============================================================================
 * Column
 * ============================================================================ */

/**
 * True when the console has both a local and an Internet route, so Options offers "Connect via".
 * A PSN-only console has no local route to offer.
 */
static bool has_both_routes(const ConsoleCardInfo *card) {
  return card->has_internet && card->host && card->host->source != VITA_HOST_SOURCE_PSN_REMOTE;
}

/** Fill @rows with what Options offers for @target (ui_console_option_rows); returns the count. */
static int fill_option_rows(const UiHomeOptionsTarget *target,
                            UiOptionsRow rows[UI_OPTS_MAX_ROWS]) {
  UiConsoleOptionRow wanted[UI_CONSOLE_OPTION_ROWS_MAX];
  const int count = ui_console_option_rows(target->status, has_both_routes(target->card),
                                           ui_room_icon_can_change(target->card->host), wanted);
  for (int i = 0; i < count; i++) {
    rows[i] = (UiOptionsRow){
        .id = (int)wanted[i].option,
        .label = OPTION_LABELS[wanted[i].option],
        .disabled = wanted[i].disabled,
    };
  }
  return count;
}

void ui_home_options_refresh(const UiHomeOptionsTarget *target) {
  if (!target->card) {
    ui_options_column_close_now(&s_col);
    return;
  }
  UiOptionsRow rows[UI_OPTS_MAX_ROWS];
  const int count = fill_option_rows(target, rows);
  ui_options_column_set_rows(&s_col, target->card->name, rows, count);
}

void ui_home_options_open(const UiHomeOptionsTarget *target) {
  if (!target->card)
    return;
  ui_options_column_open(&s_col);
  ui_home_options_refresh(target);
}

/* ============================================================================
 * Popups
 * ============================================================================ */

/** Open the Re-pair confirm popup for @card (SPEC C13). */
static void open_repair_popup(const ConsoleCardInfo *card) {
  char title[UI_POPUP_TEXT_MAX];
  snprintf(title, sizeof(title), REPAIR_TITLE_FORMAT, card->name);

  s_host = card->host;
  s_active = POPUP_REPAIR;
  ui_popup_open(&s_repair, &(UiPopupSpec){
                               .size = UI_POPUP_SIZE_S,
                               .icon = ui_page_frame_icon(UI_PAGE_ICON_LOCK),
                               .title = title,
                               .body = REPAIR_BODY,
                               .buttons = {REPAIR_BUTTON_CANCEL, REPAIR_BUTTON_CONFIRM},
                               .button_count = 2,
                               .default_focus = REPAIR_CANCEL,
                               .cancel_button = REPAIR_CANCEL,
                           });
}

/** Open the Connect via popup for @card: Local Network (its address) or Internet (PSN). */
static void open_via_popup(const ConsoleCardInfo *card) {
  UiListRow rows[VIA_ROW_COUNT] = {0};
  snprintf(rows[VIA_ROW_LOCAL].label, sizeof(rows[VIA_ROW_LOCAL].label), "%s", VIA_LOCAL);
  snprintf(rows[VIA_ROW_LOCAL].right_label, sizeof(rows[VIA_ROW_LOCAL].right_label), "%s",
           card->ip_address);
  snprintf(rows[VIA_ROW_INTERNET].label, sizeof(rows[VIA_ROW_INTERNET].label), "%s", VIA_INTERNET);
  snprintf(rows[VIA_ROW_INTERNET].right_label, sizeof(rows[VIA_ROW_INTERNET].right_label), "%s",
           VIA_INTERNET_DETAIL);

  s_host = card->host;
  s_active = POPUP_VIA;
  ui_list_popup_open(&s_via, &(UiListPopupSpec){
                                 .size = UI_POPUP_SIZE_S,
                                 .title = VIA_TITLE,
                                 .subtitle = card->name,
                                 .confirm_label = VIA_CONFIRM,
                                 .rows = rows,
                                 .count = VIA_ROW_COUNT,
                                 .focus = VIA_ROW_LOCAL,
                             });
}

/** Open the Change icon popup for @card: the room icons in a grid, the current one checked. */
static void open_icon_popup(const ConsoleCardInfo *card) {
  const int current = ui_room_icon_for_host(card->host);
  UiListRow rows[ROOM_ICON_COUNT] = {0};
  for (int i = 0; i < ROOM_ICON_COUNT; i++) {
    snprintf(rows[i].label, sizeof(rows[i].label), "%s", room_icon_label(i));
    rows[i].icon = ui_room_icons_get(i, ROOM_ICON_SIZE_GRID);
    rows[i].current = i == current;
  }

  s_host = card->host;
  s_active = POPUP_ICON;
  ui_list_popup_open(&s_icon, &(UiListPopupSpec){
                                  .size = UI_POPUP_SIZE_M,
                                  .title = ICON_TITLE,
                                  .subtitle = card->name,
                                  .confirm_label = ICON_CONFIRM,
                                  .cancel_label = ICON_CANCEL,
                                  .grid = true,
                                  .rows = rows,
                                  .count = ROOM_ICON_COUNT,
                                  .focus = current,
                              });
}

/**
 * Drive the Re-pair popup. Re-pair closes it and the column and goes to the PIN screen; Cancel,
 * the Cancel button and a tap outside close the popup and leave the Options column open.
 */
static UIScreenType update_repair_popup(const UiInput *in) {
  const UiEvent ev = ui_popup_input(&s_repair, in);
  if (ev != UI_EVENT_ACTIVATED && ev != UI_EVENT_CANCELLED)
    return UI_SCREEN_TYPE_MAIN;

  VitaChiakiHost *host = s_host;
  const bool confirmed = ev == UI_EVENT_ACTIVATED && s_repair.activated == REPAIR_CONFIRM;
  close_popups();
  if (!confirmed)
    return UI_SCREEN_TYPE_MAIN;
  ui_options_column_close_now(&s_col);
  return ui_screens_repair_host(host);
}

/** Drive the Connect via popup. A route connects over it; Cancel returns to the column. */
static UIScreenType update_via_popup(const UiInput *in) {
  const UiEvent ev = ui_list_popup_input(&s_via, in);
  if (ev != UI_EVENT_ACTIVATED && ev != UI_EVENT_CANCELLED)
    return UI_SCREEN_TYPE_MAIN;

  const bool internet = ev == UI_EVENT_ACTIVATED && s_via.activated == VIA_ROW_INTERNET;
  const bool connect = ev == UI_EVENT_ACTIVATED;
  close_popups();
  if (!connect)
    return UI_SCREEN_TYPE_MAIN;
  ui_options_column_close_now(&s_col);
  return s_connect(internet);
}

/** Drive the Change icon popup. Choose saves the icon and closes the popup; the column stays. */
static UIScreenType update_icon_popup(const UiInput *in) {
  const UiEvent ev = ui_list_popup_input(&s_icon, in);
  if (ev != UI_EVENT_ACTIVATED && ev != UI_EVENT_CANCELLED)
    return UI_SCREEN_TYPE_MAIN;

  if (ev == UI_EVENT_ACTIVATED && !ui_room_icon_set_for_host(s_host, s_icon.activated)) {
    LOGE("Change icon: could not store icon %d for \"%s\"; keeping the old one", s_icon.activated,
         s_host ? s_host->hostname : "?");
  }
  close_popups();
  return UI_SCREEN_TYPE_MAIN;
}

/* ============================================================================
 * Input
 * ============================================================================ */

/**
 * Run the Options row the user chose. Re-pair, Connect via and Change icon keep the column open
 * behind their popup; every other row closes it and connects.
 */
static UIScreenType run_option(int id, const UiHomeOptionsTarget *target) {
  switch (id) {
    case UI_CONSOLE_OPTION_REPAIR:
      open_repair_popup(target->card);
      return UI_SCREEN_TYPE_MAIN;
    case UI_CONSOLE_OPTION_CONNECT_VIA:
      open_via_popup(target->card);
      return UI_SCREEN_TYPE_MAIN;
    case UI_CONSOLE_OPTION_CHANGE_ICON:
      open_icon_popup(target->card);
      return UI_SCREEN_TYPE_MAIN;
    default:
      ui_options_column_close_now(&s_col);
      return s_connect(false);
  }
}

UIScreenType ui_home_options_input(const UiInput *in, const UiHomeOptionsTarget *target) {
  switch (s_active) {
    case POPUP_REPAIR:
      return update_repair_popup(in);
    case POPUP_VIA:
      return update_via_popup(in);
    case POPUP_ICON:
      return update_icon_popup(in);
    case POPUP_NONE:
      break;
  }
  if (ui_options_column_input(&s_col, in) == UI_EVENT_ACTIVATED && target->card)
    return run_option(s_col.activated, target);
  return UI_SCREEN_TYPE_MAIN;
}

/* ============================================================================
 * Draw and hints
 * ============================================================================ */

float ui_home_options_behind_alpha(void) {
  return ui_options_column_behind_alpha(&s_col);
}

void ui_home_options_draw_column(void) {
  ui_options_column_draw(&s_col);
}

void ui_home_options_draw_popup(void) {
  switch (s_active) {
    case POPUP_REPAIR:
      ui_popup_draw(&s_repair);
      break;
    case POPUP_VIA:
      ui_list_popup_draw(&s_via);
      break;
    case POPUP_ICON:
      ui_list_popup_draw(&s_icon);
      break;
    case POPUP_NONE:
      break;
  }
}

int ui_home_options_hints(UiHintItem out[UI_HINT_MAX_ITEMS]) {
  switch (s_active) {
    case POPUP_REPAIR:
      return ui_popup_hints(&s_repair, out);
    case POPUP_VIA:
      return ui_list_popup_hints(&s_via, out);
    case POPUP_ICON:
      return ui_list_popup_hints(&s_icon, out);
    case POPUP_NONE:
      break;
  }
  if (!ui_options_column_is_open(&s_col))
    return 0;
  out[0] = (UiHintItem){.action = UI_BTN_CONFIRM, .label = HINT_SELECT};
  out[1] = (UiHintItem){.action = UI_BTN_CANCEL, .label = HINT_BACK};
  return 2;
}
