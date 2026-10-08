/**
 * @file ui_pin.c
 * @brief The PIN screen (SPEC.md sections 3.2 and 3.3)
 */

#include "ui/ui_pin.h"

#include <stdio.h>

#include <vita2d.h>

#include "context.h"
#include "host.h"
#include "host_registration.h"
#include "ui/ui_chrome_layout.h"
#include "ui/ui_constants.h"
#include "ui/ui_freeze.h"
#include "ui/ui_home.h"
#include "ui/ui_hint_row.h"
#include "ui/ui_input.h"
#include "ui/ui_page_frame.h"
#include "ui/ui_pin_field.h"
#include "ui/ui_popup.h"
#include "ui/ui_result_copy.h"
#include "ui/ui_result_popup.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"
#include "ui/ui_top_bar.h"

/** What the screen is doing. */
typedef enum pin_phase_t {
  PIN_PHASE_ENTRY = 0,  ///< the user types the PIN
  PIN_PHASE_PAIRING,    ///< the attempt runs; only Cancel is live
  PIN_PHASE_RESULT,     ///< the result popup is open
} PinPhase;

#define RESULT_BODY_MAX 256

/* Copy (SPEC section 5; "Pairing..." is new and awaits sign-off) */
static const char TITLE_FORMAT[] = "%s Console Registration";
static const char SUB_FORMAT[] = "%s (%s)";
static const char PROMPT_FORMAT[] = "Enter the 8-digit session PIN displayed on your %s:";
static const char PROMPT_PAIRING[] = "Pairing...";
static const char MODEL_PS5[] = "PS5";
static const char MODEL_PS4[] = "PS4";

static UiPinField s_field;
static UiPopup s_popup;
static UiHintLayout s_hints;
static PinPhase s_phase = PIN_PHASE_ENTRY;
static VitaChiakiHost *s_host = NULL;
/** The result the open popup shows (a paired console goes to Home, a failure offers Try again). */
static HostRegistrationResult s_result = HOST_REGISTRATION_FAILED;

/* Text built once per entry, never per frame. */
static char s_title[UI_PIN_TEXT_MAX];
static char s_sub[UI_PIN_TEXT_MAX];
static char s_prompt[UI_PIN_TEXT_MAX];
static int s_sub_x;
static int s_prompt_x;
static int s_pairing_x;

void ui_pin_init(void) {
  ui_result_popup_init();
  ui_popup_close(&s_popup);
  s_hints.count = 0;
}

/* ============================================================================
 * Entry
 * ============================================================================ */

/** Width function for ui_ellipsize_to_fit(): the console line's face. */
static int measure_sub(const char *text, void *ctx) {
  (void)ctx;
  return ui_text_face_width(UI_FACE_T16, text);
}

/** Build the title, the console line and the prompt for s_host, and where each is drawn. */
static void build_texts(void) {
  const char *model = chiaki_target_is_ps5(s_host->target) ? MODEL_PS5 : MODEL_PS4;
  const char *name = s_host->display_name[0] ? s_host->display_name : s_host->hostname;

  snprintf(s_title, sizeof(s_title), TITLE_FORMAT, model);
  s_sub_x = UI_PAGE_TITLE_X + ui_text_face_width(UI_FACE_T28, s_title) + UI_PIN_SUB_GAP;

  char full_sub[UI_PIN_TEXT_MAX];
  if (s_host->hostname[0])
    snprintf(full_sub, sizeof(full_sub), SUB_FORMAT, name, s_host->hostname);
  else
    snprintf(full_sub, sizeof(full_sub), "%s", name);
  ui_ellipsize_to_fit(full_sub, UI_CONTENT_RIGHT - s_sub_x, measure_sub, NULL, s_sub,
                      sizeof(s_sub));

  snprintf(s_prompt, sizeof(s_prompt), PROMPT_FORMAT, model);
  s_prompt_x = (VITA_WIDTH - ui_text_face_width(UI_FACE_T20, s_prompt)) / 2;
  s_pairing_x = (VITA_WIDTH - ui_text_face_width(UI_FACE_T20, PROMPT_PAIRING)) / 2;
}

/** Start over with an empty field for s_host. */
static void reset_entry(void) {
  ui_popup_close(&s_popup);
  ui_pin_field_init(&s_field);
  s_phase = PIN_PHASE_ENTRY;
  s_hints.count = 0;
}

void ui_pin_on_enter(void) {
  HostRegistrationResult stale;
  if (host_registration_take_result(&stale, NULL, NULL, 0))
    LOGD("PIN screen: dropped an uncollected pairing result (%s)",
         host_registration_result_name(stale));

  s_host = context.active_host;
  if (!s_host) {
    LOGE("PIN screen opened with no active console");
    return;
  }
  build_texts();
  reset_entry();
}

/* ============================================================================
 * Pairing
 * ============================================================================ */

/**
 * show_result() - Handle a finished attempt: no popup for a cancel, else open the result popup.
 * @name: The console's name as the attempt saw it.
 *
 * @return UI_SCREEN_TYPE_MAIN for a cancel (back to Home), otherwise the PIN screen
 */
static UIScreenType show_result(HostRegistrationResult result, const char *name) {
  char body[RESULT_BODY_MAX];
  UiResultCopy copy;
  if (!ui_result_copy_pairing(result, name, body, sizeof(body), &copy)) {
    if (result != HOST_REGISTRATION_CANCELLED)
      LOGE("PIN screen: pairing result %s has no popup", host_registration_result_name(result));
    return UI_SCREEN_TYPE_MAIN;
  }
  s_result = result;
  s_phase = PIN_PHASE_RESULT;
  ui_result_popup_open(&s_popup, &copy, body);
  return UI_SCREEN_TYPE_REGISTER_HOST;
}

/** Take the attempt's result if it has finished. Returns the screen to show next. */
static UIScreenType collect_result(void) {
  HostRegistrationResult result;
  char name[VITA_HOST_DISPLAY_NAME_LEN];
  if (!host_registration_take_result(&result, NULL, name, sizeof(name)))
    return UI_SCREEN_TYPE_REGISTER_HOST;
  return show_result(result, name);
}

/**
 * start_pairing() - Send the PIN in the field to the console and lock the field.
 *
 * Any result left from an earlier attempt is dropped first. host_register() reports its own
 * failures to start as a result; when it fails without one, the screen reports FAILED itself so
 * it never waits for an attempt that is not running.
 *
 * @return the screen to show next
 */
static UIScreenType start_pairing(void) {
  HostRegistrationResult stale;
  host_registration_take_result(&stale, NULL, NULL, 0);

  const int pin = ui_pin_field_value(&s_field);
  if (host_register(s_host, pin) != 0) {
    HostRegistrationResult result = HOST_REGISTRATION_FAILED;
    char name[VITA_HOST_DISPLAY_NAME_LEN];
    if (!host_registration_take_result(&result, NULL, name, sizeof(name))) {
      LOGE("PIN screen: host_register() failed without a result for \"%s\"", s_host->hostname);
      snprintf(name, sizeof(name), "%s",
               s_host->display_name[0] ? s_host->display_name : s_host->hostname);
    }
    ui_pin_field_set_locked(&s_field, true);
    return show_result(result, name);
  }
  s_phase = PIN_PHASE_PAIRING;
  ui_pin_field_set_locked(&s_field, true);
  return UI_SCREEN_TYPE_REGISTER_HOST;
}

/* ============================================================================
 * Input
 * ============================================================================ */

/** Forward input to the field; returns the screen to show next. */
static UIScreenType update_field(const UiInput *in) {
  switch (ui_pin_field_input(&s_field, in)) {
    case UI_EVENT_ACTIVATED:
      return start_pairing();
    case UI_EVENT_CANCELLED:
      if (s_phase == PIN_PHASE_PAIRING) {
        host_registration_cancel();
        return UI_SCREEN_TYPE_REGISTER_HOST;
      }
      return UI_SCREEN_TYPE_MAIN;
    default:
      return UI_SCREEN_TYPE_REGISTER_HOST;
  }
}

/**
 * update_result_popup() - Drive the result popup. Paired goes back to Home with the console
 * focused; Close goes back to Home; Try again opens this screen again, empty.
 *
 * @return the screen to show next
 */
static UIScreenType update_result_popup(const UiInput *in) {
  const int choice = ui_result_popup_choice(&s_popup, ui_popup_input(&s_popup, in));
  if (choice < 0)
    return UI_SCREEN_TYPE_REGISTER_HOST;

  ui_popup_close(&s_popup);
  if (s_result == HOST_REGISTRATION_PAIRED) {
    ui_home_focus_console(s_host);
    return UI_SCREEN_TYPE_MAIN;
  }
  if (choice == 0)
    return UI_SCREEN_TYPE_MAIN;
  reset_entry();
  return UI_SCREEN_TYPE_REGISTER_HOST;
}

/** Run this frame's input and attempt bookkeeping for the current phase. */
static UIScreenType update(const UiInput *in) {
  if (s_phase == PIN_PHASE_RESULT)
    return update_result_popup(in);

  if (s_phase == PIN_PHASE_PAIRING) {
    host_registration_poll();
    const UIScreenType next = collect_result();
    if (next != UI_SCREEN_TYPE_REGISTER_HOST || s_phase == PIN_PHASE_RESULT)
      return next;
  }
  return update_field(in);
}

/* ============================================================================
 * Draw
 * ============================================================================ */

/** Draw the live screen: page frame, top bar, console line, prompt and the field. */
static void draw_live(void) {
  ui_page_frame_draw(UI_PAGE_ICON_LOCK, s_title);
  ui_top_bar_draw(NULL);
  ui_text_draw_face_centered_v(UI_FACE_T16, s_sub_x, UI_TITLE_Y, UI_PAGE_TITLE_H, UI_TEXT_2, s_sub);
  const bool pairing = s_phase == PIN_PHASE_PAIRING;
  ui_text_draw_face_centered_v(UI_FACE_T20, pairing ? s_pairing_x : s_prompt_x, UI_PIN_PROMPT_Y,
                               UI_T20_LINE, UI_TEXT_2, pairing ? PROMPT_PAIRING : s_prompt);
  ui_pin_field_draw(&s_field);
}

UIScreenType ui_pin_frame(void) {
  if (!s_host) {
    LOGE("PIN screen has no console; back to Home");
    return UI_SCREEN_TYPE_MAIN;
  }

  /* The freeze state at the start of the frame decides whether ui.c already drew the frozen
   * screen (a popup is open): then the screen behind is not drawn live. */
  const bool frozen = ui_freeze_is_ready();

  /* A tapped hint acts as that button pressed, in the same frame. */
  UiInput in = *ui_input_snapshot();
  const uint32_t tapped =
      ui_hint_row_tap(&s_hints, &in) & (UI_BTN_CONFIRM | UI_BTN_CANCEL | UI_BTN_CLEAR);
  if (tapped) {
    in.pressed |= tapped;
    in.touch.pressed = false;
    in.touch.released = false;
    in.touch.down = false;
  }

  const UIScreenType next = update(&in);

  /* True when the popup was opened this frame: the frame becomes the popup's background copy, so
   * it is drawn without the popup and without the hint row. */
  const bool capturing = ui_freeze_is_capturing();
  if (!frozen)
    draw_live();
  if (!capturing)
    ui_popup_draw(&s_popup);

  if (capturing) {
    s_hints.count = 0;
  } else {
    UiHintItem hints[UI_HINT_MAX_ITEMS];
    const int count = ui_popup_is_open(&s_popup) ? ui_popup_hints(&s_popup, hints)
                                                 : ui_pin_field_hints(&s_field, hints);
    ui_hint_row_layout(&s_hints, hints, count);
    ui_hint_row_draw(&s_hints);
  }
  return next;
}
