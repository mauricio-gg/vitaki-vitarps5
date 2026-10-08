/**
 * @file ui_popup.h
 * @brief C11 Popup: the one modal frame (SPEC.md C11, C13)
 *
 * Interactive component (SPEC 2.0): a struct plus open (the init), draw and input. A screen owns
 * one UiPopup as a member (a zeroed one is closed) and, every frame while it is open:
 *
 *   1. converts a tapped hint to a button press (ui_hint_row_tap) and calls ui_popup_input();
 *   2. acts on the event: UI_EVENT_ACTIVATED means button popup->activated was pressed,
 *      UI_EVENT_CANCELLED means Cancel or a tap on the scrim; the popup stays open until the
 *      screen calls ui_popup_close();
 *   3. draws ui_popup_draw() and then the hint row from ui_popup_hints().
 *
 * The popup is modal: while it is open the screen runs no other input. Opening it freezes the
 * screen behind it (ui_freeze.h): the opening frame is drawn without the screen's hint row and
 * without the popup, and the screen behind is not drawn live from the next frame on, unless
 * ui_freeze_is_ready() is false (the copy could not be made), when the screen keeps drawing live
 * under the scrim. Closing releases the freeze.
 *
 * Configurations (SPEC C12 to C14) are filled in through UiPopupSpec: a confirm popup is a title,
 * a body and two buttons; a result popup adds a tone icon and picks its default focus; a list or
 * grid popup has no buttons and draws its rows into popup->content between ui_popup_draw() and
 * the hint row, applying ui_popup_enter() to what it draws.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <vita2d.h>

#include "ui/ui_component.h"
#include "ui/ui_hint_row.h"
#include "ui/ui_text_button.h"
#include "ui/ui_text_wrap.h"
#include "ui/ui_theme.h"

typedef enum ui_popup_size_t {
  UI_POPUP_SIZE_S = 0,  ///< 480 x 256 at y 144: confirm, result, two-row list
  UI_POPUP_SIZE_M,      ///< 480 x 352 at y 96: icon picker grid
  UI_POPUP_SIZE_L,      ///< 480 x 432 at y 56: mapping list
} UiPopupSize;

/** What a popup shows. Strings are copied, except the button and hint labels, which are borrowed
 * and must outlive the popup (use string literals). */
typedef struct ui_popup_spec_t {
  UiPopupSize size;
  vita2d_texture *icon;  ///< optional, drawn UI_POPUP_ICON px high before the title
  uint32_t icon_color;  ///< ABGR tint of the icon; 0 reads UI_TEXT (a result popup passes its tone)
  const char *title;
  const char *subtitle;                       ///< optional
  const char *body;                           ///< optional, wrapped to UI_POPUP_BODY_LINES
  const char *buttons[UI_POPUP_MAX_BUTTONS];  ///< labels, left to right
  int button_count;                           ///< 0 to UI_POPUP_MAX_BUTTONS
  int default_focus;                          ///< button focused when it opens
  int cancel_button;                          ///< the button that means Cancel, or -1
  const char *cancel_label;                   ///< hint for Cancel when no button is the cancel
                                              ///< button; NULL reads "Cancel"
  const char *confirm_label;                  ///< hint for Confirm when there are no buttons;
                                              ///< NULL reads "Select"
} UiPopupSpec;

typedef struct ui_popup_t {
  bool open;
  UiPopupSize size;
  vita2d_texture *icon;
  uint32_t icon_color;
  char title[UI_POPUP_TEXT_MAX];
  char subtitle[UI_POPUP_TEXT_MAX];  ///< empty for none
  UiWrapped body;                    ///< count 0 for none
  UiTextButton buttons[UI_POPUP_MAX_BUTTONS];
  int button_count;
  int focus;          ///< focused button
  int cancel_button;  ///< -1 for none
  const char *cancel_label;
  const char *confirm_label;
  int activated;  ///< the button of the last UI_EVENT_ACTIVATED

  /* Layout, computed once in ui_popup_open() */
  UiRect visible;  ///< the card; a tap outside it cancels
  int left_x;      ///< the icon, the subtitle and the body start here
  int title_x;     ///< right of the icon, when there is one
  int title_y;
  int subtitle_y;  ///< 0 when there is no subtitle
  int body_y;
  UiRect content;  ///< free space between the header text and the button bar

  uint64_t enter_start_us;  ///< first frame the popup was shown; 0 until then
} UiPopup;

/**
 * ui_popup_open() - Fill @popup from @spec, compute its rects and open it.
 * Requests the background freeze, so the frame this is called in must be drawn without the
 * screen's hint row and without the popup (ui_freeze_is_capturing()). Opening an open popup
 * replaces its content.
 */
void ui_popup_open(UiPopup *popup, const UiPopupSpec *spec);

/** ui_popup_close() - Close @popup and release the background freeze. Safe on a closed popup. */
void ui_popup_close(UiPopup *popup);

/** ui_popup_is_open() - True from ui_popup_open() until ui_popup_close(). */
static inline bool ui_popup_is_open(const UiPopup *popup) {
  return popup->open;
}

/**
 * ui_popup_enter() - How far the popup has come in: @dy is how many pixels it is still below its
 * place (UI_RISE_PX at the start, 0 when settled) and @alpha its opacity, 0 to 1.
 */
void ui_popup_enter(const UiPopup *popup, int *dy, float *alpha);

/**
 * ui_popup_draw() - Draw the scrim, the card, its header text and its buttons. No state change.
 * Paper cost with a title, no icon or subtitle, n body lines and two buttons: scrim 1, card 17
 * (fill 9, border 8), title 1, body n, buttons 4 each (8 for the focused one).
 */
void ui_popup_draw(const UiPopup *popup);

/**
 * ui_popup_input() - Left/Right move between buttons, Confirm presses the focused one, a tap
 * presses the tapped one, Cancel or a tap outside the card cancels.
 * @return UI_EVENT_MOVED, UI_EVENT_ACTIVATED (see popup->activated), UI_EVENT_CANCELLED or
 *         UI_EVENT_NONE
 */
UiEvent ui_popup_input(UiPopup *popup, const UiInput *in);

/**
 * ui_popup_hints() - Fill @out with the hint row of the open popup (SPEC C06, Popups) and return
 * how many. The Confirm hint carries the focused button's label; the Cancel hint shows the cancel
 * label; a one-button popup, or one whose focused button is the cancel button, shows Confirm only.
 */
int ui_popup_hints(const UiPopup *popup, UiHintItem out[UI_HINT_MAX_ITEMS]);
