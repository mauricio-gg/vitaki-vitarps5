/**
 * @file ui_profile_account.c
 * @brief The Account group of the Profile page (see ui_profile_account.h)
 */

#include "ui/ui_profile_account.h"

#include <stdlib.h>
#include <string.h>

#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>

#include "config.h"
#include "context.h"
#include "psn_account_id.h"
#include "ui/ui_settings_actions.h"
#include "ui/ui_theme.h"
#include "ui/ui_toast.h"
#include "ui/ui_utf16.h"

/* Copy (SPEC 3.7 and the copy deck, "Profile, Account ID (#201)") */
static const char ID_UNSET[] = "Not Set";
static const char MANUAL_UNSET[] = "Not set";
static const char SOURCE_VITA_WORD[] = "Vita";
static const char SOURCE_MANUAL_WORD[] = "Manual";
static const char SOURCE_DESCRIPTION[] =
    "Which Account ID the app uses. The other one stays saved.";
static const char SOURCE_DESCRIPTION_NO_MANUAL[] =
    "Choose Manual to type an Account ID, for example for a different PS5 account.";
static const char MANUAL_DESCRIPTION_NONE[] =
    "Type an Account ID: base64 (12 characters ending in =) or a number.";
static const char MANUAL_DESCRIPTION_SAVED[] =
    "Type a different Account ID. Base64 (ends in =) or a number.";
static const char HINT_ENTER_ID[] = "Enter ID";
static const char HINT_EDIT_ID[] = "Edit ID";
static const char KEYBOARD_TITLE[] = "Enter Account ID";
static const char TOAST_USING_VITA[] = "Using the Vita Account ID";
static const char TOAST_USING_MANUAL[] = "Using the Manual Account ID";
static const char TOAST_SAVED[] = "Manual Account ID saved";
static const char TOAST_SAVED_IN_USE[] = "Manual Account ID saved. Now in use";
static const char TOAST_BAD_FORMAT[] =
    "Invalid Account ID. Use 12 characters ending in =, or a number";
static const char TOAST_ALL_ZERO[] = "Invalid Account ID. It cannot be all zeros";
static const char TOAST_KEYBOARD_FAILED[] = "Could not open text input";

/** Characters the keyboard accepts: a base64 ID is 12, a decimal number at most 20. */
#define KEYBOARD_CHARS 32

/* The keyboard's buffers are static: no allocation while the page runs. */
static bool s_keyboard_open = false;
static SceWChar16 s_keyboard_input[KEYBOARD_CHARS + 1];
static SceWChar16 s_keyboard_initial[KEYBOARD_CHARS + 1];
static SceWChar16 s_keyboard_title[sizeof(KEYBOARD_TITLE)];

/* ============================================================================
 * State
 * ============================================================================ */

/** True when a valid typed ID is saved. */
static bool has_manual(void) {
  return psn_account_id_b64_is_valid(context.config.psn_account_id_manual);
}

/** True when @id holds characters. */
static bool is_set(const char *id) {
  return id && id[0];
}

/** True when @source is the one in use and there is an ID to use: a fallback shows truthfully. */
static bool source_in_use(PsnAccountIdSource source) {
  PsnAccountIdSource used;
  const char *id = config_psn_account_id_in_use(&context.config, &used);
  return is_set(id) && used == source;
}

bool ui_profile_account_vita_in_use(void) {
  return source_in_use(PSN_ACCOUNT_ID_SOURCE_VITA);
}

bool ui_profile_account_manual_in_use(void) {
  return source_in_use(PSN_ACCOUNT_ID_SOURCE_MANUAL);
}

const char *ui_profile_account_in_use_text(void) {
  const char *id = config_psn_account_id_in_use(&context.config, NULL);
  return is_set(id) ? id : ID_UNSET;
}

/* ============================================================================
 * Row words
 * ============================================================================ */

const char *ui_profile_account_source_text(void) {
  return context.config.psn_account_id_source == PSN_ACCOUNT_ID_SOURCE_MANUAL ? SOURCE_MANUAL_WORD
                                                                              : SOURCE_VITA_WORD;
}

/* The signed-out and not-found variants of this description are task 2b's: they depend on whether
 * the Vita's own ID could be read, which this group does not yet tell apart. */
const char *ui_profile_account_source_description(void) {
  return has_manual() ? SOURCE_DESCRIPTION : SOURCE_DESCRIPTION_NO_MANUAL;
}

const char *ui_profile_account_vita_text(void) {
  return is_set(context.config.psn_account_id) ? context.config.psn_account_id : ID_UNSET;
}

uint32_t ui_profile_account_vita_color(void) {
  return is_set(context.config.psn_account_id) ? 0 : UI_TEXT_3;
}

const char *ui_profile_account_manual_text(void) {
  return has_manual() ? context.config.psn_account_id_manual : MANUAL_UNSET;
}

uint32_t ui_profile_account_manual_color(void) {
  return has_manual() ? 0 : UI_TEXT_3;
}

const char *ui_profile_account_manual_description(void) {
  return has_manual() ? MANUAL_DESCRIPTION_SAVED : MANUAL_DESCRIPTION_NONE;
}

const char *ui_profile_account_manual_hint(void) {
  return has_manual() ? HINT_EDIT_ID : HINT_ENTER_ID;
}

/* ============================================================================
 * Keyboard
 * ============================================================================ */

/** Open the system keyboard on the typed ID (empty when none is saved). Ignored while open. */
void ui_profile_account_edit_manual(void) {
  if (s_keyboard_open)
    return;
  for (size_t i = 0; i < sizeof(KEYBOARD_TITLE); i++)
    s_keyboard_title[i] = (SceWChar16)KEYBOARD_TITLE[i];
  memset(s_keyboard_input, 0, sizeof(s_keyboard_input));
  memset(s_keyboard_initial, 0, sizeof(s_keyboard_initial));
  if (has_manual())
    ui_utf8_to_utf16(context.config.psn_account_id_manual, s_keyboard_initial, KEYBOARD_CHARS + 1);

  SceImeDialogParam param;
  sceImeDialogParamInit(&param);
  param.supportedLanguages = 0;
  param.languagesForced = SCE_FALSE;
  param.type = SCE_IME_TYPE_BASIC_LATIN;
  param.option = 0;
  param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;
  param.maxTextLength = KEYBOARD_CHARS;
  param.title = s_keyboard_title;
  param.initialText = s_keyboard_initial;
  param.inputTextBuffer = s_keyboard_input;

  const int ret = sceImeDialogInit(&param);
  if (ret < 0) {
    LOGE("Account ID: the keyboard did not open: 0x%08x", (unsigned int)ret);
    ui_toast_show(TOAST_KEYBOARD_FAILED, UI_TOAST_ERR);
    return;
  }
  s_keyboard_open = true;
}

bool ui_profile_account_busy(void) {
  return s_keyboard_open;
}

/** True when @text holds nothing but whitespace, which parse() would call a bad format. */
static bool is_blank(const char *text) {
  for (; *text; text++) {
    if (*text != ' ' && *text != '\t' && *text != '\r' && *text != '\n')
      return false;
  }
  return true;
}

/** Make @b64_id (malloc'd, ownership taken) the typed ID. */
static void replace_manual(char *b64_id) {
  free(context.config.psn_account_id_manual);
  context.config.psn_account_id_manual = b64_id;
}

/**
 * Keep the typed ID @b64_id: the first one is put in use at once, a later one keeps the choice.
 * Save the config and say so.
 */
static void save_manual(const char *b64_id) {
  char *copy = strdup(b64_id);
  if (!copy) {
    LOGE("Account ID: out of memory keeping the typed ID ending ...%s",
         psn_account_id_fingerprint(b64_id));
    ui_toast_show(TOAST_KEYBOARD_FAILED, UI_TOAST_ERR);
    return;
  }
  const bool first = !has_manual();
  replace_manual(copy);
  if (first)
    context.config.psn_account_id_source = PSN_ACCOUNT_ID_SOURCE_MANUAL;
  ui_settings_persist_config();
  LOGD("Account ID: typed ID ending ...%s saved%s", psn_account_id_fingerprint(copy),
       first ? " and in use" : "");
  ui_toast_show(first ? TOAST_SAVED_IN_USE : TOAST_SAVED, UI_TOAST_OK);
}

void ui_profile_account_poll(void) {
  if (!s_keyboard_open || sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED)
    return;
  SceImeDialogResult result;
  memset(&result, 0, sizeof(result));
  sceImeDialogGetResult(&result);
  sceImeDialogTerm();
  s_keyboard_open = false;
  if (result.button != SCE_IME_DIALOG_BUTTON_ENTER)
    return;

  char text[KEYBOARD_CHARS * 3 + 1];
  ui_utf16_to_utf8(s_keyboard_input, KEYBOARD_CHARS + 1, text, sizeof(text));
  if (is_blank(text))
    return;

  char b64_id[PSN_ACCOUNT_ID_B64_BUF_SIZE];
  switch (psn_account_id_parse(text, b64_id, sizeof(b64_id))) {
    case PSN_ACCOUNT_ID_PARSE_OK:
      save_manual(b64_id);
      break;
    case PSN_ACCOUNT_ID_PARSE_ALL_ZERO:
      LOGW("Account ID: the typed ID was all zeros and was refused");
      ui_toast_show(TOAST_ALL_ZERO, UI_TOAST_ERR);
      break;
    case PSN_ACCOUNT_ID_PARSE_BAD_FORMAT:
      LOGW("Account ID: the typed text was not an Account ID and was refused");
      ui_toast_show(TOAST_BAD_FORMAT, UI_TOAST_ERR);
      break;
  }
}

/* ============================================================================
 * Choice
 * ============================================================================ */

void ui_profile_account_change_source(int step) {
  (void)step; /* two values: one step either way leads to the other */
  const bool to_manual = context.config.psn_account_id_source != PSN_ACCOUNT_ID_SOURCE_MANUAL;
  if (to_manual && !has_manual()) {
    ui_profile_account_edit_manual();
    return;
  }
  context.config.psn_account_id_source =
      to_manual ? PSN_ACCOUNT_ID_SOURCE_MANUAL : PSN_ACCOUNT_ID_SOURCE_VITA;
  ui_settings_persist_config();
  LOGD("Account ID: now using the %s ID", to_manual ? "typed" : "Vita's");
  ui_toast_show(to_manual ? TOAST_USING_MANUAL : TOAST_USING_VITA, UI_TOAST_OK);
}
