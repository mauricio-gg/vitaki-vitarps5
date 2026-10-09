/**
 * @file ui_controller_page.c
 * @brief The XMB Controller page: Summary pages 1 "Buttons" and 2 "Back Touch", and the Front and
 *        Rear Touch zone views (SPEC.md C20 and section 3.8)
 *
 * Input goes to the mapping popup while it is open, otherwise to the page. The page keeps no copy
 * of the mapping: callout text and the zone grids are rebuilt only when the output they show
 * changes, and the model (ui_controller_model.c) holds the data and saves every change. The grids
 * are ui_controller_zones.c's and the popup ui_controller_mapping.c's.
 */

#include "ui/ui_controller_page.h"

#include <stdio.h>
#include <string.h>

#include <vita2d.h>

#include "context.h"
#include "controller.h"
#include "ui/ui_bake.h"
#include "ui/ui_arrow.h"
#include "ui/ui_chevron.h"
#include "ui/ui_component.h"
#include "ui/ui_constants.h"
#include "ui/ui_controller_diagram.h"
#include "ui/ui_controller_mapping.h"
#include "ui/ui_controller_model.h"
#include "ui/ui_controller_zones.h"
#include "ui/ui_freeze.h"
#include "ui/ui_hint_row.h"
#include "ui/ui_home.h"
#include "ui/ui_input.h"
#include "ui/ui_page_frame.h"
#include "ui/ui_text.h"
#include "ui/ui_text_button.h"
#include "ui/ui_zone_select.h"
#include "ui/ui_theme.h"
#include "ui/ui_top_bar.h"

/* Copy (SPEC section 5) */
static const char TITLE[] = "Controller";
static const char TITLE_FRONT[] = "Front Touch";
static const char TITLE_REAR[] = "Rear Touch";
static const char PAGE_LABEL_BUTTONS[] = "Page 1/2 \xC2\xB7 Buttons";
static const char PAGE_LABEL_REAR_FORMAT[] = "Page 2/2 \xC2\xB7 Back Touch \xC2\xB7 %d %s";
static const char ZONES_ONE[] = "zone";
static const char ZONES_MANY[] = "zones";
static const char FOOT_ZONE_FORMAT[] = "Zone %s";
static const char FOOT_SELECTED_FORMAT[] = "%d Zones Selected";
static const char SPACE[] = " ";
static const char SUBTITLE_L1[] = "Left Shoulder (L1)";
static const char SUBTITLE_R1[] = "Right Shoulder (R1)";
static const char BUTTON_CLEAR[] = "Clear";
static const char BUTTON_WHOLE[] = "Whole surface";
static const char HINT_PRESET[] = "Preset";
static const char HINT_PAGE[] = "Page";
static const char HINT_SHOULDER_PICK[] = "L1 / R1";
static const char HINT_SHOULDER[] = "Shoulder";
static const char HINT_ZONES[] = "Zones";
static const char HINT_CLEAR[] = "Clear";
static const char HINT_BACK[] = "Back";
static const char HINT_MOVE[] = "Move";
static const char HINT_ASSIGN[] = "Assign";
static const char HINT_WHOLE[] = "Whole surface";

/** The two shoulder buttons, in callout order (left, right). */
typedef enum shoulder_t {
  SHOULDER_L1 = 0,
  SHOULDER_R1,
  SHOULDER_COUNT,
} Shoulder;

static const VitakiCtrlIn SHOULDER_INPUT[SHOULDER_COUNT] = {VITAKI_CTRL_IN_L1, VITAKI_CTRL_IN_R1};
static const char *const SHOULDER_NAME[SHOULDER_COUNT] = {"L1", "R1"};
static const char *const SHOULDER_SUBTITLE[SHOULDER_COUNT] = {SUBTITLE_L1, SUBTITLE_R1};

/** The two Summary pages: the front buttons and the rear touch pad. */
typedef enum page_t {
  PAGE_BUTTONS = 0,
  PAGE_BACK_TOUCH,
} Page;

/** One callout: its text, rects and leader, rebuilt only when the output it names changes. The
 * text is the shoulder name, the arrow, then @text (the output's name); @arrow_dx and @text_dx are
 * where the arrow and @text start, from the callout's text start. */
typedef struct callout_t {
  char text[UI_CTRL_CALLOUT_TEXT_MAX];
  int output;  ///< the output @text names; NO_OUTPUT before the first build
  int arrow_dx;
  int text_dx;
  int text_w;  ///< the whole width: name, arrow and output with their gaps
  UiRect visible;
  UiRect hit;
  int leader_x1;  ///< the leader starts under the callout...
  int leader_y1;
  int leader_x2;  ///< ...and ends in the dot at the shoulder
  int leader_y2;
} Callout;

#define NO_OUTPUT (-2)
#define FOOT_TEXT_MAX 48

/* ============================================================================
 * State
 * ============================================================================ */

static DiagramState s_diagram;
static bool s_diagram_ready = false;
static vita2d_texture *s_chevron_left = NULL;
static vita2d_texture *s_chevron_right = NULL;
static vita2d_texture *s_dot = NULL;
static vita2d_texture *s_arrow = NULL;

static Callout s_callouts[SHOULDER_COUNT];
static Shoulder s_focus = SHOULDER_L1;

/** Which view is showing: a Summary page, or the zone view of that page's side. */
static Page s_page = PAGE_BUTTONS;
static bool s_in_zones = false;

/** The preset the label's position was computed for; -1 before the first frame. */
static int s_label_preset = -1;
static int s_label_text_x = 0;

/** The page label at the footer's right: its text, position and tap rect. They are rebuilt when
 * the page or the rear zone count changes (s_footer_key). */
static char s_page_text[FOOT_TEXT_MAX];
static int s_page_text_x = 0;
static UiRect s_page_hit;
static int s_footer_key = -1;

/** Which footer button the D-pad has focus on: none (the callouts or the grid have it), Whole
 * surface (zone views only) or Clear. Applied to the buttons' focused flag each frame, because
 * laying the buttons out again resets them. */
typedef enum footer_focus_t {
  FOOT_NONE = 0,
  FOOT_WHOLE,
  FOOT_CLEAR,
} FooterFocus;
static FooterFocus s_footer_focus = FOOT_NONE;

/** The footer's left text in a zone view ("Zone C2", "N Zones Selected"), rebuilt when the cursor
 * or the number of picked cells changes (s_zone_foot_key). */
static char s_zone_foot[FOOT_TEXT_MAX];
static int s_zone_foot_key = -1;

/** Where the preset name sits after the title in a zone view. */
static int s_sub_x = 0;

/** The small Clear and Whole surface buttons. */
static UiTextButton s_clear_btn;
static UiTextButton s_whole_btn;

/** Hint layout of the last drawn frame; taps are resolved against it. */
static UiHintLayout s_hints;

/* Rects fixed by the layout constants */
static UiRect s_chevron_left_hit;
static UiRect s_label_hit;
static UiRect s_chevron_right_hit;

/* ============================================================================
 * Views
 * ============================================================================ */

/** The zone grid shown by the current view: the zone view's, or Summary page 2's read-only one. */
static UiCtrlZoneView zone_view(void) {
  if (s_in_zones)
    return s_page == PAGE_BUTTONS ? UI_CTRL_VIEW_FRONT : UI_CTRL_VIEW_REAR;
  return UI_CTRL_VIEW_SUMMARY_REAR;
}

/** The touch surface the current Summary page, or zone view, is about. */
static UiCtrlSide current_side(void) {
  return s_page == PAGE_BUTTONS ? UI_CTRL_SIDE_FRONT : UI_CTRL_SIDE_REAR;
}

/** Centre the footer buttons of the current view as a row in the footer band. */
static void layout_footer_buttons(void) {
  ui_text_button_init_small(&s_whole_btn, BUTTON_WHOLE, 0, UI_CTRL_FOOT_BTN_Y);
  ui_text_button_init_small(&s_clear_btn, BUTTON_CLEAR, 0, UI_CTRL_FOOT_BTN_Y);
  int total = s_clear_btn.visible.w;
  if (s_in_zones)
    total += UI_CTRL_FOOT_BTN_GAP + s_whole_btn.visible.w;
  int x = (VITA_WIDTH - total) / 2;
  if (s_in_zones) {
    ui_text_button_init_small(&s_whole_btn, BUTTON_WHOLE, x, UI_CTRL_FOOT_BTN_Y);
    x += s_whole_btn.visible.w + UI_CTRL_FOOT_BTN_GAP;
  }
  ui_text_button_init_small(&s_clear_btn, BUTTON_CLEAR, x, UI_CTRL_FOOT_BTN_Y);
}

/** Show the zone view of the current page's side, the cursor on A1 with nothing picked. */
static void enter_zones(void) {
  s_in_zones = true;
  ui_controller_zones_reset(zone_view());
  s_zone_foot_key = -1;
  const char *title = s_page == PAGE_BUTTONS ? TITLE_FRONT : TITLE_REAR;
  s_sub_x = UI_PAGE_TITLE_X + ui_text_face_width(UI_FACE_T28, title) + UI_CTRL_SUB_GAP;
  layout_footer_buttons();
  s_footer_focus = FOOT_NONE;
}

/** Go back from a zone view to the Summary page it came from. */
static void leave_zones(void) {
  ui_controller_zones_reset(zone_view());
  s_in_zones = false;
  layout_footer_buttons();
  s_footer_focus = FOOT_NONE;
}

/** Switch to the other Summary page. */
static void toggle_page(void) {
  s_page = s_page == PAGE_BUTTONS ? PAGE_BACK_TOUCH : PAGE_BUTTONS;
  s_footer_key = -1;
  s_footer_focus = FOOT_NONE;
}

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
  s_dot = ui_bake_white(UI_CTRL_DOT, UI_CTRL_DOT, dot_alpha, NULL);
  s_arrow =
      ui_arrow_bake(UI_CTRL_ARROW_W, UI_CTRL_ARROW_H, UI_CTRL_ARROW_HEAD, UI_CTRL_ARROW_STROKE);

  s_chevron_right_hit = (UiRect){UI_CONTENT_RIGHT - UI_TAP_MIN, UI_TITLE_Y, UI_TAP_MIN, UI_TAP_MIN};
  s_label_hit = (UiRect){s_chevron_right_hit.x - UI_CTRL_PRESET_GAP - UI_CTRL_PRESET_LABEL_W,
                         UI_TITLE_Y, UI_CTRL_PRESET_LABEL_W, UI_TAP_MIN};
  s_chevron_left_hit =
      (UiRect){s_label_hit.x - UI_CTRL_PRESET_GAP - UI_TAP_MIN, UI_TITLE_Y, UI_TAP_MIN, UI_TAP_MIN};

  for (int i = 0; i < SHOULDER_COUNT; i++)
    s_callouts[i].output = NO_OUTPUT;
  s_hints.count = 0;
  layout_footer_buttons();
}

/** Load the diagram textures the first time the page is drawn (as the old screen did). */
static void ensure_diagram(void) {
  if (s_diagram_ready)
    return;
  ui_diagram_init(&s_diagram);
  s_diagram_ready = true;
}

void ui_controller_page_open(int preset) {
  ui_controller_model_select_preset(preset);
  s_focus = SHOULDER_L1;
  s_page = PAGE_BUTTONS;
  s_in_zones = false;
  s_footer_focus = FOOT_NONE;
  s_label_preset = -1;
  s_footer_key = -1;
  ui_controller_mapping_close();
  ui_controller_zones_reset(UI_CTRL_VIEW_FRONT);
  ui_controller_zones_reset(UI_CTRL_VIEW_REAR);
  layout_footer_buttons();
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
  snprintf(c->text, sizeof(c->text), "%s", controller_output_name((VitakiCtrlOut)output));
  const int gap = ui_text_face_width(UI_FACE_T20, SPACE);
  c->arrow_dx = ui_text_face_width(UI_FACE_T20, SHOULDER_NAME[i]) + gap;
  c->text_dx = c->arrow_dx + UI_CTRL_ARROW_W + gap;
  c->text_w = c->text_dx + ui_text_face_width(UI_FACE_T20, c->text);

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

/** Place the preset name in its label box when the preset changed. */
static void layout_preset_label(void) {
  const int preset = ui_controller_model_preset();
  if (preset == s_label_preset)
    return;
  s_label_preset = preset;
  const int name_w = ui_text_face_width(UI_FACE_T20, g_controller_presets[preset].name);
  s_label_text_x = s_label_hit.x + (s_label_hit.w - name_w) / 2;
}

/** Rebuild the page label at the footer's right, with its tap rect, when the page or the number
 * of mapped rear zones it shows changed. */
static void layout_page_label(void) {
  const int zones =
      s_page == PAGE_BACK_TOUCH ? ui_controller_model_mapped_zones(UI_CTRL_SIDE_REAR) : 0;
  const int key = (int)s_page * (UI_CTRL_ZONES + 1) + zones;
  if (key == s_footer_key)
    return;
  s_footer_key = key;
  if (s_page == PAGE_BUTTONS) {
    snprintf(s_page_text, sizeof(s_page_text), "%s", PAGE_LABEL_BUTTONS);
  } else {
    snprintf(s_page_text, sizeof(s_page_text), PAGE_LABEL_REAR_FORMAT, zones,
             zones == 1 ? ZONES_ONE : ZONES_MANY);
  }
  const int w = ui_text_face_width(UI_FACE_T16, s_page_text);
  s_page_text_x = UI_CONTENT_RIGHT - w;
  s_page_hit =
      (UiRect){s_page_text_x - UI_CTRL_FOOT_HIT_PAD_X, UI_CTRL_FOOT_Y - UI_CTRL_FOOT_HIT_PAD_Y,
               w + 2 * UI_CTRL_FOOT_HIT_PAD_X, UI_TAP_MIN};
}

/** Rebuild the zone view's left footer when the cursor or the picked count changed. */
static void layout_zone_footer(void) {
  const UiCtrlZoneView view = zone_view();
  const int picked = ui_controller_zones_selection(view)->count;
  const int cursor = ui_controller_zones_cursor(view);
  const int key = picked > 1 ? UI_CTRL_ZONES + picked : cursor;
  if (key == s_zone_foot_key)
    return;
  s_zone_foot_key = key;
  if (picked > 1) {
    snprintf(s_zone_foot, sizeof(s_zone_foot), FOOT_SELECTED_FORMAT, picked);
  } else {
    snprintf(s_zone_foot, sizeof(s_zone_foot), FOOT_ZONE_FORMAT, ui_controller_zones_name(cursor));
  }
}

/** Bring everything the current view shows up to date with the model and the input. */
static void layout_all(void) {
  layout_preset_label();
  if (s_in_zones) {
    ui_controller_zones_sync(zone_view());
    layout_zone_footer();
  } else if (s_page == PAGE_BUTTONS) {
    for (int i = 0; i < SHOULDER_COUNT; i++)
      layout_callout(i);
  } else {
    ui_controller_zones_sync(UI_CTRL_VIEW_SUMMARY_REAR);
  }
  layout_page_label();
}

/* ============================================================================
 * Page input
 * ============================================================================ */

/** True when this frame is a tap that lifted inside @r. */
static bool tapped_in(const UiInput *in, UiRect r) {
  return ui_touch_tap(in) && ui_rect_contains(r, in->touch.x, in->touch.y);
}

/** Give the footer buttons their focused look from s_footer_focus. */
static void apply_footer_focus(void) {
  s_whole_btn.focused = s_footer_focus == FOOT_WHOLE;
  s_clear_btn.focused = s_footer_focus == FOOT_CLEAR;
}

/** Clear every output of the current side. */
static void clear_current_side(void) {
  ui_controller_model_assign_side(current_side(), VITAKI_CTRL_OUT_NONE);
}

/** Open the Shoulder Mapping popup for shoulder @which. */
static void open_shoulder_popup(Shoulder which) {
  ui_controller_mapping_open_shoulder(SHOULDER_INPUT[which], SHOULDER_SUBTITLE[which]);
}

/** Open the popup for the zones the grid just reported picked. */
static void open_selection_popup(void) {
  const UiZoneSelection *sel = ui_controller_zones_selection(zone_view());
  ui_controller_mapping_open_zones(current_side(), sel->cells, sel->count);
}

/** The box a tap on the diagram of the current Summary page opens the zone view from. */
static UiRect summary_diagram_rect(void) {
  if (s_page == PAGE_BUTTONS)
    return (UiRect){UI_CTRL_FRONT_X, UI_CTRL_FRONT_Y, UI_CTRL_FRONT_W, UI_CTRL_FRONT_H};
  return ui_controller_zones_diagram_rect(UI_CTRL_VIEW_SUMMARY_REAR);
}

/**
 * update_summary() - Act on this frame's input on a Summary page.
 * @return true when the page should go back to Home
 */
static bool update_summary(const UiInput *in) {
  apply_footer_focus();
  if ((in->pressed & UI_BTN_CANCEL) || ui_page_frame_back_tapped(in))
    return true;

  /* Small controls win over the diagram (SPEC 2.0); the callouts' hit rects reach 8 px past
   * their text. */
  if (tapped_in(in, s_chevron_left_hit)) {
    ui_controller_model_step_preset(-1);
  } else if (tapped_in(in, s_chevron_right_hit) || tapped_in(in, s_label_hit)) {
    ui_controller_model_step_preset(1);
  } else if (tapped_in(in, s_page_hit)) {
    toggle_page();
  } else if (ui_text_button_input(&s_clear_btn, in) == UI_EVENT_ACTIVATED) {
    clear_current_side();
  } else {
    if (s_page == PAGE_BUTTONS) {
      for (int i = 0; i < SHOULDER_COUNT; i++) {
        if (tapped_in(in, s_callouts[i].hit)) {
          s_focus = (Shoulder)i;
          open_shoulder_popup(s_focus);
          return false;
        }
      }
    }
    if (tapped_in(in, summary_diagram_rect())) {
      enter_zones();
      return false;
    }
  }

  /* A switch is one press, not a hold-repeat: each one saves the config. */
  if (in->pressed & UI_BTN_LEFT)
    ui_controller_model_step_preset(-1);
  else if (in->pressed & UI_BTN_RIGHT)
    ui_controller_model_step_preset(1);

  if (in->pressed & (UI_BTN_L | UI_BTN_R))
    toggle_page();

  /* Up and Down walk L1, R1 and then the Clear button (page 2 has only the button). */
  if (s_footer_focus == FOOT_CLEAR) {
    if (in->pressed & UI_BTN_UP)
      s_footer_focus = FOOT_NONE;
  } else if (s_page == PAGE_BUTTONS) {
    if (in->pressed & UI_BTN_UP)
      s_focus = SHOULDER_L1;
    else if ((in->pressed & UI_BTN_DOWN) && s_focus == SHOULDER_L1)
      s_focus = SHOULDER_R1;
    else if (in->pressed & UI_BTN_DOWN)
      s_footer_focus = FOOT_CLEAR;
  } else if (in->pressed & UI_BTN_DOWN) {
    s_footer_focus = FOOT_CLEAR;
  }

  if (in->pressed & UI_BTN_OPTIONS) {
    enter_zones();
  } else if (in->pressed & UI_BTN_CLEAR) {
    clear_current_side();
  } else if ((in->pressed & UI_BTN_CONFIRM) && s_footer_focus == FOOT_NONE) {
    if (s_page == PAGE_BUTTONS)
      open_shoulder_popup(s_focus);
    else
      enter_zones();
  }
  return false;
}

/**
 * Move the D-pad focus between the grid and the footer buttons of a zone view: Down from the
 * grid's bottom row goes to the button under that half of the grid, Left and Right switch between
 * the buttons, Up goes back to the grid.
 * @return true when the press was used, so the grid must not see it
 */
static bool move_footer_focus(const UiInput *in) {
  if (s_footer_focus == FOOT_NONE) {
    const int cursor = ui_controller_zones_cursor(zone_view());
    if ((in->pressed & UI_BTN_DOWN) && !(in->down & UI_BTN_CONFIRM) &&
        cursor / UI_ZONE_COLS == UI_ZONE_ROWS - 1) {
      s_footer_focus = cursor % UI_ZONE_COLS < UI_ZONE_COLS / 2 ? FOOT_WHOLE : FOOT_CLEAR;
      return true;
    }
    return false;
  }
  if (in->pressed & UI_BTN_UP)
    s_footer_focus = FOOT_NONE;
  else if (in->pressed & UI_BTN_LEFT)
    s_footer_focus = FOOT_WHOLE;
  else if (in->pressed & UI_BTN_RIGHT)
    s_footer_focus = FOOT_CLEAR;
  return true;
}

/** Act on this frame's input in a zone view: back, Whole surface, Clear, or the grid. */
static void update_zones(const UiInput *in) {
  if ((in->pressed & UI_BTN_CANCEL) || ui_page_frame_back_tapped(in)) {
    leave_zones();
    return;
  }
  const bool used = move_footer_focus(in);
  apply_footer_focus();
  if (ui_text_button_input(&s_whole_btn, in) == UI_EVENT_ACTIVATED ||
      (in->pressed & UI_BTN_OPTIONS)) {
    ui_controller_mapping_open_side(current_side());
  } else if (ui_text_button_input(&s_clear_btn, in) == UI_EVENT_ACTIVATED ||
             (in->pressed & UI_BTN_CLEAR)) {
    clear_current_side();
  } else if (!used && s_footer_focus == FOOT_NONE &&
             ui_controller_zones_input(zone_view(), in) == UI_EVENT_ACTIVATED) {
    open_selection_popup();
  }
}

/** Drive the open popup; when it closes, drop the picks the grid still shows. */
static void update_popup(const UiInput *in) {
  if (ui_controller_mapping_update(in) && s_in_zones)
    ui_controller_zones_clear_selection(zone_view());
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

/** Draw the preset switcher of a Summary page. Paper cost 3. */
static void draw_preset_switcher(void) {
  draw_centered(s_chevron_left, s_chevron_left_hit, UI_CHOICE_ARROW_ART, UI_TEXT_2);
  ui_text_draw_face_centered_v(UI_FACE_T20, s_label_text_x, UI_TITLE_Y, UI_TAP_MIN, UI_TEXT,
                               g_controller_presets[s_label_preset].name);
  draw_centered(s_chevron_right, s_chevron_right_hit, UI_CHOICE_ARROW_ART, UI_TEXT_2);
}

/**
 * Draw callout @i: its name, arrow and output, the underline (2 px white with a glow when
 * focused, else 1 px) and the leader ending in a dot at the shoulder. Paper cost 6 (7 focused).
 */
static void draw_callout(int i) {
  const Callout *c = &s_callouts[i];
  const bool focused = s_focus == (Shoulder)i && s_footer_focus == FOOT_NONE;
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
  const unsigned int text_color = focused ? UI_TEXT : UI_TEXT_2;
  ui_text_draw_face_centered_v(UI_FACE_T20, text_x, c->visible.y, c->visible.h, text_color,
                               SHOULDER_NAME[i]);
  if (s_arrow) {
    vita2d_draw_texture_tint(
        s_arrow, (float)(text_x + c->arrow_dx),
        (float)(c->visible.y + (c->visible.h - UI_CTRL_ARROW_H) / 2 + UI_CTRL_ARROW_DROP),
        text_color);
  }
  ui_text_draw_face_centered_v(UI_FACE_T20, text_x + c->text_dx, c->visible.y, c->visible.h,
                               text_color, c->text);

  const int line = focused ? UI_LW2 : UI_LW1;
  vita2d_draw_rectangle((float)c->visible.x, (float)(c->visible.y + c->visible.h - line),
                        (float)c->visible.w, (float)line, focused ? UI_TEXT : UI_LEADER);
}

/** Draw the front or rear art in @rect. Paper cost 1. */
static void draw_diagram(ControllerViewMode mode, UiRect rect) {
  s_diagram.mode = mode;
  ui_diagram_render(&s_diagram, rect.x, rect.y, rect.w, rect.h);
}

/** Draw a footer text at the left margin. Paper cost 1. */
static void draw_left_footer(const char *text) {
  ui_text_draw_face_centered_v(UI_FACE_T16, UI_MARGIN_X, UI_CTRL_FOOT_Y, UI_CTRL_FOOT_H, UI_TEXT_2,
                               text);
}

/** Draw Summary page 1: preset switcher, front art, callouts and footers. */
static void draw_buttons_page(void) {
  draw_preset_switcher();
  draw_diagram(CTRL_VIEW_FRONT,
               (UiRect){UI_CTRL_FRONT_X, UI_CTRL_FRONT_Y, UI_CTRL_FRONT_W, UI_CTRL_FRONT_H});
  for (int i = 0; i < SHOULDER_COUNT; i++)
    draw_callout(i);
  draw_left_footer(g_controller_presets[s_label_preset].description);
}

/** Draw Summary page 2: preset switcher, rear art with the read-only zone grid, footers. */
static void draw_back_touch_page(void) {
  draw_preset_switcher();
  draw_diagram(CTRL_VIEW_BACK, ui_controller_zones_diagram_rect(UI_CTRL_VIEW_SUMMARY_REAR));
  ui_controller_zones_draw(UI_CTRL_VIEW_SUMMARY_REAR);
  draw_left_footer(g_controller_presets[s_label_preset].description);
}

/** Draw a zone view: the preset name after the title, the art, the interactive grid and the
 * cursor or selection footer. */
static void draw_zone_view(void) {
  ui_text_draw_face_centered_v(UI_FACE_T16, s_sub_x, UI_TITLE_Y, UI_PAGE_TITLE_H, UI_TEXT_2,
                               g_controller_presets[s_label_preset].name);
  const UiCtrlZoneView view = zone_view();
  draw_diagram(s_page == PAGE_BUTTONS ? CTRL_VIEW_FRONT : CTRL_VIEW_BACK,
               ui_controller_zones_diagram_rect(view));
  ui_controller_zones_set_cursor_visible(view, s_footer_focus == FOOT_NONE);
  ui_controller_zones_draw(view);
  draw_left_footer(s_zone_foot);
}

/** The page title of the current view. */
static const char *page_title(void) {
  if (!s_in_zones)
    return TITLE;
  return s_page == PAGE_BUTTONS ? TITLE_FRONT : TITLE_REAR;
}

/** Draw the page behind the popup layer: frame, top bar, back chevron, the view and the footer
 * buttons (Clear on a Summary page; Whole surface and Clear in a zone view, 4 draws each, 8 when
 * focused). */
static void draw_page(void) {
  apply_footer_focus();
  ui_page_frame_draw(UI_PAGE_ICON_CONTROLLER, page_title());
  ui_top_bar_draw(NULL);
  ui_page_frame_back_draw();
  if (s_in_zones) {
    draw_zone_view();
    ui_text_button_draw(&s_whole_btn);
  } else {
    if (s_page == PAGE_BUTTONS)
      draw_buttons_page();
    else
      draw_back_touch_page();
    ui_text_draw_face_centered_v(UI_FACE_T16, s_page_text_x, UI_CTRL_FOOT_Y, UI_CTRL_FOOT_H,
                                 UI_TEXT_2, s_page_text);
  }
  ui_text_button_draw(&s_clear_btn);
}

/** Fill @out with the hint row of the current view (SPEC 3.8) and return how many. */
static int build_hints(UiHintItem out[UI_HINT_MAX_ITEMS]) {
  if (ui_controller_mapping_is_open())
    return ui_controller_mapping_hints(out);
  int n = 0;
  if (s_in_zones) {
    out[n++] = (UiHintItem){.action = UI_BTN_DPAD, .label = HINT_MOVE};
    out[n++] = (UiHintItem){.action = UI_BTN_CONFIRM, .label = HINT_ASSIGN};
    out[n++] = (UiHintItem){.action = UI_BTN_OPTIONS, .label = HINT_WHOLE};
    out[n++] = (UiHintItem){.action = UI_BTN_CLEAR, .label = HINT_CLEAR};
    out[n++] = (UiHintItem){.action = UI_BTN_CANCEL, .label = HINT_BACK};
    return n;
  }
  out[n++] = (UiHintItem){
      .action = UI_BTN_LEFT | UI_BTN_RIGHT, .label = HINT_PRESET, .low_priority = true};
  out[n++] = (UiHintItem){.action = UI_BTN_L | UI_BTN_R, .label = HINT_PAGE};
  if (s_page == PAGE_BUTTONS) {
    out[n++] = (UiHintItem){.action = UI_BTN_UP | UI_BTN_DOWN, .label = HINT_SHOULDER_PICK};
    out[n++] = (UiHintItem){.action = UI_BTN_CONFIRM, .label = HINT_SHOULDER};
  }
  out[n++] = (UiHintItem){.action = UI_BTN_OPTIONS, .label = HINT_ZONES};
  out[n++] = (UiHintItem){.action = UI_BTN_CLEAR, .label = HINT_CLEAR, .low_priority = true};
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

  /* A tapped hint acts as that button pressed in one frame; the D-pad hints have no action. In a
   * zone view a tapped Confirm is a press and a release, which assigns the cursor's cell. */
  UiInput in = *ui_input_snapshot();
  const uint32_t tapped = ui_hint_row_tap(&s_hints, &in);
  if (tapped) {
    in.pressed |= tapped & ~(uint32_t)UI_BTN_DPAD;
    if (s_in_zones)
      in.released |= tapped & UI_BTN_CONFIRM;
    in.touch.pressed = false;
    in.touch.released = false;
    in.touch.down = false;
  }

  layout_all();

  bool back = false;
  if (ui_controller_mapping_is_open())
    update_popup(&in);
  else if (s_in_zones)
    update_zones(&in);
  else
    back = update_summary(&in);

  /* Input may have changed the view, the preset or an output: lay out again before drawing. */
  layout_all();

  const bool capturing = ui_freeze_is_capturing();
  if (!frozen)
    draw_page();
  if (!capturing)
    ui_controller_mapping_draw();

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
