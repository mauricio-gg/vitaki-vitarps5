/**
 * @file ui_pair_ip.c
 * @brief Pair new device > Enter IP address (see ui_pair_ip.h)
 */

#include "ui/ui_pair_ip.h"

#include <stdint.h>
#include <string.h>

#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>

#include "context.h"
#include "discovery_probe.h"
#include "ip_address.h"
#include "ui/ui_component.h"
#include "ui/ui_home.h"
#include "ui/ui_internal.h"
#include "ui/ui_popup.h"
#include "ui/ui_result_copy.h"
#include "ui/ui_result_popup.h"
#include "ui/ui_screens.h"
#include "ui/ui_spinner.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"
#include "ui/ui_toast.h"
#include "ui/ui_utf16.h"

/** What the flow is doing. */
typedef enum pair_ip_phase_t {
  PAIR_IP_IDLE = 0,
  PAIR_IP_KEYBOARD,  ///< the system keyboard is up
  PAIR_IP_LOOKING,   ///< the probe runs; "Looking for console" is open
  PAIR_IP_FAILED,    ///< a result popup is open
} PairIpPhase;

/* Copy (SPEC 3.1a; CEO-approved 2026-10-09) */
static const char KEYBOARD_TITLE[] = "Console IP address";
static const char LOOKING_TITLE[] = "Looking for console";
static const char LOOKING_LINE[] = "Contacting the console...";
static const char BUTTON_CANCEL[] = "Cancel";
static const char TOAST_KEYBOARD_FAILED[] = "Could not open text input";

/** Characters the keyboard accepts: room for a dotted address with stray spaces around it. */
#define KEYBOARD_CHARS 32
/** Bytes of the body text of a failure popup. */
#define BODY_MAX 160

static PairIpPhase s_phase = PAIR_IP_IDLE;
static UiPopup s_popup;
/** What the user typed, as the keyboard returned it: the prefill of Try again. */
static SceWChar16 s_typed[KEYBOARD_CHARS + 1];
static SceWChar16 s_input[KEYBOARD_CHARS + 1];
static SceWChar16 s_title[sizeof(KEYBOARD_TITLE)];
/** The typed text as UTF-8, then the canonical address once it parsed. */
static char s_text[KEYBOARD_CHARS * 3 + 1];
static char s_ip[IP_ADDRESS_STRING_SIZE];

/** End the flow on Home with the Pair new device item focused. */
static void end_flow(void) {
  s_phase = PAIR_IP_IDLE;
  ui_home_focus_pair_item();
}

/** Open the system keyboard prefilled with s_typed; ends the flow when it will not open. */
static void open_keyboard(void) {
  for (size_t i = 0; i < sizeof(KEYBOARD_TITLE); i++)
    s_title[i] = (SceWChar16)KEYBOARD_TITLE[i];
  memset(s_input, 0, sizeof(s_input));

  SceImeDialogParam param;
  sceImeDialogParamInit(&param);
  param.supportedLanguages = 0;
  param.languagesForced = SCE_FALSE;
  param.type = SCE_IME_TYPE_BASIC_LATIN;
  param.option = 0;
  param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;
  param.maxTextLength = KEYBOARD_CHARS;
  param.title = s_title;
  param.initialText = s_typed;
  param.inputTextBuffer = s_input;

  const int ret = sceImeDialogInit(&param);
  if (ret < 0) {
    LOGE("Enter IP address: the keyboard did not open: 0x%08x", (unsigned int)ret);
    ui_toast_show(TOAST_KEYBOARD_FAILED, UI_TOAST_ERR);
    end_flow();
    return;
  }
  s_phase = PAIR_IP_KEYBOARD;
}

void ui_pair_ip_start(void) {
  memset(s_typed, 0, sizeof(s_typed));
  open_keyboard();
}

bool ui_pair_ip_active(void) {
  return s_phase != PAIR_IP_IDLE;
}

/** Open the result popup for @failure over whatever popup is open. */
static void show_failure(UiPairIpFailure failure) {
  char body[BODY_MAX];
  UiResultCopy copy;
  if (!ui_result_copy_pair_ip(failure, s_ip, body, sizeof(body), &copy)) {
    LOGE("Enter IP address: no popup copy for failure %d", (int)failure);
    ui_popup_close(&s_popup);
    end_flow();
    return;
  }
  ui_result_popup_open(&s_popup, &copy, body);
  s_phase = PAIR_IP_FAILED;
}

/** Open "Looking for console" for the address in s_ip, whose probe is already running. */
static void show_looking(void) {
  ui_popup_open(&s_popup, &(UiPopupSpec){
                              .size = UI_POPUP_SIZE_S,
                              .title = LOOKING_TITLE,
                              .subtitle = s_ip,
                              .buttons = {BUTTON_CANCEL},
                              .button_count = 1,
                              .cancel_button = 0,
                          });
  s_phase = PAIR_IP_LOOKING;
}

/** Done on the keyboard: check the typed text, then start the probe or say why not. */
static void submit_typed_text(void) {
  ui_utf16_to_utf8(s_typed, KEYBOARD_CHARS + 1, s_text, sizeof(s_text));
  uint8_t octets[IP_ADDRESS_OCTET_COUNT];
  if (!ip_address_parse(s_text, octets) || !ip_address_format(octets, s_ip, sizeof(s_ip))) {
    s_ip[0] = '\0';
    show_failure(UI_PAIR_IP_NOT_AN_ADDRESS);
    return;
  }
  if (!discovery_probe_start(s_ip)) {
    show_failure(UI_PAIR_IP_NOT_FOUND);
    return;
  }
  show_looking();
}

/** Collect the keyboard once it has finished. */
static void poll_keyboard(void) {
  if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED)
    return;
  SceImeDialogResult result;
  memset(&result, 0, sizeof(result));
  sceImeDialogGetResult(&result);
  sceImeDialogTerm();
  if (result.button != SCE_IME_DIALOG_BUTTON_ENTER) {
    end_flow();
    return;
  }
  memcpy(s_typed, s_input, sizeof(s_typed));
  s_typed[KEYBOARD_CHARS] = 0;
  submit_typed_text();
}

/** The probe stopped without a console: drop it and say so. */
static void probe_failed(void) {
  discovery_probe_cancel();
  show_failure(UI_PAIR_IP_NOT_FOUND);
}

/**
 * update_looking() - Cancel stops the probe; otherwise advance it. A console found goes to the
 * PIN screen, which owns the probed host from then on.
 */
static UIScreenType update_looking(const UiInput *in) {
  const UiEvent event = ui_popup_input(&s_popup, in);
  if (event == UI_EVENT_ACTIVATED || event == UI_EVENT_CANCELLED) {
    discovery_probe_cancel();
    ui_popup_close(&s_popup);
    end_flow();
    return UI_SCREEN_TYPE_MAIN;
  }

  switch (discovery_probe_poll()) {
    case DISCOVERY_PROBE_FOUND: {
      VitaChiakiHost *host = discovery_probe_take_host();
      if (!host) {
        LOGE("Enter IP address: probe reported a console but gave no host for %s", s_ip);
        probe_failed();
        break;
      }
      ui_popup_close(&s_popup);
      s_phase = PAIR_IP_IDLE;
      return ui_screens_pair_probed_host(host);
    }
    case DISCOVERY_PROBE_NOT_FOUND:
    case DISCOVERY_PROBE_ERROR:
    case DISCOVERY_PROBE_IDLE:
      probe_failed();
      break;
    case DISCOVERY_PROBE_SEARCHING:
      break;
  }
  return UI_SCREEN_TYPE_MAIN;
}

/** Close ends the flow; Try again reopens the keyboard with the typed text. */
static void update_failed(const UiInput *in) {
  const int choice = ui_result_popup_choice(&s_popup, ui_popup_input(&s_popup, in));
  if (choice < 0)
    return;
  ui_popup_close(&s_popup);
  if (choice == 0)
    end_flow();
  else
    open_keyboard();
}

UIScreenType ui_pair_ip_input(const UiInput *in) {
  switch (s_phase) {
    case PAIR_IP_KEYBOARD:
      poll_keyboard();
      break;
    case PAIR_IP_LOOKING:
      return update_looking(in);
    case PAIR_IP_FAILED:
      update_failed(in);
      break;
    case PAIR_IP_IDLE:
      break;
  }
  return UI_SCREEN_TYPE_MAIN;
}

void ui_pair_ip_draw(void) {
  if (s_phase != PAIR_IP_LOOKING && s_phase != PAIR_IP_FAILED)
    return;
  ui_popup_draw(&s_popup);
  if (s_phase != PAIR_IP_LOOKING || s_popup.enter_start_us == 0)
    return;

  int dy;
  float k;
  ui_popup_enter(&s_popup, &dy, &k);
  ui_layer_set_alpha(k);
  const int y = s_popup.content.y + dy;
  ui_spinner_draw(UI_SPINNER_INLINE, s_popup.content.x + UI_PAIR_LOOKING_SPINNER_CX,
                  y + UI_T16_LINE / 2);
  ui_text_draw_face_centered_v(UI_FACE_T16, s_popup.content.x + UI_PAIR_LOOKING_TEXT_X, y,
                               UI_T16_LINE, UI_TEXT_2, LOOKING_LINE);
  ui_layer_set_alpha(1.0f);
}

int ui_pair_ip_hints(UiHintItem out[UI_HINT_MAX_ITEMS]) {
  if (!ui_popup_is_open(&s_popup))
    return 0;
  return ui_popup_hints(&s_popup, out);
}
