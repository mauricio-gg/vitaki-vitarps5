/**
 * @file ui_filter_keyboard.c
 * @brief The system keyboard for a console filter (see ui_filter_keyboard.h)
 */

#include "ui/ui_filter_keyboard.h"

#include <string.h>

#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>

#include "context.h"
#include "ui/ui_console_cards.h"
#include "ui/ui_internal.h"
#include "ui/ui_utf16.h"

/** Longest filter text, in characters. */
#define FILTER_MAX_LEN (UI_FILTER_TEXT_MAX - 1)
/** Keyboard title capacity, in 16-bit units, terminator included. */
#define TITLE_UNITS 64

static bool s_running = false;
static SceWChar16 s_input[FILTER_MAX_LEN + 1];
static SceWChar16 s_initial[FILTER_MAX_LEN + 1];
static SceWChar16 s_title[TITLE_UNITS];

bool ui_filter_keyboard_open(const char *title, const char *initial) {
  if (s_running)
    return false;

  memset(s_input, 0, sizeof(s_input));
  ui_utf8_to_utf16(initial, s_initial, sizeof(s_initial) / sizeof(s_initial[0]));
  ui_utf8_to_utf16(title, s_title, sizeof(s_title) / sizeof(s_title[0]));

  SceImeDialogParam param;
  sceImeDialogParamInit(&param);
  param.supportedLanguages = 0; /* All languages */
  param.languagesForced = SCE_FALSE;
  param.type = SCE_IME_TYPE_DEFAULT;
  param.option = 0;
  param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
  param.maxTextLength = FILTER_MAX_LEN;
  param.title = s_title;
  param.initialText = s_initial;
  param.inputTextBuffer = s_input;

  const int ret = sceImeDialogInit(&param);
  if (ret < 0) {
    LOGE("Filter keyboard failed to open: 0x%08x", (unsigned int)ret);
    return false;
  }
  s_running = true;
  return true;
}

bool ui_filter_keyboard_running(void) {
  return s_running;
}

UiFilterKeyboardResult ui_filter_keyboard_poll(char *text, size_t size) {
  if (!s_running)
    return UI_FILTER_KB_IDLE;
  if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED)
    return UI_FILTER_KB_OPEN;

  SceImeDialogResult result;
  memset(&result, 0, sizeof(result));
  sceImeDialogGetResult(&result);
  sceImeDialogTerm();
  s_running = false;

  if (result.button != SCE_IME_DIALOG_BUTTON_ENTER)
    return UI_FILTER_KB_CANCEL;
  ui_utf16_to_utf8(s_input, FILTER_MAX_LEN + 1, text, size);
  return UI_FILTER_KB_DONE;
}
