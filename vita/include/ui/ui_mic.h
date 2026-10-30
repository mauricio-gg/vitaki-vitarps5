/**
 * @file ui_mic.h
 * @brief The on-screen microphone control of the stream overlay (GH #474)
 *
 * With the microphone setting on and the mic running, the overlay shows one of two controls:
 *  - the bottom-centre button (setting "Display mic button on-screen" on): a 64 x 32 pill with a
 *    mic icon, faint at rest. A tap toggles the mute; the pill then turns opaque and widens to
 *    show "Mic on" or "Mic muted" for UI_MIC_LABEL_MS before shrinking back. Width and opacity
 *    ease towards their targets over time (no frame counting, no allocation).
 *  - the small top-left corner icon (the button setting off): live or muted, a tap toggles.
 * If the mic failed to start or died, a "Mic unavailable" pill shows for UI_MIC_FAILED_MS and no
 * control is drawn. With the microphone setting off nothing is drawn and no touch area exists.
 *
 * Touches on the control never reach the game (mic_touch.h). The input thread cannot take the
 * mic's lifecycle lock, so a tap or the mute combo only raises a request (ui_mic_request_toggle());
 * the UI thread applies it in ui_mic_tick(). Everything else here runs on the UI thread.
 */

#pragma once

#include "mic_touch.h"

/** Bakes the mic icons once. Call after vita2d is initialised. */
void ui_mic_init(void);

/** A stream is starting: forget the previous stream's label, failure and animation. */
void ui_mic_on_stream_start(void);

/** The stream ended: no touch area exists any more. */
void ui_mic_on_stream_stop(void);

/** Any thread: ask for the mute to be toggled (a tap on the control, or the mute combo). */
void ui_mic_request_toggle(void);

/** UI thread, every streaming pass: applies a pending toggle request, notes a failure, and
 * publishes the touch area for the input thread. */
void ui_mic_tick(void);

/** UI thread, from the stream overlay: draws the control or the "Mic unavailable" pill. */
void ui_mic_draw(void);

/** Input thread: the control now on screen (MIC_TOUCH_MODE_NONE when there is none). */
MicTouchMode ui_mic_touch_mode(void);

/** Input thread: the button's width while its label shows, 0 at rest. */
int ui_mic_touch_label_w(void);
