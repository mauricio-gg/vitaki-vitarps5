/**
 * @file ui_controller_page.c
 * @brief The XMB Controller page, Summary page 1 "Buttons" (SPEC.md C20 and section 3.8)
 *
 * Input goes to the Shoulder Mapping popup while it is open, otherwise to the page. The page keeps
 * no copy of the mapping: callout text is rebuilt only when the output it names changes, and the
 * model (ui_controller_model.c) holds the data and saves every change.
 */

#include "ui/ui_controller_page.h"

#include <stdio.h>
#include <string.h>

#include <vita2d.h>

#include "context.h"
#include "controller.h"
#include "ui/ui_bake.h"
#include "ui/ui_chevron.h"
#include "ui/ui_component.h"
#include "ui/ui_controller_diagram.h"
#include "ui/ui_controller_model.h"
#include "ui/ui_freeze.h"
#include "ui/ui_hint_row.h"
#include "ui/ui_home.h"
#include "ui/ui_input.h"
#include "ui/ui_list_popup.h"
#include "ui/ui_page_frame.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"
#include "ui/ui_top_bar.h"

/* Copy (SPEC section 5) */
static const char TITLE[] = "Controller";
static const char PAGE_LABEL[] = "Page 1/2 \xC2\xB7 Buttons";
static const char CALLOUT_FORMAT[] = "%s \xE2\x86\x92 %s";
static const char MAPPING_TITLE[] = "Shoulder Mapping";
static const char HINT_PRESET[] = "Preset";
static const char HINT_SHOULDER_PICK[] = "L1 / R1";
static const char HINT_SHOULDER[] = "Shoulder";
static const char HINT_BACK[] = "Back";
static const char HINT_ASSIGN[] = "Assign";
static const char HINT_CANCEL[] = "Cancel";

/** The two shoulder buttons, in callout order (left, right). */
typedef enum shoulder_t {
  SHOULDER_L1 = 0,
  SHOULDER_R1,
  SHOULDER_COUNT,
} Shoulder;

static const VitakiCtrlIn SHOULDER_INPUT[SHOULDER_COUNT] = {VITAKI_CTRL_IN_L1, VITAKI_CTRL_IN_R1};
static const char *const SHOULDER_NAME[SHOULDER_COUNT] = {"L1", "R1"};
static const char *const SHOULDER_SUBTITLE[SHOULDER_COUNT] = {"Left Shoulder (L1)",
                                                              "Right Shoulder (R1)"};

/** One callout: its text, rects and leader, rebuilt only when the output it names changes. */
typedef struct callout_t {
  char text[UI_CTRL_CALLOUT_TEXT_MAX];
  int output;  ///< the output @text names; NO_OUTPUT before the first build
  int text_w;
  UiRect visible;
  UiRect hit;
  int leader_x1;  ///< the leader starts under the callout...
  int leader_y1;
  int leader_x2;  ///< ...and ends in the dot at the shoulder
  int leader_y2;
} Callout;

#define NO_OUTPUT (-2)

/* ============================================================================
 * State
 * ============================================================================ */

static DiagramState s_diagram;
static bool s_diagram_ready = false;
static vita2d_texture *s_chevron_left = NULL;
static vita2d_texture *s_chevron_right = NULL;
static vita2d_texture *s_back_icon = NULL;
static vita2d_texture *s_dot = NULL;

static Callout s_callouts[SHOULDER_COUNT];
static Shoulder s_focus = SHOULDER_L1;
/** The preset the label's position was computed for; -1 before the first frame. */
static int s_label_preset = -1;
static int s_label_text_x = 0;
static int s_footer_page_x = 0;

/** The Shoulder Mapping popup, and the inputs its choice is assigned to. */
static UiListPopup s_popup;
static VitakiCtrlIn s_targets[UI_CTRL_ZONE_COUNT + 1];
static int s_target_count = 0;

/** Hint layout of the last drawn frame; taps are resolved against it. */
static UiHintLayout s_hints;

/* Rects fixed by the layout constants */
static UiRect s_back_hit;
static UiRect s_chevron_left_hit;
static UiRect s_label_hit;
static UiRect s_chevron_right_hit;

/* ============================================================================
 * Setup
 * ============================================================================ */

/** Alpha of the leader dot: a disc filling the texture. */
static float dot_alpha(float px, float py, const void *ctx) {
  (void)ctx;
  const float r = (float)UI_CTRL_DOT / 2.0f;
  const float dx = px - r;
  const float dy = py - r;
  return r + 0.5f - __builtin_sqrtf(dx * dx + dy * dy);
}

void ui_controller_page_init(void) {
  s_chevron_left = ui_chevron_bake(UI_CHEVRON_LEFT, UI_CHOICE_ARROW_ART, UI_CHOICE_ARROW_STROKE);
  s_chevron_right = ui_chevron_bake(UI_CHEVRON_RIGHT, UI_CHOICE_ARROW_ART, UI_CHOICE_ARROW_STROKE);
  s_back_icon = ui_chevron_bake(UI_CHEVRON_LEFT, UI_CTRL_BACK_ART, UI_CTRL_BACK_STROKE);
  s_dot = ui_bake_white(UI_CTRL_DOT, UI_CTRL_DOT, dot_alpha, NULL);

  s_back_hit = (UiRect){0, UI_TITLE_Y, UI_TAP_MIN, UI_TAP_MIN};
  s_chevron_right_hit = (UiRect){UI_CONTENT_RIGHT - UI_TAP_MIN, UI_TITLE_Y, UI_TAP_MIN, UI_TAP_MIN};
  s_label_hit = (UiRect){s_chevron_right_hit.x - UI_CTRL_PRESET_GAP - UI_CTRL_PRESET_LABEL_W,
                         UI_TITLE_Y, UI_CTRL_PRESET_LABEL_W, UI_TAP_MIN};
  s_chevron_left_hit =
      (UiRect){s_label_hit.x - UI_CTRL_PRESET_GAP - UI_TAP_MIN, UI_TITLE_Y, UI_TAP_MIN, UI_TAP_MIN};

  memset(&s_popup, 0, sizeof(s_popup));
  for (int i = 0; i < SHOULDER_COUNT; i++)
    s_callouts[i].output = NO_OUTPUT;
  s_hints.count = 0;
}

/** Load the diagram textures the first time the page is drawn (as the old screen did). */
static void ensure_diagram(void) {
  if (s_diagram_ready)
    return;
  ui_diagram_init(&s_diagram);
  s_diagram.mode = CTRL_VIEW_FRONT;
  s_diagram.detail_view = CTRL_DETAIL_SUMMARY;
  s_diagram_ready = true;
}

void ui_controller_page_open(int preset) {
  ui_controller_model_select_preset(preset);
  s_focus = SHOULDER_L1;
  s_label_preset = -1;
  ui_list_popup_close(&s_popup);
  s_hints.count = 0;
}

/* ============================================================================
 * Layout (rebuilt only when what it shows changes)
 * ============================================================================ */

/** Rebuild callout @i when the output it names changed since the last frame. */
static void layout_callout(int i) {
  Callout *c = &s_callouts[i];
  const int output = (int)ui_controller_model_output(SHOULDER_INPUT[i]);
  if (output == c->output)
    return;
  c->output = output;
  snprintf(c->text, sizeof(c->text), CALLOUT_FORMAT, SHOULDER_NAME[i],
           controller_output_name((VitakiCtrlOut)output));
  c->text_w = ui_text_face_width(UI_FACE_T20, c->text);

  const int w = c->text_w > UI_CTRL_CALLOUT_W ? c->text_w : UI_CTRL_CALLOUT_W;
  const int x = i == SHOULDER_L1 ? UI_CTRL_CALLOUT_LEFT_X : UI_CTRL_CALLOUT_RIGHT_EDGE - w;
  c->visible = (UiRect){x, UI_CTRL_CALLOUT_Y, w, UI_CTRL_CALLOUT_H};
  c->hit = (UiRect){x, UI_CTRL_CALLOUT_Y - UI_CTRL_CALLOUT_HIT_PAD, w,
                    UI_CTRL_CALLOUT_H + 2 * UI_CTRL_CALLOUT_HIT_PAD};

  const int shoulder_x_pct =
      i == SHOULDER_L1 ? UI_CTRL_SHOULDER_X_PCT : 100 - UI_CTRL_SHOULDER_X_PCT;
  c->leader_x1 = x + w / 2;
  c->leader_y1 = UI_CTRL_CALLOUT_Y + UI_CTRL_CALLOUT_H;
  c->leader_x2 = UI_CTRL_FRONT_X + UI_CTRL_FRONT_W * shoulder_x_pct / 100;
  c->leader_y2 = UI_CTRL_FRONT_Y + UI_CTRL_FRONT_H * UI_CTRL_SHOULDER_Y_PCT / 100;
}

/** Place the preset name in its label box and the page label at the right margin, when the
 * preset changed. */
static void layout_labels(void) {
  const int preset = ui_controller_model_preset();
  if (preset == s_label_preset)
    return;
  s_label_preset = preset;
  const int name_w = ui_text_face_width(UI_FACE_T20, g_controller_presets[preset].name);
  s_label_text_x = s_label_hit.x + (s_label_hit.w - name_w) / 2;
  s_footer_page_x = UI_CONTENT_RIGHT - ui_text_face_width(UI_FACE_T16, PAGE_LABEL);
}

/** Bring the labels and both callouts up to date with the model. */
static void layout_all(void) {
  layout_labels();
  for (int i = 0; i < SHOULDER_COUNT; i++)
    layout_callout(i);
}

/* ============================================================================
 * Shoulder Mapping popup
 * ============================================================================ */

/**
 * open_mapping_popup() - Open the mapping popup (C11 size L, C12 list) for @count inputs.
 * @title:    "Shoulder Mapping", or a touch mapping title.
 * @subtitle: What the inputs are ("Left Shoulder (L1)", "Front C2", "N Zones Selected").
 * @targets:  The inputs the chosen output is assigned to; copied.
 *
 * The 11 outputs are the rows; the one every input holds is ticked and focused. When the inputs
 * differ nothing is ticked and the first row is focused.
 */
static void open_mapping_popup(const char *title, const char *subtitle, const VitakiCtrlIn *targets,
                               int count) {
  if (count <= 0 || count > (int)(sizeof(s_targets) / sizeof(s_targets[0]))) {
    LOGE("Controller page: cannot open the mapping popup for %d inputs", count);
    return;
  }
  memcpy(s_targets, targets, (size_t)count * sizeof(targets[0]));
  s_target_count = count;

  const int common = ui_controller_model_common_output(targets, count);
  const int current_row =
      common == UI_CTRL_MIXED ? -1 : ui_controller_model_output_row((VitakiCtrlOut)common);

  UiListRow rows[UI_CTRL_OUTPUT_COUNT] = {0};
  for (int i = 0; i < UI_CTRL_OUTPUT_COUNT; i++) {
    snprintf(rows[i].label, sizeof(rows[i].label), "%s",
             controller_output_name(ui_controller_model_output_choice(i)));
    rows[i].current = i == current_row;
  }
  ui_list_popup_open(&s_popup, &(UiListPopupSpec){
                                   .size = UI_POPUP_SIZE_L,
                                   .title = title,
                                   .subtitle = subtitle,
                                   .confirm_label = HINT_ASSIGN,
                                   .cancel_label = HINT_CANCEL,
                                   .rows = rows,
                                   .count = UI_CTRL_OUTPUT_COUNT,
                                   .focus = current_row >= 0 ? current_row : 0,
                               });
}

/** Open the Shoulder Mapping popup for shoulder @which. */
static void open_shoulder_popup(Shoulder which) {
  open_mapping_popup(MAPPING_TITLE, SHOULDER_SUBTITLE[which], &SHOULDER_INPUT[which], 1);
}

/** Drive the popup: a choice is assigned and saved, Cancel and a tap outside close it unchanged. */
static void update_popup(const UiInput *in) {
  const UiEvent event = ui_list_popup_input(&s_popup, in);
  if (event == UI_EVENT_ACTIVATED) {
    ui_controller_model_assign(s_targets, s_target_count,
                               ui_controller_model_output_choice(s_popup.activated));
    ui_list_popup_close(&s_popup);
  } else if (event == UI_EVENT_CANCELLED) {
    ui_list_popup_close(&s_popup);
  }
}

/* ============================================================================
 * Page input
 * ============================================================================ */

/** True when this frame is a tap that lifted inside @r. */
static bool tapped_in(const UiInput *in, UiRect r) {
  return ui_touch_tap(in) && ui_rect_contains(r, in->touch.x, in->touch.y);
}

/**
 * update_page() - Act on this frame's input.
 * @return true when the page should go back to Home
 */
static bool update_page(const UiInput *in) {
  if ((in->pressed & UI_BTN_CANCEL) || tapped_in(in, s_back_hit))
    return true;

  /* Small controls win over the diagram (SPEC 2.0); the callouts' hit rects reach 8 px past
   * their text. */
  if (tapped_in(in, s_chevron_left_hit)) {
    ui_controller_model_step_preset(-1);
  } else if (tapped_in(in, s_chevron_right_hit) || tapped_in(in, s_label_hit)) {
    ui_controller_model_step_preset(1);
  } else {
    for (int i = 0; i < SHOULDER_COUNT; i++) {
      if (tapped_in(in, s_callouts[i].hit)) {
        s_focus = (Shoulder)i;
        open_shoulder_popup(s_focus);
        return false;
      }
    }
  }

  /* A switch is one press, not a hold-repeat: each one saves the config. */
  if (in->pressed & UI_BTN_LEFT)
    ui_controller_model_step_preset(-1);
  else if (in->pressed & UI_BTN_RIGHT)
    ui_controller_model_step_preset(1);

  if (in->pressed & UI_BTN_UP)
    s_focus = SHOULDER_L1;
  else if (in->pressed & UI_BTN_DOWN)
    s_focus = SHOULDER_R1;

  if (in->pressed & UI_BTN_CONFIRM)
    open_shoulder_popup(s_focus);
  return false;
}

/* ============================================================================
 * Draw
 * ============================================================================ */

/** Draw a 48 x 48 hit box's art centred in it. */
static void draw_centered(vita2d_texture *tex, UiRect box, int art, uint32_t color) {
  if (!tex)
    return;
  vita2d_draw_texture_tint(tex, (float)(box.x + (box.w - art) / 2),
                           (float)(box.y + (box.h - art) / 2), color);
}

/** Draw the back chevron and the preset switcher. Paper cost 4. */
static void draw_title_row_controls(void) {
  draw_centered(s_back_icon, s_back_hit, UI_CTRL_BACK_ART, UI_TEXT_2);
  draw_centered(s_chevron_left, s_chevron_left_hit, UI_CHOICE_ARROW_ART, UI_TEXT_2);
  ui_text_draw_face_centered_v(UI_FACE_T20, s_label_text_x, UI_TITLE_Y, UI_TAP_MIN, UI_TEXT,
                               g_controller_presets[s_label_preset].name);
  draw_centered(s_chevron_right, s_chevron_right_hit, UI_CHOICE_ARROW_ART, UI_TEXT_2);
}

/**
 * Draw callout @i: its text, the underline (2 px white with a glow when focused, else 1 px) and
 * the leader ending in a dot at the shoulder. Paper cost 4 (5 focused).
 */
static void draw_callout(int i) {
  const Callout *c = &s_callouts[i];
  const bool focused = s_focus == (Shoulder)i;
  const int text_x = i == SHOULDER_L1 ? c->visible.x : c->visible.x + c->visible.w - c->text_w;

  if (focused) {
    ui_glow_draw_rect(
        (UiRect){text_x, c->visible.y + (c->visible.h - UI_T20_LINE) / 2, c->text_w, UI_T20_LINE},
        UI_ROW_GLOW, ui_color_scale_alpha(UI_GLOW, (float)UI_ROW_GLOW_PCT / 100.0f));
  }
  vita2d_draw_line((float)c->leader_x1, (float)c->leader_y1, (float)c->leader_x2,
                   (float)c->leader_y2, UI_LEADER);
  if (s_dot) {
    vita2d_draw_texture_tint(s_dot, (float)(c->leader_x2 - UI_CTRL_DOT / 2),
                             (float)(c->leader_y2 - UI_CTRL_DOT / 2), UI_TEXT);
  }
  ui_text_draw_face_centered_v(UI_FACE_T20, text_x, c->visible.y, c->visible.h,
                               focused ? UI_TEXT : UI_TEXT_2, c->text);

  const int line = focused ? UI_LW2 : UI_LW1;
  vita2d_draw_rectangle((float)c->visible.x, (float)(c->visible.y + c->visible.h - line),
                        (float)c->visible.w, (float)line, focused ? UI_TEXT : UI_LEADER);
}

/** Draw the footers: the preset description at the left, the page label at the right. Paper cost 2.
 */
static void draw_footers(void) {
  ui_text_draw_face_centered_v(UI_FACE_T16, UI_MARGIN_X, UI_CTRL_FOOT_Y, UI_CTRL_FOOT_H, UI_TEXT_2,
                               g_controller_presets[s_label_preset].description);
  ui_text_draw_face_centered_v(UI_FACE_T16, s_footer_page_x, UI_CTRL_FOOT_Y, UI_CTRL_FOOT_H,
                               UI_TEXT_2, PAGE_LABEL);
}

/** Draw the page behind the popup layer: frame, top bar, controls, diagram, callouts, footers. */
static void draw_page(void) {
  ui_page_frame_draw(UI_PAGE_ICON_CONTROLLER, TITLE);
  ui_top_bar_draw(NULL);
  draw_title_row_controls();
  ui_diagram_render(&s_diagram, ui_controller_model_map(), UI_CTRL_FRONT_X, UI_CTRL_FRONT_Y,
                    UI_CTRL_FRONT_W, UI_CTRL_FRONT_H);
  for (int i = 0; i < SHOULDER_COUNT; i++)
    draw_callout(i);
  draw_footers();
}

/** Fill @out with the hint row (SPEC 3.8) and return how many. Page, Zones and Clear join the row
 * with the features they trigger. */
static int build_hints(UiHintItem out[UI_HINT_MAX_ITEMS]) {
  if (ui_list_popup_is_open(&s_popup))
    return ui_list_popup_hints(&s_popup, out);
  int n = 0;
  out[n++] = (UiHintItem){
      .action = UI_BTN_LEFT | UI_BTN_RIGHT, .label = HINT_PRESET, .low_priority = true};
  out[n++] = (UiHintItem){.action = UI_BTN_UP | UI_BTN_DOWN, .label = HINT_SHOULDER_PICK};
  out[n++] = (UiHintItem){.action = UI_BTN_CONFIRM, .label = HINT_SHOULDER};
  out[n++] = (UiHintItem){.action = UI_BTN_CANCEL, .label = HINT_BACK};
  return n;
}

/* ============================================================================
 * Frame
 * ============================================================================ */

UIScreenType ui_controller_page_frame(void) {
  ensure_diagram();

  /* The freeze state at the start of the frame decides whether ui.c already drew the frozen copy
   * (then the page behind the popup is not drawn live); whether this frame becomes the copy is
   * known only after input, which is what opens the popup (see Home's frame). */
  const bool frozen = ui_freeze_is_ready();

  /* A tapped hint acts as that button pressed in one frame; the D-pad hints have no action. */
  UiInput in = *ui_input_snapshot();
  const uint32_t tapped = ui_hint_row_tap(&s_hints, &in);
  if (tapped) {
    in.pressed |= tapped & ~(uint32_t)UI_BTN_DPAD;
    in.touch.pressed = false;
    in.touch.released = false;
    in.touch.down = false;
  }

  layout_all();

  bool back = false;
  if (ui_list_popup_is_open(&s_popup))
    update_popup(&in);
  else
    back = update_page(&in);

  /* Input may have changed the preset or an output: lay out again before drawing. */
  layout_all();

  const bool capturing = ui_freeze_is_capturing();
  if (!frozen)
    draw_page();
  if (!capturing)
    ui_list_popup_draw(&s_popup);

  if (capturing) {
    s_hints.count = 0;
  } else {
    UiHintItem hints[UI_HINT_MAX_ITEMS];
    ui_hint_row_layout(&s_hints, hints, build_hints(hints));
    ui_hint_row_draw(&s_hints);
  }

  if (!back)
    return UI_SCREEN_TYPE_CONTROLLER;
  ui_home_select_controller_preset(ui_controller_model_preset());
  return UI_SCREEN_TYPE_MAIN;
}
