/**
 * @file ui_home_options.h
 * @brief Home's Options column and the popups its rows open: Re-pair, Connect via, Change icon
 *        (SPEC.md C05, C12, C13, section 3.1), and the "Could not connect" result popup (C14)
 *
 * Home hands this module the console the Options were opened for and the input of the frames in
 * which it is open; the module owns the C05 column and the four popups, decides what each row
 * does, and tells Home which screen to show next. It never reads Home's list: the console comes in
 * as a UiHomeOptionsTarget.
 *
 * A popup freezes the screen behind it (ui_freeze.h). While ui_home_options_popup_open() is true,
 * Home runs no other input and does not update its console list, so the row a popup is about
 * stays put. The Options column stays open behind a popup that is cancelled or does not leave the
 * screen (Re-pair cancelled, Connect via cancelled, a new icon chosen).
 */

#pragma once

#include <stdbool.h>

#include "ui/ui_component.h"
#include "ui/ui_connect_failure.h"
#include "ui/ui_console_status.h"
#include "ui/ui_hint_row.h"
#include "ui/ui_types.h"

/** The console the Options are about, as Home sees it this frame. */
typedef struct ui_home_options_target_t {
  const ConsoleCardInfo *card;  ///< NULL when no console is focused
  UiConsoleStatus status;       ///< the card's status
} UiHomeOptionsTarget;

/** Connects @host, over the PSN route when @force_psn; returns the screen to show. */
typedef UIScreenType (*UiHomeConnectFn)(VitaChiakiHost *host, bool force_psn);

/** ui_home_options_init() - Close the column and every popup. @connect is what a Connect row
 * and the Connect via popup run. */
void ui_home_options_init(UiHomeConnectFn connect);

/** ui_home_options_reset() - Close the column and the popups at once (Home is being entered). */
void ui_home_options_reset(void);

/** ui_home_options_column_open() - True while the Options column is open, even sliding out. */
bool ui_home_options_column_open(void);

/** ui_home_options_popup_open() - True while any of the popups is open. */
bool ui_home_options_popup_open(void);

/**
 * ui_home_options_open_failure() - Open the "Could not connect" popup (C14) for @failure.
 * @failure:   The failed connection (ui_connect_failure_take()).
 * @can_retry: The console is still in Home's list. When false the popup offers only Close.
 *
 * Call only while no popup is open. Try again connects the console as Confirm on its row would.
 * Logs the console and the reason once.
 */
void ui_home_options_open_failure(const UiConnectFailure *failure, bool can_retry);

/** ui_home_options_open() - Open the column on @target's first row (Triangle). */
void ui_home_options_open(const UiHomeOptionsTarget *target);

/** ui_home_options_refresh() - Point the open column at @target; closes it when the card is
 * NULL. Call every live frame while the column is open. */
void ui_home_options_refresh(const UiHomeOptionsTarget *target);

/**
 * ui_home_options_input() - Run this frame's input for the popup that is open, else the column.
 * @target: The console the Options are about; a row chosen in the column acts on it.
 * @return the screen to show next (UI_SCREEN_TYPE_MAIN to stay on Home)
 */
UIScreenType ui_home_options_input(const UiInput *in, const UiHomeOptionsTarget *target);

/** ui_home_options_behind_alpha() - Opacity for the Home layers behind the column. */
float ui_home_options_behind_alpha(void);

/** ui_home_options_draw_column() - Draw the Options column. */
void ui_home_options_draw_column(void);

/** ui_home_options_draw_popup() - Draw the open popup, if any. */
void ui_home_options_draw_popup(void);

/** ui_home_options_hints() - Fill @out with the hints of the open popup, else of the open column,
 * and return how many; 0 when neither is open. */
int ui_home_options_hints(UiHintItem out[UI_HINT_MAX_ITEMS]);
