/**
 * @file ui_pin.h
 * @brief The PIN screen: pair a console with its 8-digit session PIN (SPEC.md section 3.2, 3.3)
 *
 * On UI_SCREEN_TYPE_REGISTER_HOST. An XMB page (page frame, top bar, hint row) around one C17
 * PinField. Register starts the pairing attempt (host_register()); while it runs the field is
 * locked and the prompt reads "Pairing..."; the screen polls the attempt every frame and, when
 * it reports, opens the C14 result popup over the frozen screen. Paired goes back to Home with the
 * console focused; a failure offers Close (Home) or Try again (this screen, empty).
 *
 * Paper cost with the digits zone focused: wash 1, title 3, top bar 5, console line 1, prompt 1,
 * boxes 2 each and 6 more for the focused one (empty boxes 1), buttons 4 each (8 for the
 * focused one), hint row 2 per hint with a glyph. Over a popup: the frozen copy 1, scrim 1, card
 * 17, title 2, body 1 to 3, two buttons 8 to 12, hint row.
 */

#pragma once

#include "ui/ui_types.h"

/** ui_pin_init() - Load the result popup's icons. Call once at start-up. */
void ui_pin_init(void);

/**
 * ui_pin_adopt_probed_host() - Take ownership of a host from discovery_probe_take_host(). Call
 * right before switching to this screen, with context.active_host set to @host.
 *
 * The screen then owns it until it leaves: a successful registration hands it to the registered
 * table, saves the address as a manual host and focuses the console on Home; every other exit
 * (Cancel on the field, Close on a failure, a cancelled attempt) releases it with
 * discovery_probe_free_host() once no attempt is running. Try again keeps it.
 */
void ui_pin_adopt_probed_host(VitaChiakiHost *host);

/**
 * ui_pin_on_enter() - Open the screen for context.active_host with an empty field.
 * Drops a pairing result nobody collected, so it cannot show for this console.
 */
void ui_pin_on_enter(void);

/**
 * ui_pin_frame() - Run one frame: poll a running attempt, read input, draw.
 * @return UI_SCREEN_TYPE_REGISTER_HOST to stay, UI_SCREEN_TYPE_MAIN to go back to Home
 */
UIScreenType ui_pin_frame(void);
