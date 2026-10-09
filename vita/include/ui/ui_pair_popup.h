/**
 * @file ui_pair_popup.h
 * @brief C29 PairPopup: the L popup that lists the unpaired consoles found on the network
 *        (SPEC.md C29, section 3.1a)
 *
 * A configuration of the C11 popup (ui_popup.h) with its own content: the Link Device
 * instructions, the section label, a four-row viewport of consoles that scrolls, and the pinned
 * "Enter IP address" row under it. The rows are the unpaired discovered consoles
 * (ui_pair_hosts_collect()) read again every frame, so the list follows discovery while the popup
 * is open; the focused console is followed by host, and when it disappears the focus moves to the
 * row now in its place.
 *
 * Home owns one (this module keeps its state in the file). Every frame while it is open Home
 * converts a tapped hint to a button press, calls ui_pair_popup_input(), acts on the result, then
 * draws ui_pair_popup_draw() and the hint row from ui_pair_popup_hints().
 *
 * The Filter row and L/R paging of C29 are not built yet: the list is the rows array plus the
 * pinned row, and the focus is an index into it, so they are added without reshaping either.
 */

#pragma once

#include "ui/ui_component.h"
#include "ui/ui_hint_row.h"
#include "ui/ui_types.h"

/** What the popup asks of its owner after a frame of input. */
typedef enum ui_pair_popup_result_t {
  UI_PAIR_POPUP_NONE = 0,  ///< stay open
  UI_PAIR_POPUP_PAIR,      ///< a console was chosen: see the host out-parameter; popup still open
  UI_PAIR_POPUP_CLOSE,     ///< Circle or a tap outside the card; popup still open
} UiPairPopupResult;

/** ui_pair_popup_init() - Bake the chevron of the Enter IP address row. Call once at start-up. */
void ui_pair_popup_init(void);

/**
 * ui_pair_popup_open() - Open the popup with the discovery clock at zero.
 * Requests the background freeze like ui_popup_open(): the frame this is called in must be drawn
 * without the screen's hint row and without the popup (ui_freeze_is_capturing()). Focus starts on
 * the first console, or on Enter IP address when none is listed.
 */
void ui_pair_popup_open(void);

/** ui_pair_popup_close() - Close the popup and release the freeze. Safe when closed. */
void ui_pair_popup_close(void);

/** ui_pair_popup_is_open() - True from ui_pair_popup_open() until ui_pair_popup_close(). */
bool ui_pair_popup_is_open(void);

/**
 * ui_pair_popup_input() - Run this frame's input.
 * @in:     This frame's input (a tapped hint already converted to a press).
 * @chosen: Out: the console to pair when the result is UI_PAIR_POPUP_PAIR, else untouched.
 *
 * Up and Down move through the consoles and then Enter IP address (no wrap); Confirm or a tap
 * acts on a row; a vertical swipe over the viewport scrolls it one row per UI_ROW_SWIPE_PX and
 * the focus follows. Confirm or a tap on Enter IP address runs ui_pair_popup_enter_ip_address().
 * The list is refreshed from discovery first.
 */
UiPairPopupResult ui_pair_popup_input(const UiInput *in, VitaChiakiHost **chosen);

/** ui_pair_popup_draw() - Draw the popup (frame, instructions, rows, pinned row), rising and
 * fading in. Paper cost about 30 to 40 (SPEC C29). */
void ui_pair_popup_draw(void);

/** ui_pair_popup_hints() - Fill @out: Confirm "Pair" on a console or "Enter IP" on the pinned
 * row, then Cancel "Close". Returns how many. */
int ui_pair_popup_hints(UiHintItem out[UI_HINT_MAX_ITEMS]);
