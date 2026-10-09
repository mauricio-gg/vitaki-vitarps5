/**
 * @file ui_profile.c
 * @brief The XMB Profile page (SPEC.md section 3.7)
 *
 * Rows are data: ProfileRowDef says what a row is called, what kind it is, its description, how to
 * read its value and what it does. A group is a table of rows. The PlayStation Network group lists
 * every row it can ever show and each row says when it is visible, so the PSN state alone decides
 * which ones are on the page.
 */

#include "ui/ui_profile.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <vita2d.h>

#include "context.h"
#include "logging.h"
#include "psn_auth.h"
#include "psn_remote.h"
#include "ui.h"
#include "ui/ui_animation.h"
#include "ui/ui_bake.h"
#include "ui/ui_chrome_layout.h"
#include "ui/ui_console_cards.h"
#include "ui/ui_group_list.h"
#include "ui/ui_hint_row.h"
#include "ui/ui_home.h"
#include "ui/ui_home_detail.h"
#include "ui/ui_input.h"
#include "ui/ui_page_frame.h"
#include "ui/ui_profile_login.h"
#include "ui/ui_scroll_indicator.h"
#include "ui/ui_setting_list.h"
#include "ui/ui_settings_actions.h"
#include "ui/ui_text.h"
#include "ui/ui_text_wrap.h"
#include "ui/ui_theme.h"
#include "ui/ui_toast.h"
#include "ui/ui_top_bar.h"
#include "ui/ui_value_labels.h"

/* ============================================================================
 * Row definitions
 * ============================================================================ */

/** The words of an action that needs a second press: its label while armed, with the Confirm glyph
 * between @head and @tail, and the Confirm hint's verb. */
typedef struct profile_arm_t {
  const char *head;
  const char *tail;
  const char *hint;
} ProfileArm;

/**
 * One row. An info row fills @value_text; an action runs @run and names itself in the hint row
 * with @hint.
 */
typedef struct profile_row_def_t {
  const char *label;
  UiSettingKind kind;
  const char *description;
  const char *hint;  ///< action: the Confirm hint's verb
  bool small_value;  ///< info: the value is drawn in T16
  const char *(*value_text)(void);
  uint32_t (*value_color)(void);  ///< info: NULL or 0 for the row's own; UI_ERR styles an error
  const char *disabled_toast;     ///< action: set when it cannot run; pressing it toasts this
  const ProfileArm *arm;          ///< action: NULL, or runs only on a second press in the window
  bool (*visible)(void);          ///< NULL: always shown
  void (*run)(void);
} ProfileRowDef;

typedef struct profile_group_t {
  const ProfileRowDef *rows;
  int count;
} ProfileGroup;

/* Copy */
static const char TITLE[] = "Profile";
static const char PSN_NAME[] = "PlayStation Network";
static const char ACCOUNT_ID_UNSET[] = "Not Set";
static const char TOAST_REFRESHED[] = "Account ID refreshed from system profile";
static const char TOAST_REFRESH_FAILED[] = "Could not refresh Account ID";
static const char HINT_OPEN[] = "Open";
static const char HINT_REFRESH[] = "Refresh";
static const char HINT_GROUP[] = "Group";
static const char HINT_BACK[] = "Back";
static const char PSN_LOGIN_FAILED[] = "PSN login could not start";
static const char PSN_LOGIN_STARTED[] =
    "Scan QR on phone, then press %s to paste the full redirect URL";
static const char PSN_HOSTS_REFRESHED[] = "PSN internet host list refreshed";
static const char PSN_HOSTS_FAILED[] = "PSN internet host refresh failed";
static const char PSN_LOGGED_OUT[] = "PSN login removed";
static const char PSN_DISABLED_HELP[] = "Enable PSN internet mode in Settings";
static const char PSN_DISABLED_TOAST[] = "PSN internet mode is disabled in Settings";

/** How long the first press of Log out stays armed (SPEC 1.3). */
#define PSN_ARM_WINDOW_MS 3000.0f

/* Account */

/** The PSN Account ID, or "Not Set" when there is none. */
static const char *account_id_text(void) {
  const char *id = context.config.psn_account_id;
  return id && id[0] ? id : ACCOUNT_ID_UNSET;
}

/** Read the Account ID again from the system profile, save it and say how it went. */
static void refresh_account_id(void) {
  if (ui_reload_psn_account_id()) {
    ui_settings_persist_config();
    ui_toast_show(TOAST_REFRESHED, UI_TOAST_OK);
  } else {
    ui_toast_show(TOAST_REFRESH_FAILED, UI_TOAST_ERR);
  }
}

/* Connection (SPEC 3.7). The words come from ui_connection_words(), the one rule Home's info panel
 * also uses. */

static const char *network_type_text(void) {
  return ui_connection_words(ui_profile_reference_host()).network_type;
}

static const char *console_name_text(void) {
  return ui_connection_console_name(ui_profile_reference_host());
}

static const char *console_ip_text(void) {
  return ui_connection_console_ip(ui_profile_reference_host());
}

/** The Console IP row shows only when an address is known. */
static bool console_ip_known(void) {
  return console_ip_text() != NULL;
}

static const char *status_text(void) {
  return ui_connection_words(ui_profile_reference_host()).status;
}

static const char *quality_text(void) {
  return ui_label_resolution(context.config.resolution);
}

static const ProfileRowDef CONNECTION_ROWS[] = {
    {.label = "Network Type", .kind = UI_SETTING_INFO, .value_text = network_type_text},
    {.label = "Console", .kind = UI_SETTING_INFO, .value_text = console_name_text},
    {.label = "Console IP",
     .kind = UI_SETTING_INFO,
     .value_text = console_ip_text,
     .visible = console_ip_known},
    {.label = "Status", .kind = UI_SETTING_INFO, .value_text = status_text},
    {.label = "Quality", .kind = UI_SETTING_INFO, .value_text = quality_text},
};

static const ProfileRowDef ACCOUNT_ROWS[] = {
    {.label = "Account ID",
     .kind = UI_SETTING_INFO,
     .small_value = true,
     .value_text = account_id_text},
    {.label = "Refresh Account ID",
     .kind = UI_SETTING_ACTION,
     .description = "Read the Account ID again from the system profile.",
     .hint = HINT_REFRESH,
     .run = refresh_account_id},
};

/* PlayStation Network (SPEC 3.7). The state is read once per frame by fill_items(), so every
 * callback below sees the same one. */

static PsnAuthState s_psn_state;
static uint64_t s_psn_now;

static const char *psn_status_text(void) {
  return psn_auth_state_label_for(s_psn_state, s_psn_now);
}

static uint32_t psn_status_color(void) {
  return ui_psn_auth_color(s_psn_state);
}

static bool psn_is_disabled(void) {
  return s_psn_state == PSN_AUTH_STATE_DISABLED;
}

static bool psn_is_enabled(void) {
  return s_psn_state != PSN_AUTH_STATE_DISABLED;
}

static bool psn_is_authenticated(void) {
  return s_psn_state == PSN_AUTH_STATE_TOKEN_VALID;
}

/** Log in is offered when the Vita is signed out or the last attempt failed. */
static bool psn_offers_login(void) {
  return s_psn_state == PSN_AUTH_STATE_LOGGED_OUT || s_psn_state == PSN_AUTH_STATE_ERROR;
}

/** The Confirm button's name for a toast, which cannot draw the glyph. */
static const char *confirm_button_name(void) {
  return context.config.circle_btn_confirm ? "Circle" : "Cross";
}

/** Sign in: a saved token that refreshes needs nothing more; otherwise start the phone login. */
static void psn_log_in(void) {
  const uint64_t now = (uint64_t)time(NULL);
  if (psn_auth_refresh_token_if_needed(now, false))
    return;
  if (psn_auth_begin_device_login(now)) {
    char text[UI_TOAST_TEXT_MAX];
    snprintf(text, sizeof(text), PSN_LOGIN_STARTED, confirm_button_name());
    ui_toast_show(text, UI_TOAST_PLAIN);
    return;
  }
  const char *error = psn_auth_last_error();
  ui_toast_show(error && error[0] ? error : PSN_LOGIN_FAILED, UI_TOAST_ERR);
}

static void psn_refresh_hosts(void) {
  if (psn_remote_refresh_hosts() == 0) {
    ui_cards_update_cache(true);
    ui_toast_show(PSN_HOSTS_REFRESHED, UI_TOAST_OK);
  } else {
    ui_toast_show(PSN_HOSTS_FAILED, UI_TOAST_ERR);
  }
}

/** Remove the saved PSN login: tokens, cached internet consoles, then save and refresh the cards.
 */
static void psn_log_out(void) {
  psn_auth_clear_tokens();
  psn_remote_clear_cached_hosts();
  ui_settings_persist_config();
  ui_cards_update_cache(true);
  ui_toast_show(PSN_LOGGED_OUT, UI_TOAST_OK);
}

static const ProfileArm LOGOUT_ARM = {
    .head = "Press", .tail = "again to confirm log out", .hint = "Confirm log out"};

static const ProfileRowDef PSN_ROWS[] = {
    {.label = "PSN Auth",
     .kind = UI_SETTING_INFO,
     .description = PSN_DISABLED_HELP,
     .value_text = psn_status_text,
     .visible = psn_is_disabled},
    {.label = "PSN Auth",
     .kind = UI_SETTING_INFO,
     .value_text = psn_status_text,
     .value_color = psn_status_color,
     .visible = psn_is_enabled},
    {.label = "Log in",
     .kind = UI_SETTING_ACTION,
     .description = PSN_DISABLED_HELP,
     .hint = "Log in",
     .disabled_toast = PSN_DISABLED_TOAST,
     .visible = psn_is_disabled},
    {.label = "Log in",
     .kind = UI_SETTING_ACTION,
     .description = "Sign in with your phone. Needed for internet Remote Play.",
     .hint = "Log in",
     .visible = psn_offers_login,
     .run = psn_log_in},
    {.label = "Refresh hosts",
     .kind = UI_SETTING_ACTION,
     .description = "Reload your internet-capable consoles.",
     .hint = HINT_REFRESH,
     .visible = psn_is_authenticated,
     .run = psn_refresh_hosts},
    {.label = "Log out",
     .kind = UI_SETTING_ACTION,
     .description = "Remove the saved PSN login from this Vita.",
     .hint = "Log out",
     .arm = &LOGOUT_ARM,
     .visible = psn_is_authenticated,
     .run = psn_log_out},
};

/** Groups, in the order of Home's Profile list. */
static const ProfileGroup GROUPS[UI_PROFILE_GROUP_COUNT] = {
    [UI_PROFILE_GROUP_ACCOUNT] = {ACCOUNT_ROWS,
                                  (int)(sizeof(ACCOUNT_ROWS) / sizeof(ACCOUNT_ROWS[0]))},
    [UI_PROFILE_GROUP_CONNECTION] = {CONNECTION_ROWS,
                                     (int)(sizeof(CONNECTION_ROWS) / sizeof(CONNECTION_ROWS[0]))},
    [UI_PROFILE_GROUP_PSN] = {PSN_ROWS, (int)(sizeof(PSN_ROWS) / sizeof(PSN_ROWS[0]))},
};

static const char *const GROUP_NAMES[UI_PROFILE_GROUP_COUNT] = {"Account", "Connection", PSN_NAME};

_Static_assert(UI_PROFILE_GROUP_COUNT <= UI_GROUP_MAX, "Profile groups must fit the group list");

/* ============================================================================
 * State
 * ============================================================================ */

static UiGroupList s_groups;
static UiSettingList s_pane;
static UiSettingItem s_items[UI_SETTING_MAX_ROWS];
/** The definition behind each shown row; hidden rows are skipped, so this is not GROUPS[].rows. */
static const ProfileRowDef *s_defs[UI_SETTING_MAX_ROWS];
static int s_group = 0;
/** The pane has focus; otherwise the group list has. */
static bool s_pane_focus = false;
/** Description of the focused row, wrapped once when the row shown there changes. */
static UiWrapped s_desc;
static const ProfileRowDef *s_desc_def = NULL;
static bool s_desc_stale = true;
/** The action armed by its first press, and when; NULL when none is. */
static const ProfileRowDef *s_armed_def = NULL;
static uint64_t s_armed_start_us = 0;
/** Hint layout of the last drawn frame; taps are resolved against it. */
static UiHintLayout s_hints;

/** The avatar's ring, baked once and tinted at draw time. */
static vita2d_texture *s_ring = NULL;
/** The Account ID the identity block shows, as last seen, and shortened to its column. */
static char s_ident_raw[UI_IDENT_ID_MAX];
static char s_ident_fit[UI_IDENT_ID_MAX];

/** Room for the identity block's text column: what the group column leaves after the avatar. */
#define IDENT_TEXT_W (UI_PAGE_GROUP_W - 2 * UI_IDENT_PAD - UI_IDENT_AVATAR - UI_IDENT_GAP)

/* ============================================================================
 * Identity block
 * ============================================================================ */

static float clamp01(float v) {
  return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/** Alpha of the avatar ring: a circle outline UI_IDENT_RING thick filling the texture. */
static float ring_alpha(float px, float py, const void *ctx) {
  (void)ctx;
  const float r = (float)UI_IDENT_AVATAR / 2.0f;
  const float d = sqrtf((px - r) * (px - r) + (py - r) * (py - r));
  return clamp01(r + 0.5f - d) - clamp01(r - (float)UI_IDENT_RING + 0.5f - d);
}

/** Width function for ui_ellipsize_to_fit(): the identity block's face. */
static int measure_ident(const char *text, void *ctx) {
  (void)ctx;
  return ui_text_face_width(UI_FACE_T16, text);
}

/** Re-measure the shortened Account ID when it changed since the last frame. */
static void update_identity(void) {
  const char *text = account_id_text();
  if (strncmp(s_ident_raw, text, sizeof(s_ident_raw) - 1) == 0)
    return;
  snprintf(s_ident_raw, sizeof(s_ident_raw), "%s", text);
  ui_ellipsize_to_fit(s_ident_raw, IDENT_TEXT_W, measure_ident, NULL, s_ident_fit,
                      sizeof(s_ident_fit));
}

/** Draw the identity block: avatar ring, the person icon, the Account ID and "PlayStation Network".
 * Paper cost 4. */
static void draw_identity(void) {
  const int x = UI_PAGE_GROUP_X + UI_IDENT_PAD;
  const int avatar_y = UI_IDENT_Y + (UI_IDENT_H - UI_IDENT_AVATAR) / 2;
  if (s_ring)
    vita2d_draw_texture_tint(s_ring, (float)x, (float)avatar_y, UI_LINE);

  vita2d_texture *icon = ui_page_frame_icon(UI_PAGE_ICON_PROFILE);
  if (icon) {
    const float scale = (float)UI_IDENT_ICON / (float)vita2d_texture_get_height(icon);
    const int offset = (UI_IDENT_AVATAR - UI_IDENT_ICON) / 2;
    vita2d_draw_texture_tint_scale(
        icon, (float)(x + offset), (float)(avatar_y + offset), scale, scale,
        ui_color_scale_alpha(UI_TEXT, (float)UI_IDENT_ICON_PCT / 100.0f));
  }

  const int text_x = x + UI_IDENT_AVATAR + UI_IDENT_GAP;
  ui_text_draw_face_centered_v(UI_FACE_T16, text_x, UI_IDENT_Y, UI_T16_LINE, UI_TEXT, s_ident_fit);
  ui_text_draw_face_centered_v(UI_FACE_T16, text_x, UI_IDENT_Y + UI_T16_LINE, UI_T16_LINE,
                               UI_TEXT_3, PSN_NAME);
}

/* ============================================================================
 * Rows and description
 * ============================================================================ */

/** True while @def's first press is still inside its window. */
static bool row_is_armed(const ProfileRowDef *def) {
  return def && def == s_armed_def && ui_anim_elapsed_ms(s_armed_start_us) < PSN_ARM_WINDOW_MS;
}

static void disarm(void) {
  s_armed_def = NULL;
}

/** Drop the arm when its window passed or its row is no longer the focused one. */
static void update_arm(void) {
  const bool on_row = s_pane_focus && s_pane.count > 0 && s_defs[s_pane.focus] == s_armed_def;
  if (!on_row || !row_is_armed(s_armed_def))
    disarm();
}

/**
 * fill_items() - Rewrite the items' values from the config and the console, skipping rows that
 * are hidden right now.
 * @return how many rows are shown
 */
static int fill_items(void) {
  const ProfileGroup *group = &GROUPS[s_group];
  s_psn_now = (uint64_t)time(NULL);
  s_psn_state = psn_auth_state(s_psn_now);
  int shown = 0;
  for (int i = 0; i < group->count && shown < UI_SETTING_MAX_ROWS; i++) {
    const ProfileRowDef *def = &group->rows[i];
    if (def->visible && !def->visible())
      continue;
    s_defs[shown] = def;
    s_items[shown] = (UiSettingItem){
        .label = def->label,
        .kind = def->kind,
        .value_text = def->value_text ? def->value_text() : NULL,
        .small_value = def->small_value,
        .disabled = def->disabled_toast != NULL,
        .color = def->value_color ? def->value_color() : 0,
    };
    s_items[shown].error = s_items[shown].color == UI_ERR;
    if (row_is_armed(def)) {
      s_items[shown].label = def->arm->head;
      s_items[shown].label_glyph = UI_BTN_CONFIRM;
      s_items[shown].label_tail = def->arm->tail;
      s_items[shown].color = UI_WARN;
    }
    shown++;
  }
  return shown;
}

/** Show group @index: its rows, with the pane focus on the first row. */
static void load_group(int index) {
  s_group = index;
  ui_group_list_set_current(&s_groups, index);
  disarm();
  ui_setting_list_load(&s_pane, s_items, fill_items());
  s_desc_stale = true;
}

/**
 * Refill the items; when a row appeared or went away (the console's address became known or
 * unknown), reload the list and keep the focus on the same row if it is still there.
 */
static void refresh_items(void) {
  const ProfileRowDef *focused = s_pane.count > 0 ? s_defs[s_pane.focus] : NULL;
  const int count = fill_items();
  if (count != s_pane.count) {
    ui_setting_list_load(&s_pane, s_items, count);
    for (int i = 0; i < count; i++) {
      if (s_defs[i] == focused)
        s_pane.focus = i;
    }
  }
  ui_setting_list_sync(&s_pane);
}

/** Width function for ui_text_wrap(): the description's face. */
static int measure_description(const char *text, void *ctx) {
  (void)ctx;
  return ui_text_face_width(UI_FACE_T16, text);
}

/** Wrap the description of the row now in the focus when it is not the one wrapped last. */
static void update_description(void) {
  const ProfileRowDef *def = s_pane.count > 0 ? s_defs[s_pane.focus] : NULL;
  if (!s_desc_stale && def == s_desc_def)
    return;
  s_desc_stale = false;
  s_desc_def = def;
  const char *text = def ? def->description : NULL;
  ui_text_wrap(text ? text : "", UI_PAGE_PANE_W, measure_description, NULL, &s_desc);
}

/* ============================================================================
 * Setup
 * ============================================================================ */

void ui_profile_init(void) {
  ui_group_list_init(&s_groups, GROUP_NAMES, UI_PROFILE_GROUP_COUNT);
  ui_setting_list_init(&s_pane);
  s_ring = ui_bake_white(UI_IDENT_AVATAR, UI_IDENT_AVATAR, ring_alpha, NULL);
  s_hints.count = 0;
  ui_profile_open(UI_PROFILE_GROUP_ACCOUNT);
}

void ui_profile_open(int group) {
  if (group < 0 || group >= UI_PROFILE_GROUP_COUNT)
    group = UI_PROFILE_GROUP_ACCOUNT;
  load_group(group);
  s_pane_focus = s_pane.count > 0;
  s_hints.count = 0;
}

/* ============================================================================
 * Input
 * ============================================================================ */

/**
 * run_focused_row() - Act on the focused action row. A disabled one says why it cannot run; one
 * that needs a second press arms on the first and runs on the next inside its window.
 */
static void run_focused_row(void) {
  const ProfileRowDef *def = s_defs[s_pane.focus];
  if (def->disabled_toast) {
    ui_toast_show(def->disabled_toast, UI_TOAST_ERR);
    return;
  }
  if (def->arm && !row_is_armed(def)) {
    s_armed_def = def;
    s_armed_start_us = ui_anim_now_us();
    return;
  }
  disarm();
  if (def->run)
    def->run();
}

/**
 * handle_pane_event() - Act on what the pane reported. Any event while the group list has focus
 * came from touch and moves the focus to the pane.
 */
static void handle_pane_event(UiEvent event) {
  switch (event) {
    case UI_EVENT_ACTIVATED:
      run_focused_row();
      s_pane_focus = true;
      break;
    case UI_EVENT_MOVED:
      s_pane_focus = true;
      break;
    case UI_EVENT_CANCELLED:
      s_pane_focus = false;
      break;
    default:
      break;
  }
}

/**
 * handle_group_event() - Act on what the group list reported.
 * @tapped: The event came from a tap, which also gives the group list the focus.
 * @return true when the page should go back to Home
 */
static bool handle_group_event(UiEvent event, bool tapped) {
  switch (event) {
    case UI_EVENT_MOVED:
      if (s_groups.current != s_group)
        load_group(s_groups.current);
      if (tapped || s_pane.count == 0)
        s_pane_focus = false;
      break;
    case UI_EVENT_ACTIVATED:
      s_pane_focus = s_pane.count > 0;
      break;
    case UI_EVENT_CANCELLED:
      return true;
    default:
      break;
  }
  return false;
}

/* ============================================================================
 * Draw
 * ============================================================================ */

/** True while the phone login replaces the PlayStation Network rows. */
static bool login_pane_shows(void) {
  return s_group == UI_PROFILE_GROUP_PSN && ui_profile_login_showing();
}

/** Fill @out with the hints for what is focused (SPEC 3.7) and return how many. */
static int build_hints(UiHintItem out[UI_HINT_MAX_ITEMS]) {
  if (login_pane_shows())
    return ui_profile_login_hints(out);
  int n = 0;
  if (!s_pane_focus) {
    out[n++] = (UiHintItem){.action = UI_BTN_CONFIRM, .label = HINT_OPEN, .dim = s_pane.count == 0};
  } else {
    const ProfileRowDef *def = s_defs[s_pane.focus];
    if (def->kind == UI_SETTING_ACTION) {
      out[n++] = (UiHintItem){.action = UI_BTN_CONFIRM,
                              .label = row_is_armed(def) ? def->arm->hint : def->hint,
                              .dim = def->disabled_toast != NULL};
    }
    out[n++] = (UiHintItem){.action = UI_BTN_L | UI_BTN_R, .label = HINT_GROUP};
  }
  out[n++] = (UiHintItem){.action = UI_BTN_CANCEL, .label = HINT_BACK};
  return n;
}

/** Draw the focused row's description, at most UI_PAGE_DESC_LINES lines. */
static void draw_description(void) {
  for (int i = 0; i < s_desc.count && i < UI_PAGE_DESC_LINES; i++) {
    ui_text_draw_face_centered_v(UI_FACE_T16, UI_PAGE_PANE_X, UI_PAGE_DESC_Y + i * UI_T16_LINE,
                                 UI_T16_LINE, UI_TEXT_2, s_desc.lines[i]);
  }
}

/* ============================================================================
 * Frame
 * ============================================================================ */

UIScreenType ui_profile_frame(void) {
  /* While the system keyboard is open, and in the frame it closes, the page does not act. */
  const bool keyboard_was_open = ui_profile_login_busy();
  ui_profile_login_poll();
  ui_profile_login_update();

  /* A tapped hint acts as that button pressed in one frame; the D-pad hint has no action. */
  /* Plain if/else rather than a ternary: cppcheck cannot parse a compound literal in one. */
  UiInput in = {0};
  if (!keyboard_was_open) {
    in = *ui_input_snapshot();
  }
  const uint32_t tapped = ui_hint_row_tap(&s_hints, &in);
  if (tapped) {
    in.pressed |= tapped & ~(uint32_t)UI_BTN_DPAD;
    in.touch.pressed = false;
    in.touch.released = false;
    in.touch.down = false;
  }

  /* The back chevron is a small control and wins over everything under it, the login pane
   * included, so a tap on it reaches nothing else. */
  const bool back_tapped = ui_page_frame_back_tapped(&in);
  if (back_tapped) {
    in.touch.pressed = false;
    in.touch.released = false;
    in.touch.down = false;
  }

  /* The login pane takes the face buttons and touch whatever had focus (its buttons are touch
   * targets and take no controller focus), so the focus stays with the pane while it shows. */
  const bool login_in = login_pane_shows();
  if (login_in) {
    s_pane_focus = true;
    ui_profile_login_input(&in);
  }

  /* Keys go to the column that has focus, so one press never acts twice; L and R always reach
   * the group list, and touch reaches both. */
  UiInput pane_in = in;
  UiInput group_in = in;
  if (s_pane_focus) {
    group_in.pressed &= UI_BTN_L | UI_BTN_R;
    group_in.repeat = 0;
  } else {
    pane_in.pressed = 0;
    pane_in.repeat = 0;
  }

  if (!login_in)
    handle_pane_event(ui_setting_list_input(&s_pane, &pane_in));
  bool back = handle_group_event(ui_group_list_input(&s_groups, &group_in), ui_touch_tap(&in));
  /* Cancel goes back to Home; the login keeps running and its pane shows again on return. */
  if (back_tapped || (login_in && (in.pressed & UI_BTN_CANCEL)))
    back = true;
  const bool login = login_pane_shows();
  if (login)
    s_pane_focus = true;

  refresh_items();
  update_arm();
  update_description();
  update_identity();
  s_groups.focused = !s_pane_focus;
  s_pane.active = s_pane_focus;

  ui_page_frame_draw(UI_PAGE_ICON_PROFILE, TITLE);
  ui_page_frame_back_draw();
  ui_top_bar_draw(NULL);
  ui_group_list_draw(&s_groups);
  draw_identity();
  if (login) {
    ui_profile_login_draw();
  } else {
    ui_setting_list_draw(&s_pane);
    ui_scroll_indicator_draw(UI_PAGE_SCROLL_X, UI_PAGE_BODY_Y, UI_PAGE_PANE_H, s_pane.count,
                             UI_PAGE_PANE_ROWS, s_pane.scroll);
    /* The toast covers the description's place, so the description waits until it is gone. */
    if (!ui_toast_active())
      draw_description();
  }

  UiHintItem hints[UI_HINT_MAX_ITEMS];
  ui_hint_row_layout(&s_hints, hints, build_hints(hints));
  ui_hint_row_draw(&s_hints);
  ui_toast_draw();

  if (!back)
    return UI_SCREEN_TYPE_PROFILE;
  disarm();
  ui_home_select_profile_group(s_group);
  return UI_SCREEN_TYPE_MAIN;
}
