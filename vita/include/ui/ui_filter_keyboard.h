/**
 * @file ui_filter_keyboard.h
 * @brief The system keyboard for a console filter, shared by Home's Filter row and the Pair new
 *        device popup's (SPEC.md C02 "Filter item", C29)
 *
 * One keyboard at a time. The caller opens it with the current text prefilled, polls it every
 * frame and applies the text itself: Done applies it (empty text clears the filter), Cancel
 * leaves the filter as it was.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

/** Where the keyboard is, as ui_filter_keyboard_poll() reports it. */
typedef enum ui_filter_keyboard_result_t {
  UI_FILTER_KB_IDLE = 0,  ///< not open
  UI_FILTER_KB_OPEN,      ///< still open
  UI_FILTER_KB_DONE,      ///< Done: the typed text was written (it may be empty)
  UI_FILTER_KB_CANCEL,    ///< Cancel or closed: the text was not written
} UiFilterKeyboardResult;

/**
 * ui_filter_keyboard_open() - Open the keyboard.
 * @title:   Keyboard title (ASCII).
 * @initial: Text to prefill (UTF-8), cut to the filter length.
 *
 * @return true when it opened; false when one is already open or the system refused (logged)
 */
bool ui_filter_keyboard_open(const char *title, const char *initial);

/** ui_filter_keyboard_running() - True from a successful open until poll reports Done or Cancel. */
bool ui_filter_keyboard_running(void);

/**
 * ui_filter_keyboard_poll() - Check the keyboard; call every frame.
 * @text: Receives the typed text (UTF-8) on UI_FILTER_KB_DONE; at least UI_FILTER_TEXT_MAX bytes.
 * @size: Size of @text.
 */
UiFilterKeyboardResult ui_filter_keyboard_poll(char *text, size_t size);
