/**
 * @file ui_profile_login.c
 * @brief The "Phone Login Assist" pane of the Profile page (see ui_profile_login.h)
 */

#include "ui/ui_profile_login.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>

#include "context.h"
#include "psn_auth.h"
#include "psn_remote.h"
#include "ui/ui_console_cards.h"
#include "ui/ui_qr_panel.h"
#include "ui/ui_settings_actions.h"
#include "ui/ui_text.h"
#include "ui/ui_text_button.h"
#include "ui/ui_theme.h"
#include "ui/ui_draw_stats.h"
#include "ui/ui_toast.h"
#include "ui/ui_utf16.h"

/* Copy (SPEC 5) */
static const char TITLE[] = "Phone Login Assist";
static const char LABEL_CODE[] = "Code";
static const char LABEL_URL[] = "URL";
static const char CODE_PLACEHOLDER[] = "Paste redirect URL/code";
/** The URL line is a deliberate short display form; the full authorize URL stays in psn_auth for
 * the QR code (SPEC 6, flag 3). */
static const char URL_DISPLAY[] = "my.account.sony.com/sso/ca/authorize";
static const char BUTTON_ENTER[] = "Enter code";
static const char BUTTON_CANCEL[] = "Cancel login";
static const char HINT_ENTER[] = "Enter code";
static const char HINT_QR[] = "QR";
static const char HINT_CANCEL[] = "Cancel login";
static const char KEYBOARD_TITLE[] = "Paste full redirect URL";
static const char TOAST_QR_SHOWN[] = "QR shown. Scan it with your phone.";
static const char TOAST_QR_HIDDEN[] = "QR hidden. Press Start to show it again.";
static const char TOAST_QR_FAILED[] = "Could not draw the QR code. Cancel and try again.";
static const char TOAST_NO_INPUT[] = "No URL/code entered";
static const char TOAST_KEYBOARD_FAILED[] = "Could not open text input";
static const char TOAST_COMPLETE[] = "PSN login complete";
static const char TOAST_FAILED[] = "PSN login failed";
static const char TOAST_CANCELED[] = "PSN login canceled";

/** Characters the keyboard returns at most, and bytes of the UTF-8 text made from them. */
#define KEYBOARD_CHARS 1024
#define KEYBOARD_TEXT_MAX 1200
/** Longest Code the login can hold (psn_auth's user_code buffer). */
#define CODE_MAX 64

/** One numbered step: words, then optionally a button glyph and the words after it. */
typedef struct login_step_t {
  const char *number;
  const char *head;  ///< "" when the step opens with the glyph
  uint32_t glyph;    ///< a UiButton action, or 0 for a step without one
  const char *tail;
} LoginStep;

static const LoginStep STEPS[] = {
    {"1", "Press", UI_BTN_FILTER, "to show or hide the QR code"},
    {"2", "Scan the QR code with your phone and sign in", 0, NULL},
    {"3", "Press", UI_BTN_CONFIRM, "and paste the redirect URL or code"},
};
#define STEP_COUNT UI_LOGIN_STEP_COUNT
_Static_assert(UI_LOGIN_STEP_COUNT == (int)(sizeof(STEPS) / sizeof(STEPS[0])),
               "UI_LOGIN_STEP_COUNT must match STEPS");

/* ============================================================================
 * State
 * ============================================================================ */

static bool s_was_active = false;
static bool s_qr_shown = true;

static bool s_keyboard_open = false;
static SceWChar16 s_keyboard_text[KEYBOARD_CHARS];
static SceWChar16 s_keyboard_title[sizeof(KEYBOARD_TITLE)];

/** Layout, measured once the fonts are up and again when the Confirm glyph changes. */
static bool s_layout_ready = false;
static bool s_layout_circle = false;
static int s_glyph_x[STEP_COUNT];
static int s_tail_x[STEP_COUNT];
static int s_value_x = 0;
static UiTextButton s_btn_enter;
static UiTextButton s_btn_cancel;

/** The Code the line shows and the part of it that fits, shortened when the code changes. */
static char s_code_raw[CODE_MAX];
static char s_code_fit[CODE_MAX];

bool ui_profile_login_showing(void) {
  return psn_auth_device_login_active();
}

bool ui_profile_login_busy(void) {
  return s_keyboard_open;
}

/* ============================================================================
 * Layout
 * ============================================================================ */

/** Place the glyph and the tail of each step, in x from the start of the steps column. */
static void layout_steps(void) {
  for (int i = 0; i < STEP_COUNT; i++) {
    const LoginStep *step = &STEPS[i];
    int x = UI_LOGIN_STEPS_X + UI_LOGIN_STEP_NUM_W;
    if (step->head[0] && step->glyph)
      x += ui_text_face_width(UI_FACE_T16, step->head) + UI_LOGIN_GLYPH_GAP;
    s_glyph_x[i] = x;
    s_tail_x[i] = x + ui_hint_row_glyph_width(step->glyph) + UI_LOGIN_GLYPH_GAP;
  }
}

/** Measure what depends on the fonts and the Confirm glyph, when that has not been done yet. */
static void ensure_layout(void) {
  if (s_layout_ready && s_layout_circle == context.config.circle_btn_confirm)
    return;
  layout_steps();
  const int code_w = ui_text_face_width(UI_FACE_T16, LABEL_CODE);
  const int url_w = ui_text_face_width(UI_FACE_T16, LABEL_URL);
  s_value_x = UI_LOGIN_STEPS_X + (code_w > url_w ? code_w : url_w) + UI_LOGIN_INFO_GAP;
  if (!s_layout_ready) {
    int x = UI_PAGE_PANE_X;
    UiTextButton *buttons[] = {&s_btn_enter, &s_btn_cancel};
    const char *labels[] = {BUTTON_ENTER, BUTTON_CANCEL};
    for (int i = 0; i < 2; i++) {
      ui_text_button_init(buttons[i], labels[i], x, UI_LOGIN_BUTTONS_Y);
      x += buttons[i]->visible.w + UI_LOGIN_BUTTON_GAP;
    }
  }
  s_layout_ready = true;
  s_layout_circle = context.config.circle_btn_confirm;
}

/**
 * fit_tail() - Copy the end of @text that fits @max_w into @out, dropping characters from the
 * front. A code too long for its line shows its tail: the part the user types last.
 */
static void fit_tail(const char *text, int max_w, char *out, size_t cap) {
  snprintf(out, cap, "%s", text);
  const char *start = out;
  while (*start && ui_text_face_width(UI_FACE_T16, start) > max_w) {
    start++;
    while (((unsigned char)*start & 0xC0) == 0x80)
      start++;
  }
  memmove(out, start, strlen(start) + 1);
}

/** Refit the Code line when the code (or its placeholder) is not the one fitted last. */
static void update_code_line(void) {
  const char *code = psn_auth_device_user_code();
  if (!code[0])
    code = CODE_PLACEHOLDER;
  if (strncmp(s_code_raw, code, sizeof(s_code_raw) - 1) == 0)
    return;
  snprintf(s_code_raw, sizeof(s_code_raw), "%s", code);
  fit_tail(s_code_raw, UI_LOGIN_COLUMN_RIGHT - s_value_x, s_code_fit, sizeof(s_code_fit));
}

/* ============================================================================
 * Actions
 * ============================================================================ */

/** Open the system keyboard for the redirect URL or code. Ignored while it is already open. */
static void open_keyboard(void) {
  if (s_keyboard_open)
    return;
  memset(s_keyboard_text, 0, sizeof(s_keyboard_text));
  for (size_t i = 0; i < sizeof(KEYBOARD_TITLE); i++)
    s_keyboard_title[i] = (SceWChar16)KEYBOARD_TITLE[i];

  SceImeDialogParam param;
  sceImeDialogParamInit(&param);
  param.supportedLanguages = 0;
  param.languagesForced = SCE_FALSE;
  param.type = SCE_IME_TYPE_URL;
  param.option = 0;
  param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;
  param.maxTextLength = KEYBOARD_CHARS - 1;
  param.title = s_keyboard_title;
  param.initialText = NULL;
  param.inputTextBuffer = s_keyboard_text;

  const int ret = sceImeDialogInit(&param);
  if (ret < 0) {
    LOGE("PSN login: the keyboard did not open: 0x%08x", (unsigned int)ret);
    ui_toast_show(TOAST_KEYBOARD_FAILED, UI_TOAST_ERR);
    return;
  }
  s_keyboard_open = true;
}

/** Hand the pasted redirect URL or code to psn_auth and say how it went. */
static void submit_text(const char *text) {
  if (!text[0]) {
    ui_toast_show(TOAST_NO_INPUT, UI_TOAST_PLAIN);
    return;
  }
  if (psn_auth_submit_authorization_response(text, (uint64_t)time(NULL))) {
    ui_settings_persist_config();
    ui_toast_show(TOAST_COMPLETE, UI_TOAST_OK);
    const uint64_t work_start_us = UI_WORK_START();
    const int refresh_result = psn_remote_refresh_hosts();
    UI_WORK_NOTE("psn_hosts", work_start_us);
    if (refresh_result == 0)
      ui_cards_update_cache(true);
    return;
  }
  const char *error = psn_auth_last_error();
  ui_toast_show(error && error[0] ? error : TOAST_FAILED, UI_TOAST_ERR);
}

void ui_profile_login_poll(void) {
  if (!s_keyboard_open || sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED)
    return;
  SceImeDialogResult result;
  memset(&result, 0, sizeof(result));
  sceImeDialogGetResult(&result);
  sceImeDialogTerm();
  s_keyboard_open = false;
  if (result.button != SCE_IME_DIALOG_BUTTON_ENTER)
    return;
  char text[KEYBOARD_TEXT_MAX];
  ui_utf16_to_utf8(s_keyboard_text, KEYBOARD_CHARS, text, sizeof(text));
  submit_text(text);
}

/** Show or hide the QR code and say so; when it cannot be drawn, say that instead. */
static void toggle_qr(void) {
  if (!ui_qr_panel_ready()) {
    ui_toast_show(TOAST_QR_FAILED, UI_TOAST_ERR);
    return;
  }
  s_qr_shown = !s_qr_shown;
  ui_toast_show(s_qr_shown ? TOAST_QR_SHOWN : TOAST_QR_HIDDEN, UI_TOAST_PLAIN);
}

static void cancel_login(void) {
  psn_auth_cancel_device_login();
  ui_toast_show(TOAST_CANCELED, UI_TOAST_PLAIN);
}

/* ============================================================================
 * Frame
 * ============================================================================ */

void ui_profile_login_update(void) {
  const bool active = psn_auth_device_login_active();
  if (active && !s_was_active)
    s_qr_shown = true;
  s_was_active = active;

  const char *url = active ? psn_auth_device_verification_url() : NULL;
  if (ui_qr_panel_set_url(url) && url && url[0] && !ui_qr_panel_ready())
    ui_toast_show(TOAST_QR_FAILED, UI_TOAST_ERR);
}

void ui_profile_login_input(const UiInput *in) {
  ensure_layout();
  const bool enter_tapped = ui_text_button_input(&s_btn_enter, in) == UI_EVENT_ACTIVATED;
  const bool cancel_tapped = ui_text_button_input(&s_btn_cancel, in) == UI_EVENT_ACTIVATED;
  const UiRect qr_rect = {UI_PAGE_PANE_X, UI_LOGIN_QR_Y, UI_QR_BOX, UI_QR_BOX};
  const bool qr_tapped = ui_touch_tap(in) && ui_rect_contains(qr_rect, in->touch.x, in->touch.y);

  if (enter_tapped || (in->pressed & UI_BTN_CONFIRM))
    open_keyboard();
  else if (qr_tapped || (in->pressed & UI_BTN_FILTER))
    toggle_qr();
  else if (cancel_tapped || (in->pressed & UI_BTN_CLEAR))
    cancel_login();
}

int ui_profile_login_hints(UiHintItem out[UI_HINT_MAX_ITEMS]) {
  out[0] = (UiHintItem){.action = UI_BTN_CONFIRM, .label = HINT_ENTER};
  out[1] = (UiHintItem){.action = UI_BTN_FILTER, .label = HINT_QR};
  out[2] = (UiHintItem){.action = UI_BTN_CLEAR, .label = HINT_CANCEL};
  return 3;
}

/** Draw step @index: its number, its words and its glyph, on its own line of the steps column. */
static void draw_step(int index) {
  const LoginStep *step = &STEPS[index];
  const int y = UI_LOGIN_QR_Y + index * UI_LOGIN_STEP_PITCH;
  ui_text_draw_face_centered_v(UI_FACE_T16, UI_LOGIN_STEPS_X, y, UI_T16_LINE, UI_TEXT_2,
                               step->number);
  if (step->head[0]) {
    ui_text_draw_face_centered_v(UI_FACE_T16, UI_LOGIN_STEPS_X + UI_LOGIN_STEP_NUM_W, y,
                                 UI_T16_LINE, UI_TEXT_2, step->head);
  }
  if (!step->glyph)
    return;
  ui_hint_row_glyph_draw(step->glyph, s_glyph_x[index], y, UI_T16_LINE, UI_TEXT);
  ui_text_draw_face_centered_v(UI_FACE_T16, s_tail_x[index], y, UI_T16_LINE, UI_TEXT_2, step->tail);
}

/** Draw one "Code" or "URL" line of the text column: label and value, the line top at @y. */
static void draw_info_line(int y, const char *label, uint32_t value_color, const char *value) {
  ui_text_draw_face_centered_v(UI_FACE_T16, UI_LOGIN_STEPS_X, y, UI_T16_LINE, UI_TEXT_3, label);
  ui_text_draw_face_centered_v(UI_FACE_T16, s_value_x, y, UI_T16_LINE, value_color, value);
}

void ui_profile_login_draw(void) {
  ensure_layout();
  update_code_line();
  ui_text_draw_face_centered_v(UI_FACE_T20, UI_PAGE_PANE_X, UI_BODY_Y, UI_LOGIN_TITLE_H, UI_TEXT,
                               TITLE);
  ui_qr_panel_draw(UI_PAGE_PANE_X, UI_LOGIN_QR_Y, s_qr_shown);
  for (int i = 0; i < STEP_COUNT; i++)
    draw_step(i);
  draw_info_line(UI_LOGIN_INFO_Y, LABEL_CODE, UI_TEXT, s_code_fit);
  draw_info_line(UI_LOGIN_INFO_Y + UI_T16_LINE, LABEL_URL, UI_TEXT_2, URL_DISPLAY);
  ui_text_button_draw(&s_btn_enter);
  ui_text_button_draw(&s_btn_cancel);
}
