/**
 * @file ui_pair_ip.h
 * @brief Pair new device > Enter IP address: keyboard, "Looking for console" popup and the
 *        failure popups (SPEC.md 3.1a)
 *
 * Home owns the flow while it is active. Every frame while ui_pair_ip_active() it calls
 * ui_pair_ip_input(), then ui_pair_ip_draw() and the hint row from ui_pair_ip_hints(), and it
 * treats the flow like any popup (no card refresh, no live input behind it).
 *
 * States: the system keyboard ("Console IP address"); the S popup "Looking for console" while the
 * unicast probe (discovery_probe.h) runs; a result popup ("Not an IP address" or "Console not
 * found", Close / Try again). Done on the keyboard checks the text with ip_address_parse() and
 * starts the probe. A console that answers is handed to the PIN screen, which owns the probed host
 * from then on (ui_pin_adopt_probed_host()). Cancel, Close, or a tap outside a popup ends the flow
 * on Home with the Pair new device item focused; Try again reopens the keyboard with the typed
 * text. Until the PIN screen takes it, the probe (and the host it found) belongs to the
 * discovery_probe module, which every exit here cancels.
 */

#pragma once

#include "ui/ui_hint_row.h"
#include "ui/ui_types.h"

/** ui_pair_ip_start() - Open the keyboard with empty text and begin the flow. */
void ui_pair_ip_start(void);

/** ui_pair_ip_active() - True from ui_pair_ip_start() until the flow ends or hands over to the PIN
 * screen. */
bool ui_pair_ip_active(void);

/**
 * ui_pair_ip_input() - Run this frame's input and the probe for the current state.
 * @in: This frame's input (a tapped hint already converted to a press).
 *
 * @return UI_SCREEN_TYPE_REGISTER_HOST when a console was found and the PIN screen should open,
 *         else UI_SCREEN_TYPE_MAIN
 */
UIScreenType ui_pair_ip_input(const UiInput *in);

/** ui_pair_ip_draw() - Draw the open popup, if any (nothing while the keyboard is up). */
void ui_pair_ip_draw(void);

/** ui_pair_ip_hints() - Fill @out with the open popup's hints; none while the keyboard is up.
 * Returns how many. */
int ui_pair_ip_hints(UiHintItem out[UI_HINT_MAX_ITEMS]);
