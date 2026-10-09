/**
 * @file ui_settings.c
 * @brief The XMB Settings page (SPEC.md section 3.6)
 *
 * Rows are data: SettingDef says what a row is called, what kind it is, what it does, how to read
 * its value and how to change it. A group is a table of rows. Adding a setting means adding a row
 * to its group's table (and, if it has a side effect, the function that applies it).
 */

#include "ui/ui_settings.h"

#include "context.h"
#include "logging.h"
#include "video.h"
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
#include "ui/ui_top_bar.h"
#include "ui/ui_value_labels.h"

/* ============================================================================
 * Row definitions
 * ============================================================================ */

/**
 * One setting. A toggle fills @is_on, a choice fills @value_text. @change applies one step
 * (+1 forward, -1 back; a toggle ignores the way), saves the config and runs the side effect.
 */
typedef struct setting_def_t {
  const char *label;
  UiSettingKind kind;
  const char *description;
  bool (*is_on)(void);
  const char *(*value_text)(void);
  void (*change)(int step);
} SettingDef;

typedef struct setting_group_t {
  const SettingDef *rows;
  int count;
} SettingGroup;

/** Index @current moved by @step inside 0..@count-1, wrapping both ways. */
static int wrap_index(int current, int step, int count) {
  return (current + step + count) % count;
}

/* Video */

static const char *quality_text(void) {
  return ui_label_resolution(context.config.resolution);
}

static void quality_change(int step) {
  (void)step; /* two values: one step either way leads to the other */
  context.config.resolution = ui_settings_next_resolution(context.config.resolution);
  ui_settings_persist_config();
}

static const char *latency_text(void) {
  return ui_label_latency_mode(context.config.latency_mode);
}

static void latency_change(int step) {
  context.config.latency_mode = (VitaChiakiLatencyMode)wrap_index((int)context.config.latency_mode,
                                                                  step, VITA_LATENCY_MODE_COUNT);
  ui_settings_persist_config();
}

static const char *fps_text(void) {
  return ui_label_fps(context.config.fps);
}

static void fps_change(int step) {
  (void)step; /* two values: one step either way leads to the other */
  context.config.fps = context.config.fps == CHIAKI_VIDEO_FPS_PRESET_30
                           ? CHIAKI_VIDEO_FPS_PRESET_60
                           : CHIAKI_VIDEO_FPS_PRESET_30;
  ui_settings_persist_config();
}

static bool force_30fps_is_on(void) {
  return context.config.force_30fps;
}

static void force_30fps_change(int step) {
  (void)step;
  context.config.force_30fps = !context.config.force_30fps;
  ui_settings_persist_config();
  ui_settings_apply_force_30fps();
}

static bool fill_screen_is_on(void) {
  return context.config.stretch_video;
}

static void fill_screen_change(int step) {
  (void)step;
  context.config.stretch_video = !context.config.stretch_video;
  ui_settings_persist_config();
}

static const SettingDef VIDEO_ROWS[] = {
    {"Quality Preset", UI_SETTING_CHOICE, "Video resolution requested from the console.", NULL,
     quality_text, quality_change},
    {"Latency Mode", UI_SETTING_CHOICE,
     "Sets the target bitrate. Higher looks better but needs a stronger connection.", NULL,
     latency_text, latency_change},
    {"FPS Target", UI_SETTING_CHOICE, "Frame rate requested from the console.", NULL, fps_text,
     fps_change},
    {"Force 30 FPS Output", UI_SETTING_TOGGLE, "Output video at 30 FPS.", force_30fps_is_on, NULL,
     force_30fps_change},
    {"Fill Screen", UI_SETTING_TOGGLE, "Stretch the video to fill the whole screen.",
     fill_screen_is_on, NULL, fill_screen_change},
};

/* Network */

static bool auto_discovery_is_on(void) {
  return context.config.auto_discovery;
}

/* Read at startup only (SPEC 3.6): saving is all there is to do. */
static void auto_discovery_change(int step) {
  (void)step;
  context.config.auto_discovery = !context.config.auto_discovery;
  ui_settings_persist_config();
}

static bool psn_internet_is_on(void) {
  return context.config.psn_remoteplay_enabled;
}

static void psn_internet_change(int step) {
  (void)step;
  context.config.psn_remoteplay_enabled = !context.config.psn_remoteplay_enabled;
  ui_settings_persist_config();
}

static const SettingDef NETWORK_ROWS[] = {
    {"Auto Discovery", UI_SETTING_TOGGLE,
     "Find consoles on your network automatically. Takes effect the next time the app starts.",
     auto_discovery_is_on, NULL, auto_discovery_change},
    {"Enable PSN Internet Mode", UI_SETTING_TOGGLE,
     "Connect to your consoles over the internet with your PSN account.", psn_internet_is_on, NULL,
     psn_internet_change},
};

/* Display */

static bool show_latency_is_on(void) {
  return context.config.show_latency;
}

static void show_latency_change(int step) {
  (void)step;
  context.config.show_latency = !context.config.show_latency;
  ui_settings_persist_config();
}

static bool network_alerts_is_on(void) {
  return context.config.show_network_indicator;
}

static void network_alerts_change(int step) {
  (void)step;
  context.config.show_network_indicator = !context.config.show_network_indicator;
  ui_settings_persist_config();
  if (!context.config.show_network_indicator)
    vitavideo_hide_poor_net_indicator();
}

static bool exit_hint_is_on(void) {
  return context.config.show_stream_exit_hint;
}

static void exit_hint_change(int step) {
  (void)step;
  context.config.show_stream_exit_hint = !context.config.show_stream_exit_hint;
  ui_settings_persist_config();
}

static bool button_hints_is_on(void) {
  return context.config.show_button_hints;
}

static void button_hints_change(int step) {
  (void)step;
  context.config.show_button_hints = !context.config.show_button_hints;
  ui_settings_persist_config();
}

static const char *background_blur_text(void) {
  return ui_label_background_blur(context.config.background_blur);
}

/* The background draws from this field every frame, so storing it is all it takes to apply. */
static void background_blur_change(int step) {
  context.config.background_blur = (VitaChiakiBackgroundBlur)wrap_index(
      (int)context.config.background_blur, step, VITA_BACKGROUND_BLUR_COUNT);
  ui_settings_persist_config();
}

static const SettingDef DISPLAY_ROWS[] = {
    {"Show Latency", UI_SETTING_TOGGLE, "Show latency and frame rate in the stream overlay.",
     show_latency_is_on, NULL, show_latency_change},
    {"Show Network Alerts", UI_SETTING_TOGGLE, "Show a badge when the connection becomes unstable.",
     network_alerts_is_on, NULL, network_alerts_change},
    {"Show Exit Shortcut Hint", UI_SETTING_TOGGLE, "Show how to leave the stream when it starts.",
     exit_hint_is_on, NULL, exit_hint_change},
    {"Show Button Hints", UI_SETTING_TOGGLE, "Show the button hints along the bottom of menus.",
     button_hints_is_on, NULL, button_hints_change},
    {"Background Blur", UI_SETTING_CHOICE,
     "Blur the background waves behind menus. Strong and Dark are softer and calmer.", NULL,
     background_blur_text, background_blur_change},
};

/* Controls */

static bool circle_confirm_is_on(void) {
  return context.config.circle_btn_confirm;
}

static void circle_confirm_change(int step) {
  (void)step;
  context.config.circle_btn_confirm = !context.config.circle_btn_confirm;
  ui_settings_persist_config();
  ui_settings_apply_circle_confirm();
}

static const SettingDef CONTROLS_ROWS[] = {
    {"Circle Button Confirm", UI_SETTING_TOGGLE,
     "Use Circle to confirm and Cross to go back, on every screen.", circle_confirm_is_on, NULL,
     circle_confirm_change},
};

/* Advanced */

static bool clamp_restart_is_on(void) {
  return context.config.clamp_soft_restart_bitrate;
}

static void clamp_restart_change(int step) {
  (void)step;
  context.config.clamp_soft_restart_bitrate = !context.config.clamp_soft_restart_bitrate;
  ui_settings_persist_config();
}

static bool motion_during_loss_is_on(void) {
  return context.config.submit_on_missing_ref;
}

static void motion_during_loss_change(int step) {
  (void)step;
  context.config.submit_on_missing_ref = !context.config.submit_on_missing_ref;
  ui_settings_persist_config();
}

static bool logging_is_on(void) {
  return context.config.logging.enabled;
}

static void logging_change(int step) {
  (void)step;
  context.config.logging.enabled = !context.config.logging.enabled;
  ui_settings_persist_config();
  vita_log_update_enabled(context.config.logging.enabled);
  context.log.level_mask = vita_logging_profile_mask(
      context.config.logging.enabled ? VITA_LOG_PROFILE_VERBOSE : VITA_LOG_PROFILE_ERRORS);
}

static const SettingDef ADVANCED_ROWS[] = {
    {"Clamp Soft Restart Bitrate", UI_SETTING_TOGGLE,
     "Limit the bitrate when the stream restarts after packet loss.", clamp_restart_is_on, NULL,
     clamp_restart_change},
    {"Motion during loss (artifacts) (Experimental)", UI_SETTING_TOGGLE,
     "Keep motion going while packets are lost. May show visual artifacts.",
     motion_during_loss_is_on, NULL, motion_during_loss_change},
    {"Enable Logging", UI_SETTING_TOGGLE, "Write diagnostic logs on the Vita for troubleshooting.",
     logging_is_on, NULL, logging_change},
};

/** Groups, in the order of Home's Settings list. */
static const SettingGroup GROUPS[UI_SETTINGS_GROUP_COUNT] = {
    [UI_SETTINGS_GROUP_VIDEO] = {VIDEO_ROWS, (int)(sizeof(VIDEO_ROWS) / sizeof(VIDEO_ROWS[0]))},
    [UI_SETTINGS_GROUP_NETWORK] = {NETWORK_ROWS,
                                   (int)(sizeof(NETWORK_ROWS) / sizeof(NETWORK_ROWS[0]))},
    [UI_SETTINGS_GROUP_DISPLAY] = {DISPLAY_ROWS,
                                   (int)(sizeof(DISPLAY_ROWS) / sizeof(DISPLAY_ROWS[0]))},
    [UI_SETTINGS_GROUP_CONTROLS] = {CONTROLS_ROWS,
                                    (int)(sizeof(CONTROLS_ROWS) / sizeof(CONTROLS_ROWS[0]))},
    [UI_SETTINGS_GROUP_ADVANCED] = {ADVANCED_ROWS,
                                    (int)(sizeof(ADVANCED_ROWS) / sizeof(ADVANCED_ROWS[0]))},
};

static const char *const GROUP_NAMES[UI_SETTINGS_GROUP_COUNT] = {"Video", "Network", "Display",
                                                                 "Controls", "Advanced"};

_Static_assert(UI_SETTINGS_GROUP_COUNT <= UI_GROUP_MAX, "Settings groups must fit the group list");

/* Copy */
static const char TITLE[] = "Settings";
static const char HINT_OPEN[] = "Open";
static const char HINT_TOGGLE[] = "Toggle";
static const char HINT_NEXT[] = "Next";
static const char HINT_CHANGE[] = "Change";
static const char HINT_GROUP[] = "Group";
static const char HINT_BACK[] = "Back";

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

/* ============================================================================
 * Rows and description
 * ============================================================================ */

/** Rewrite the items' values from the config. */
static void fill_items(void) {
  const SettingGroup *group = &GROUPS[s_group];
  for (int i = 0; i < group->count && i < UI_SETTING_MAX_ROWS; i++) {
    const SettingDef *def = &group->rows[i];
    s_items[i] = (UiSettingItem){
        .label = def->label,
        .kind = def->kind,
        .on = def->is_on ? def->is_on() : false,
        .value_text = def->value_text ? def->value_text() : NULL,
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
  const char *text = s_pane.count > 0 ? GROUPS[s_group].rows[s_pane.focus].description : "";
  ui_text_wrap(text, UI_PAGE_PANE_W, measure_description, NULL, &s_desc);
}

/* ============================================================================
 * Setup
 * ============================================================================ */

void ui_settings_init(void) {
  ui_group_list_init(&s_groups, GROUP_NAMES, UI_SETTINGS_GROUP_COUNT);
  ui_setting_list_init(&s_pane);
  s_hints.count = 0;
  ui_settings_open(UI_SETTINGS_GROUP_VIDEO);
}

void ui_settings_open(int group) {
  if (group < 0 || group >= UI_SETTINGS_GROUP_COUNT)
    group = UI_SETTINGS_GROUP_VIDEO;
  load_group(group);
  s_pane_focus = s_pane.count > 0;
  s_hints.count = 0;
}

/* ============================================================================
 * Input
 * ============================================================================ */

/** Apply the change the pane asked for on its focused row. */
static void change_focused_row(void) {
  GROUPS[s_group].rows[s_pane.focus].change(s_pane.step);
}

/**
 * handle_pane_event() - Act on what the pane reported. Any event while the group list has focus
 * came from touch and moves the focus to the pane.
 */
static void handle_pane_event(UiEvent event) {
  switch (event) {
    case UI_EVENT_ACTIVATED:
      change_focused_row();
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

/** Fill @out with the hints for what is focused (SPEC 3.6) and return how many. */
static int build_hints(UiHintItem out[UI_HINT_MAX_ITEMS]) {
  int n = 0;
  if (!s_pane_focus) {
    out[n++] = (UiHintItem){.action = UI_BTN_CONFIRM, .label = HINT_OPEN, .dim = s_pane.count == 0};
  } else if (s_pane.items[s_pane.focus].kind == UI_SETTING_TOGGLE) {
    out[n++] = (UiHintItem){.action = UI_BTN_CONFIRM, .label = HINT_TOGGLE};
  } else {
    out[n++] = (UiHintItem){.action = UI_BTN_CONFIRM, .label = HINT_NEXT};
    out[n++] = (UiHintItem){.action = UI_BTN_LEFT | UI_BTN_RIGHT, .label = HINT_CHANGE};
  }
  if (s_pane_focus)
    out[n++] = (UiHintItem){.action = UI_BTN_L | UI_BTN_R, .label = HINT_GROUP};
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

UIScreenType ui_settings_frame(void) {
  /* A tapped hint acts as that button pressed in one frame; the D-pad hint has no action. */
  UiInput in = *ui_input_snapshot();
  const uint32_t tapped = ui_hint_row_tap(&s_hints, &in);
  if (tapped) {
    in.pressed |= tapped & ~(uint32_t)UI_BTN_DPAD;
    in.touch.pressed = false;
    in.touch.released = false;
    in.touch.down = false;
  }

  /* The back chevron is a small control and wins over everything under it; one tap leaves the page
   * whichever column has focus, so it reaches neither column. */
  const bool back_tapped = ui_page_frame_back_tapped(&in);
  if (back_tapped) {
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
      handle_group_event(ui_group_list_input(&s_groups, &group_in), ui_touch_tap(&in)) ||
      back_tapped;

  fill_items();
  ui_setting_list_sync(&s_pane);
  update_description();
  s_groups.focused = !s_pane_focus;
  s_pane.active = s_pane_focus;

  ui_page_frame_draw(UI_PAGE_ICON_GEAR, TITLE);
  ui_page_frame_back_draw();
  ui_top_bar_draw(NULL);
  ui_group_list_draw(&s_groups);
  ui_setting_list_draw(&s_pane);
  ui_scroll_indicator_draw(UI_PAGE_SCROLL_X, UI_PAGE_BODY_Y, UI_PAGE_PANE_H, s_pane.count,
                           UI_PAGE_PANE_ROWS, s_pane.scroll);
  draw_description();

  UiHintItem hints[UI_HINT_MAX_ITEMS];
  ui_hint_row_layout(&s_hints, hints, build_hints(hints));
  ui_hint_row_draw(&s_hints);

  if (!back)
    return UI_SCREEN_TYPE_SETTINGS;
  ui_home_select_settings_group(s_group);
  return UI_SCREEN_TYPE_MAIN;
}
