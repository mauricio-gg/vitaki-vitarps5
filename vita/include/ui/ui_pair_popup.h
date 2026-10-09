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
 * row now in its place. With more than UI_FILTER_ROW_MAX_PLAIN_CONSOLES consoles, or a filter
 * active, the first row of the list is a Filter row (the popup's own text, kept apart from
 * Home's) that scrolls with the list; L and R page the list.
 *
 * Home owns one (this module keeps its state in the file). Every frame while it is open Home
 * converts a tapped hint to a button press, calls ui_pair_popup_input(), acts on the result, then
 * draws ui_pair_popup_draw() and the hint row from ui_pair_popup_hints().
 */

#pragma once

#include "ui/ui_component.h"
#include "ui/ui_hint_row.h"
#include "ui/ui_types.h"

/** What the popup asks of its owner after a frame of input. */
typedef enum ui_pair_popup_result_t {
  UI_PAIR_POPUP_NONE = 0,  ///< stay open
  UI_PAIR_POPUP_PAIR,      ///< a console was chosen: see the host out-parameter; popup still open
  UI_PAIR_POPUP_ENTER_IP,  ///< Enter IP address was chosen; popup still open
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
 * Up and Down move through the Filter row, the consoles and then Enter IP address (no wrap); L and
 * R move one page; Confirm or a tap acts on a row (the Filter row opens the system keyboard, a tap
 * on its Clear button clears); Start opens the keyboard or clears an active filter and Square on
 * the Filter row clears it, both only while the Filter row is shown; a vertical swipe over the
 * viewport scrolls it one row per UI_ROW_SWIPE_PX and the focus follows. Confirm or a tap on Enter
 * IP address returns UI_PAIR_POPUP_ENTER_IP. While the keyboard is up the popup takes no other
 * input. The list is refreshed from discovery first.
 */
UiPairPopupResult ui_pair_popup_input(const UiInput *in, VitaChiakiHost **chosen);

/** ui_pair_popup_draw() - Draw the popup (frame, instructions, rows, pinned row), rising and
 * fading in. Paper cost: see SPEC C29 "Draws". */
void ui_pair_popup_draw(void);

/** ui_pair_popup_hints() - Fill @out: Confirm "Pair" on a console, "Filter" on the Filter row
 * (with Square "Clear" while a filter is active) or "Enter IP" on the pinned row; "L R Page" when
 * the list has more rows than fit (low priority, not tappable: Home drops a tap on it); then
 * Cancel "Close". Returns how many. */
int ui_pair_popup_hints(UiHintItem out[UI_HINT_MAX_ITEMS]);
