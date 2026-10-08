/**
 * @file ui_profile.c
 * @brief The XMB Profile page (SPEC.md section 3.7)
 *
 * Rows are data: ProfileRowDef says what a row is called, what kind it is, its description, how to
 * read its value and what it does. A group is a table of rows. The Connection and PlayStation
 * Network groups have no rows yet.
 */

#include "ui/ui_profile.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <vita2d.h>

#include "context.h"
#include "logging.h"
#include "ui.h"
#include "ui/ui_bake.h"
#include "ui/ui_chrome_layout.h"
#include "ui/ui_group_list.h"
#include "ui/ui_hint_row.h"
#include "ui/ui_home.h"
#include "ui/ui_home_detail.h"
#include "ui/ui_input.h"
#include "ui/ui_page_frame.h"
#include "ui/ui_scroll_indicator.h"
#include "ui/ui_setting_list.h"
#include "ui/ui_settings_actions.h"
#include "ui/ui_text.h"
#include "ui/ui_text_wrap.h"
#include "ui/ui_theme.h"
#include "ui/ui_toast.h"
#include "ui/ui_top_bar.h"

/* ============================================================================
 * Row definitions
 * ============================================================================ */

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

/** Groups, in the order of Home's Profile list. */
static const ProfileGroup GROUPS[UI_PROFILE_GROUP_COUNT] = {
    [UI_PROFILE_GROUP_ACCOUNT] = {ACCOUNT_ROWS,
                                  (int)(sizeof(ACCOUNT_ROWS) / sizeof(ACCOUNT_ROWS[0]))},
    [UI_PROFILE_GROUP_CONNECTION] = {NULL, 0},
    [UI_PROFILE_GROUP_PSN] = {NULL, 0},
};

static const char *const GROUP_NAMES[UI_PROFILE_GROUP_COUNT] = {"Account", "Connection", PSN_NAME};

_Static_assert(UI_PROFILE_GROUP_COUNT <= UI_GROUP_MAX, "Profile groups must fit the group list");

/* ============================================================================
 * State
 * ============================================================================ */

static UiGroupList s_groups;
static UiSettingList s_pane;
static UiSettingItem s_items[UI_SETTING_MAX_ROWS];
static int s_group = 0;
/** The pane has focus; otherwise the group list has. */
static bool s_pane_focus = false;
/** Description of the focused row, wrapped once when the focus changes. */
static UiWrapped s_desc;
static int s_desc_group = -1;
static int s_desc_row = -1;
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

/** Rewrite the items' values from the config. */
static void fill_items(void) {
  const ProfileGroup *group = &GROUPS[s_group];
  for (int i = 0; i < group->count && i < UI_SETTING_MAX_ROWS; i++) {
    const ProfileRowDef *def = &group->rows[i];
    s_items[i] = (UiSettingItem){
        .label = def->label,
        .kind = def->kind,
        .value_text = def->value_text ? def->value_text() : NULL,
        .small_value = def->small_value,
    };
  }
}

/** Show group @index: its rows, with the pane focus on the first row. */
static void load_group(int index) {
  s_group = index;
  ui_group_list_set_current(&s_groups, index);
  fill_items();
  ui_setting_list_load(&s_pane, s_items, GROUPS[index].count);
  s_desc_group = -1;
}

/** Width function for ui_text_wrap(): the description's face. */
static int measure_description(const char *text, void *ctx) {
  (void)ctx;
  return ui_text_face_width(UI_FACE_T16, text);
}

/** Wrap the focused row's description when the group or the focused row changed. */
static void update_description(void) {
  if (s_desc_group == s_group && s_desc_row == s_pane.focus)
    return;
  s_desc_group = s_group;
  s_desc_row = s_pane.focus;
  const char *text = s_pane.count > 0 ? GROUPS[s_group].rows[s_pane.focus].description : NULL;
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

/** Run the focused action row. */
static void run_focused_row(void) {
  const ProfileRowDef *def = &GROUPS[s_group].rows[s_pane.focus];
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

/** Fill @out with the hints for what is focused (SPEC 3.7) and return how many. */
static int build_hints(UiHintItem out[UI_HINT_MAX_ITEMS]) {
  int n = 0;
  if (!s_pane_focus) {
    out[n++] = (UiHintItem){.action = UI_BTN_CONFIRM, .label = HINT_OPEN, .dim = s_pane.count == 0};
  } else {
    const ProfileRowDef *def = &GROUPS[s_group].rows[s_pane.focus];
    if (def->kind == UI_SETTING_ACTION)
      out[n++] = (UiHintItem){.action = UI_BTN_CONFIRM, .label = def->hint};
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
  /* A tapped hint acts as that button pressed in one frame; the D-pad hint has no action. */
  UiInput in = *ui_input_snapshot();
  const uint32_t tapped = ui_hint_row_tap(&s_hints, &in);
  if (tapped) {
    in.pressed |= tapped & ~(uint32_t)UI_BTN_DPAD;
    in.touch.pressed = false;
    in.touch.released = false;
    in.touch.down = false;
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

  handle_pane_event(ui_setting_list_input(&s_pane, &pane_in));
  const bool back =
      handle_group_event(ui_group_list_input(&s_groups, &group_in), ui_touch_tap(&in));

  fill_items();
  ui_setting_list_sync(&s_pane);
  update_description();
  update_identity();
  s_groups.focused = !s_pane_focus;
  s_pane.active = s_pane_focus;

  ui_page_frame_draw(UI_PAGE_ICON_PROFILE, TITLE);
  ui_top_bar_draw(NULL);
  ui_group_list_draw(&s_groups);
  draw_identity();
  ui_setting_list_draw(&s_pane);
  ui_scroll_indicator_draw(UI_PAGE_SCROLL_X, UI_PAGE_BODY_Y, UI_PAGE_PANE_H, s_pane.count,
                           UI_PAGE_PANE_ROWS, s_pane.scroll);
  /* The toast covers the description's place, so the description waits until it is gone. */
  if (!ui_toast_active())
    draw_description();

  UiHintItem hints[UI_HINT_MAX_ITEMS];
  ui_hint_row_layout(&s_hints, hints, build_hints(hints));
  ui_hint_row_draw(&s_hints);
  ui_toast_draw();

  if (!back)
    return UI_SCREEN_TYPE_PROFILE;
  ui_home_select_profile_group(s_group);
  return UI_SCREEN_TYPE_MAIN;
}
