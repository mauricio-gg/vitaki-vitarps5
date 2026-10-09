/**
 * @file ui_pair_popup.c
 * @brief C29 PairPopup (see ui_pair_popup.h)
 */

#include "ui/ui_pair_popup.h"

#include <stdio.h>
#include <string.h>

#include <vita2d.h>
#include <psp2/kernel/processmgr.h>

#include "context.h"
#include "ui/ui_chevron.h"
#include "ui/ui_chrome_layout.h"
#include "ui/ui_console_rows.h"
#include "ui/ui_filter_keyboard.h"
#include "ui/ui_gesture.h"
#include "ui/ui_internal.h"
#include "ui/ui_list_popup.h"
#include "ui/ui_pair_hosts.h"
#include "ui/ui_popup.h"
#include "ui/ui_scroll_indicator.h"
#include "ui/ui_spinner.h"
#include "ui/ui_text_button.h"
#include "ui/ui_text.h"
#include "ui/ui_text_wrap.h"
#include "ui/ui_theme.h"

/* Copy (SPEC section 5 and C29) */
static const char TITLE[] = "Pair new device";
static const char INSTRUCTION_PS5[] = "PS5: Settings > System > Remote Play > Link Device";
static const char INSTRUCTION_PS4[] = "PS4: Remote Play Connection Settings > Add Device";
static const char SECTION_LABEL[] = "Found on your network";
static const char COUNT_FORMAT[] = "%d found";
static const char NOTE_SEARCHING[] = "Searching your network...";
static const char NOTE_NONE[] = "No unpaired consoles found";
static const char NOTE_NO_MATCH[] = "No consoles match filter";
static const char NOTE_DISCOVERY_OFF[] =
    "Auto Discovery is off. Turn it on in Settings > Network, or enter the IP address.";
static const char ENTER_IP_LABEL[] = "Enter IP address";
static const char FILTER_IDLE[] = "Filter...";
static const char FILTER_NAME_FORMAT[] = "Filter: \"%s\"";
static const char FILTER_COUNT_FORMAT[] = "%d of %d";
static const char FILTER_KEYBOARD_TITLE[] = "Filter Found Consoles";
static const char CLEAR_LABEL[] = "Clear";
static const char RIGHT_LABEL_FORMAT[] = "%s \xC2\xB7 %s";
static const char MODEL_PS5[] = "PS5";
static const char MODEL_PS4[] = "PS4";
static const char HINT_PAIR[] = "Pair";
static const char HINT_ENTER_IP[] = "Enter IP";
static const char HINT_FILTER[] = "Filter";
static const char HINT_CLEAR[] = "Clear";
static const char HINT_PAGE[] = "Page";
static const char HINT_CLOSE[] = "Close";

/** The magnifier of Home's Filter row. */
#define SEARCH_ICON_PATH "app0:/assets/icons/search.png"
/** Bytes of the count text, "<N> found" or "<N> of <M>". */
#define COUNT_TEXT_MAX 24
/** Bytes of the Filter row's label: "Filter: " and the quoted text. */
#define FILTER_LABEL_MAX (UI_FILTER_TEXT_MAX + 16)
/** Microseconds in a millisecond. */
#define US_PER_MS 1000ULL

static UiPopup s_popup;
static vita2d_texture *s_chevron = NULL;
static vita2d_texture *s_search_icon = NULL;

/* Layout, computed once in ui_pair_popup_open() */
static int s_instruction_y;
static int s_label_y;
static UiRect s_viewport;  ///< UI_PAIR_VISIBLE_ROWS rows tall, always
static UiRect s_pinned;    ///< the Enter IP address row
static UiWrapped s_off_note;

/* State. The list is the Filter row when shown (s_lead is 1), then the consoles that match the
 * filter; a focus is a row of it, or UI_PAIR_FOCUS_PINNED for the Enter IP address row. */
static UiPairHost s_rows[UI_PAIR_HOSTS_MAX];
/** Consoles in s_rows: the unpaired ones found that match the filter. */
static int s_count;
/** Unpaired consoles found, before the filter. */
static int s_total;
/** Rows before the first console: 1 while the Filter row is shown, else 0. */
static int s_lead;
static int s_focus;
/** The row the viewport is scrolled around: the last row that had the focus. */
static int s_scroll_ref;
/** The popup's own filter text; empty means no filter. Home's filter is not touched. */
static char s_filter[UI_FILTER_TEXT_MAX];
static uint64_t s_open_us;
/** A vertical swipe that began on the viewport: the focus when the touch went down. */
static bool s_swipe_active;
static int s_swipe_base;

void ui_pair_popup_init(void) {
  s_chevron = ui_chevron_bake(UI_CHEVRON_RIGHT, UI_PAIR_CHEVRON_ART, UI_PAIR_CHEVRON_STROKE);
  s_search_icon = ui_load_png_linear(SEARCH_ICON_PATH);
}

/* ============================================================================
 * Rows
 * ============================================================================ */

/** Rows of the list: the Filter row when shown, then the matching consoles. */
static int list_rows(void) {
  return s_lead + s_count;
}

static bool filter_active(void) {
  return s_filter[0] != '\0';
}

/** The first row the viewport shows: the reference row kept near the middle. */
static int first_visible(void) {
  int first = s_scroll_ref - UI_PAIR_VISIBLE_ROWS / 2 + 1;
  if (first > list_rows() - UI_PAIR_VISIBLE_ROWS)
    first = list_rows() - UI_PAIR_VISIBLE_ROWS;
  return first < 0 ? 0 : first;
}

/** Screen rect of list row @row, which must be on screen. */
static UiRect row_rect(int row) {
  return (UiRect){s_viewport.x, s_viewport.y + (row - first_visible()) * UI_LISTPOP_ROW_H,
                  s_viewport.w, UI_LISTPOP_ROW_H};
}

/** Number of list rows on screen. */
static int visible_count(void) {
  return list_rows() < UI_PAIR_VISIBLE_ROWS ? list_rows() : UI_PAIR_VISIBLE_ROWS;
}

/** True when list row @row is on screen. */
static bool row_visible(int row) {
  const int first = first_visible();
  return row >= first && row < first + visible_count();
}

/** Put the focus on list row @row (clamped); keeps the viewport around it. */
static void focus_row(int row) {
  if (list_rows() == 0)
    return;
  s_focus = row < 0 ? 0 : (row >= list_rows() ? list_rows() - 1 : row);
  s_scroll_ref = s_focus;
}

/** Read the consoles again: all the unpaired ones found, then those the filter keeps. */
static void collect_rows(void) {
  s_total = ui_pair_hosts_collect(s_rows);
  s_count = s_total;
  if (filter_active()) {
    s_count = 0;
    for (int i = 0; i < s_total; i++) {
      if (ui_console_matches_filter(s_rows[i].name, s_rows[i].ip, s_filter))
        s_rows[s_count++] = s_rows[i];
    }
  }
  s_lead = ui_console_rows_has_filter(s_total, filter_active()) ? 1 : 0;
}

/**
 * Read the consoles again and keep the focus where it was: on the same console while it is
 * listed, on the Filter row while that is shown, else on the row now in a gone console's place
 * (the Filter row when none is left, or the pinned row when there is no Filter row either). A
 * focus on the pinned row stays there.
 */
static void refresh_rows(void) {
  const int previous_lead = s_lead;
  const int previous = s_focus;
  const bool on_console = previous != UI_PAIR_FOCUS_PINNED && previous >= previous_lead &&
                          previous - previous_lead < s_count;
  const VitaChiakiHost *focused = on_console ? s_rows[previous - previous_lead].host : NULL;
  collect_rows();

  if (previous == UI_PAIR_FOCUS_PINNED)
    return;
  for (int i = 0; focused && i < s_count; i++) {
    if (s_rows[i].host == focused) {
      focus_row(i + s_lead);
      return;
    }
  }
  if (previous < previous_lead && s_lead > 0) {
    focus_row(0);
  } else if (s_count > 0) {
    focus_row(previous < previous_lead ? s_lead : previous - previous_lead + s_lead);
  } else if (s_lead > 0) {
    focus_row(0);
  } else {
    s_focus = UI_PAIR_FOCUS_PINNED;
  }
}

/** What the viewport says now. */
static UiPairPhase pair_phase(void) {
  const uint64_t elapsed_ms = (sceKernelGetProcessTimeWide() - s_open_us) / US_PER_MS;
  return ui_console_rows_pair_phase(context.discovery_enabled, s_total, (uint32_t)elapsed_ms);
}

/* ============================================================================
 * Open and close
 * ============================================================================ */

static int measure_t16(const char *s, void *ctx) {
  (void)ctx;
  return ui_text_face_width(UI_FACE_T16, s);
}

static int measure_t20(const char *s, void *ctx) {
  (void)ctx;
  return ui_text_face_width(UI_FACE_T20, s);
}

void ui_pair_popup_open(void) {
  ui_popup_open(&s_popup, &(UiPopupSpec){
                              .size = UI_POPUP_SIZE_L,
                              .title = TITLE,
                              .cancel_button = -1,
                          });

  const UiRect content = s_popup.content;
  s_instruction_y = content.y + UI_PAIR_GAP;
  s_label_y = s_instruction_y + UI_PAIR_INSTR_LINES * UI_T16_LINE + UI_PAIR_GAP;
  s_viewport = (UiRect){content.x, s_label_y + UI_T16_LINE + UI_PAIR_GAP, content.w,
                        UI_PAIR_VISIBLE_ROWS * UI_LISTPOP_ROW_H};
  s_pinned = (UiRect){content.x, s_viewport.y + s_viewport.h, content.w, UI_LISTPOP_ROW_H};
  ui_text_wrap(NOTE_DISCOVERY_OFF, content.w, measure_t16, NULL, &s_off_note);

  s_open_us = sceKernelGetProcessTimeWide();
  s_swipe_active = false;
  s_filter[0] = '\0';
  collect_rows();
  s_scroll_ref = 0;
  s_focus = s_count > 0 ? s_lead : UI_PAIR_FOCUS_PINNED;
}

void ui_pair_popup_close(void) {
  ui_popup_close(&s_popup);
}

bool ui_pair_popup_is_open(void) {
  return ui_popup_is_open(&s_popup);
}

/* ============================================================================
 * Input
 * ============================================================================ */

/** Move the focus one row with the D-pad: the Filter row, the consoles, then the pinned row; no
 * wrap. */
static void move_focus(const UiInput *in) {
  if (in->repeat & UI_BTN_UP) {
    if (s_focus == UI_PAIR_FOCUS_PINNED && list_rows() > 0)
      focus_row(list_rows() - 1);
    else if (s_focus > 0)
      focus_row(s_focus - 1);
  } else if (in->repeat & UI_BTN_DOWN) {
    if (s_focus == UI_PAIR_FOCUS_PINNED)
      return;
    if (s_focus < list_rows() - 1)
      focus_row(s_focus + 1);
    else
      s_focus = UI_PAIR_FOCUS_PINNED;
  }
}

/** L and R move the focus one page (UI_PAIR_VISIBLE_ROWS rows) up or down, and the viewport with
 * it. */
static void page_focus(const UiInput *in) {
  const int direction = (in->pressed & UI_BTN_L) ? -1 : ((in->pressed & UI_BTN_R) ? 1 : 0);
  if (direction == 0)
    return;
  const int target =
      ui_pair_page_focus(s_focus, direction, s_lead, list_rows(), UI_PAIR_VISIBLE_ROWS);
  if (target == UI_PAIR_FOCUS_PINNED)
    s_focus = UI_PAIR_FOCUS_PINNED;
  else
    focus_row(target);
}

/** Follow a vertical swipe that began on the viewport: one row per UI_ROW_SWIPE_PX from
 * touch-down, finger up = next row. The Filter row is part of the swipe; the pinned row is not. */
static void follow_swipe(const UiTouch *touch) {
  if (touch->pressed) {
    s_swipe_active = list_rows() > 0 && ui_rect_contains(s_viewport, touch->x, touch->y);
    s_swipe_base = s_focus == UI_PAIR_FOCUS_PINNED ? s_scroll_ref : s_focus;
  }
  if (!s_swipe_active)
    return;
  if (!touch->down) {
    s_swipe_active = false;
    return;
  }
  if (touch->dragged)
    focus_row(s_swipe_base + ui_gesture_swipe_steps(-touch->dy, UI_ROW_SWIPE_PX));
}

/** The list row under a tap at (@x, @y), or -1. */
static int row_at(float x, float y) {
  const int first = first_visible();
  for (int i = first; i < first + visible_count(); i++) {
    if (ui_rect_contains(row_rect(i), x, y))
      return i;
  }
  return -1;
}

/** The small Clear button at the right end of the Filter row drawn in @row. */
static UiTextButton clear_button(UiRect row) {
  UiTextButton btn;
  ui_text_button_init_small(&btn, CLEAR_LABEL, 0, 0);
  btn.visible.x = row.x + row.w - UI_LISTPOP_ROW_PAD - btn.visible.w;
  btn.visible.y = row.y + (row.h - btn.visible.h) / 2;
  btn.hit = ui_rect_hit_from_visible(btn.visible, UI_TAP_MIN, UI_TAP_MIN);
  return btn;
}

/** Drop the filter text: every console is listed again. */
static void clear_filter(void) {
  s_filter[0] = '\0';
  refresh_rows();
}

/** Open the system keyboard on the filter text. */
static void edit_filter(void) {
  ui_filter_keyboard_open(FILTER_KEYBOARD_TITLE, s_filter);
}

/**
 * Collect the keyboard once it has finished: Done applies the typed text (empty clears the
 * filter), Cancel leaves it. @return true while the keyboard is up or finished this frame, when
 * the popup takes no other input (the press that closed it must not reach the list).
 */
static bool poll_keyboard(void) {
  char typed[UI_FILTER_TEXT_MAX];
  const UiFilterKeyboardResult result = ui_filter_keyboard_poll(typed, sizeof(typed));
  if (result == UI_FILTER_KB_IDLE)
    return false;
  if (result == UI_FILTER_KB_DONE) {
    snprintf(s_filter, sizeof(s_filter), "%s", typed);
    refresh_rows();
  }
  return true;
}

/** Start and Square on the Filter row: Start opens the keyboard, or clears an active filter, and
 * does nothing without the Filter row; Square clears an active filter from the Filter row. */
static void filter_shortcuts(const UiInput *in) {
  if ((in->pressed & UI_BTN_FILTER) && s_lead > 0) {
    if (filter_active())
      clear_filter();
    else
      edit_filter();
  }
  if ((in->pressed & UI_BTN_CLEAR) && s_lead > 0 && s_focus == 0 && filter_active())
    clear_filter();
}

/** Confirm on the focused row. @return what the owner has to do. */
static UiPairPopupResult confirm_focus(VitaChiakiHost **chosen) {
  if (s_focus == UI_PAIR_FOCUS_PINNED)
    return UI_PAIR_POPUP_ENTER_IP;
  if (s_focus < s_lead) {
    edit_filter();
    return UI_PAIR_POPUP_NONE;
  }
  *chosen = s_rows[s_focus - s_lead].host;
  return UI_PAIR_POPUP_PAIR;
}

/** A tap at the touch point: Enter IP address, Clear, the Filter row or a console. */
static UiPairPopupResult tap(const UiInput *in, VitaChiakiHost **chosen) {
  if (ui_rect_contains(s_pinned, in->touch.x, in->touch.y)) {
    s_focus = UI_PAIR_FOCUS_PINNED;
    return UI_PAIR_POPUP_ENTER_IP;
  }
  if (s_lead > 0 && filter_active() && row_visible(0)) {
    UiTextButton clear = clear_button(row_rect(0));
    if (ui_text_button_input(&clear, in) == UI_EVENT_ACTIVATED) {
      clear_filter();
      return UI_PAIR_POPUP_NONE;
    }
  }
  const int row = row_at(in->touch.x, in->touch.y);
  if (row < 0)
    return UI_PAIR_POPUP_NONE;
  focus_row(row);
  return confirm_focus(chosen);
}

UiPairPopupResult ui_pair_popup_input(const UiInput *in, VitaChiakiHost **chosen) {
  if (!ui_popup_is_open(&s_popup))
    return UI_PAIR_POPUP_NONE;
  if (poll_keyboard())
    return UI_PAIR_POPUP_NONE;
  if (ui_popup_input(&s_popup, in) == UI_EVENT_CANCELLED)
    return UI_PAIR_POPUP_CLOSE;

  refresh_rows();
  follow_swipe(&in->touch);
  move_focus(in);
  page_focus(in);
  filter_shortcuts(in);

  if (in->pressed & UI_BTN_CONFIRM)
    return confirm_focus(chosen);
  if (ui_touch_tap(in))
    return tap(in, chosen);
  return UI_PAIR_POPUP_NONE;
}

/* ============================================================================
 * Draw
 * ============================================================================ */

/** Draw @text in T16 TEXT_3 vertically centred in the line starting at (@x, @y). */
static void draw_quiet(int x, int y, const char *text) {
  ui_text_draw_face_centered_v(UI_FACE_T16, x, y, UI_T16_LINE, UI_TEXT_3, text);
}

/** Draw the section label with its spinner and the found count (with a filter the Filter row
 * carries the count). */
static void draw_section_label(int dy, UiPairPhase phase) {
  const int x = s_viewport.x;
  const int y = s_label_y + dy;
  draw_quiet(x, y, SECTION_LABEL);
  if (s_total == 0)
    return;

  if (phase == UI_PAIR_PHASE_FOUND) {
    const int spinner_cx = x + ui_text_face_width(UI_FACE_T16, SECTION_LABEL) +
                           UI_PAIR_LABEL_SPINNER_GAP + UI_SPINNER_SMALL / 2;
    ui_spinner_draw(UI_SPINNER_INLINE, spinner_cx, y + UI_T16_LINE / 2);
  }
  if (filter_active())
    return;
  char count[COUNT_TEXT_MAX];
  snprintf(count, sizeof(count), COUNT_FORMAT, s_total);
  draw_quiet(x + s_viewport.w - ui_text_face_width(UI_FACE_T16, count), y, count);
}

/** Draw the note that stands in for the console rows when there are none, with its divider: under
 * the Filter row when it is shown (the filter keeps nothing), else at the top of the list. */
static void draw_note(int dy, UiPairPhase phase) {
  const int x = s_viewport.x;
  const int w = s_viewport.w;
  const bool filtered = s_lead > 0;
  int lines = 1;
  int y = s_viewport.y + dy + s_lead * UI_LISTPOP_ROW_H + UI_PAIR_NOTE_PAD;

  if (!filtered && phase == UI_PAIR_PHASE_OFF) {
    lines = s_off_note.count;
    for (int i = 0; i < lines; i++) {
      draw_quiet(x + (w - ui_text_face_width(UI_FACE_T16, s_off_note.lines[i])) / 2,
                 y + i * UI_T16_LINE, s_off_note.lines[i]);
    }
  } else if (!filtered && phase == UI_PAIR_PHASE_SEARCHING) {
    ui_spinner_draw(UI_SPINNER_INLINE, x + UI_PAIR_NOTE_SPINNER_CX, y + UI_T16_LINE / 2);
    draw_quiet(x + UI_PAIR_NOTE_TEXT_X, y, NOTE_SEARCHING);
  } else {
    draw_quiet(x + UI_LISTPOP_ROW_PAD, y, filtered ? NOTE_NO_MATCH : NOTE_NONE);
  }
  vita2d_draw_rectangle((float)x, (float)(y + lines * UI_T16_LINE + UI_PAIR_NOTE_PAD - UI_LW1),
                        (float)w, (float)UI_LW1, ui_layer_color(UI_LINE_FAINT));
}

/** Draw console row @i at @r: its name, cut to leave room for the model and address. */
static void draw_console_row(int i, UiRect r) {
  UiListRow row = {0};
  snprintf(row.right_label, sizeof(row.right_label), RIGHT_LABEL_FORMAT,
           s_rows[i].ps5 ? MODEL_PS5 : MODEL_PS4, s_rows[i].ip);
  const int room = r.w - 2 * UI_LISTPOP_ROW_PAD - UI_PAIR_LABEL_GAP -
                   ui_text_face_width(UI_FACE_T16, row.right_label);
  ui_ellipsize_to_fit(s_rows[i].name, room, measure_t20, NULL, row.label, sizeof(row.label));
  ui_list_popup_draw_row(&row, s_focus == i + s_lead, r);
}

/**
 * Draw the Filter row at @r: the magnifier and "Filter..." idle; with a filter the quoted text,
 * "<N> of <M>" and the small Clear button at the right. The label is cut to leave room for them.
 */
static void draw_filter_row(UiRect r) {
  const bool focused = s_focus == 0;
  const int inner_w = r.w - 2 * UI_LISTPOP_ROW_PAD - UI_PAIR_FILTER_LABEL_INSET;
  UiListRow row = {0};
  char count[COUNT_TEXT_MAX] = "";
  UiTextButton clear = {0};

  if (filter_active()) {
    char label[FILTER_LABEL_MAX];
    clear = clear_button(r);
    snprintf(count, sizeof(count), FILTER_COUNT_FORMAT, s_count, s_total);
    snprintf(label, sizeof(label), FILTER_NAME_FORMAT, s_filter);
    const int room =
        inner_w - clear.visible.w - ui_text_face_width(UI_FACE_T16, count) - 2 * UI_PAIR_LABEL_GAP;
    ui_ellipsize_to_fit(label, room, measure_t20, NULL, row.label, sizeof(row.label));
  } else {
    snprintf(row.label, sizeof(row.label), "%s", FILTER_IDLE);
  }
  ui_list_popup_draw_row_inset(&row, focused, r, UI_PAIR_FILTER_LABEL_INSET);

  if (s_search_icon) {
    const float scale = (float)UI_PAIR_FILTER_ICON / (float)vita2d_texture_get_width(s_search_icon);
    vita2d_draw_texture_tint_scale(s_search_icon, (float)(r.x + UI_LISTPOP_ROW_PAD),
                                   (float)(r.y + (r.h - UI_PAIR_FILTER_ICON) / 2), scale, scale,
                                   ui_layer_color(focused ? UI_TEXT : UI_TEXT_2));
  }
  if (count[0]) {
    draw_quiet(clear.visible.x - UI_PAIR_LABEL_GAP - ui_text_face_width(UI_FACE_T16, count),
               r.y + (r.h - UI_T16_LINE) / 2, count);
    ui_text_button_draw(&clear);
  }
}

/** Draw the pinned Enter IP address row: a rule above it, the row, a chevron at its right. */
static void draw_pinned_row(int dy) {
  UiRect r = s_pinned;
  r.y += dy;
  vita2d_draw_rectangle((float)r.x, (float)(r.y - UI_LW1), (float)r.w, (float)UI_LW1,
                        ui_layer_color(UI_LINE_FAINT));
  UiListRow row = {0};
  snprintf(row.label, sizeof(row.label), "%s", ENTER_IP_LABEL);
  ui_list_popup_draw_row(&row, s_focus == UI_PAIR_FOCUS_PINNED, r);
  if (s_chevron) {
    vita2d_draw_texture_tint(s_chevron,
                             (float)(r.x + r.w - UI_LISTPOP_ROW_PAD - UI_PAIR_CHEVRON_ART),
                             (float)(r.y + (r.h - UI_PAIR_CHEVRON_ART) / 2),
                             ui_layer_color(s_focus == UI_PAIR_FOCUS_PINNED ? UI_TEXT : UI_TEXT_2));
  }
}

void ui_pair_popup_draw(void) {
  if (!ui_popup_is_open(&s_popup) || s_popup.enter_start_us == 0)
    return;
  ui_popup_draw(&s_popup);

  int dy;
  float k;
  ui_popup_enter(&s_popup, &dy, &k);
  ui_layer_set_alpha(k);

  ui_text_draw_face_centered_v(UI_FACE_T16, s_viewport.x, s_instruction_y + dy, UI_T16_LINE,
                               UI_TEXT_2, INSTRUCTION_PS5);
  ui_text_draw_face_centered_v(UI_FACE_T16, s_viewport.x, s_instruction_y + UI_T16_LINE + dy,
                               UI_T16_LINE, UI_TEXT_2, INSTRUCTION_PS4);

  const UiPairPhase phase = pair_phase();
  draw_section_label(dy, phase);
  const int first = first_visible();
  for (int i = first; i < first + visible_count(); i++) {
    UiRect r = row_rect(i);
    r.y += dy;
    if (i < s_lead)
      draw_filter_row(r);
    else
      draw_console_row(i - s_lead, r);
  }
  if (s_count == 0)
    draw_note(dy, phase);
  ui_scroll_indicator_draw(s_viewport.x + s_viewport.w - UI_SCROLL_W, s_viewport.y + dy,
                           s_viewport.h, list_rows(), UI_PAIR_VISIBLE_ROWS, first);
  draw_pinned_row(dy);
  ui_layer_set_alpha(1.0f);
}

int ui_pair_popup_hints(UiHintItem out[UI_HINT_MAX_ITEMS]) {
  int n = 0;
  if (s_focus == UI_PAIR_FOCUS_PINNED) {
    out[n++] = (UiHintItem){.action = UI_BTN_CONFIRM, .label = HINT_ENTER_IP};
  } else if (s_focus < s_lead) {
    out[n++] = (UiHintItem){.action = UI_BTN_CONFIRM, .label = HINT_FILTER};
    if (filter_active())
      out[n++] = (UiHintItem){.action = UI_BTN_CLEAR, .label = HINT_CLEAR};
  } else {
    out[n++] = (UiHintItem){.action = UI_BTN_CONFIRM, .label = HINT_PAIR};
  }
  if (list_rows() > UI_PAIR_VISIBLE_ROWS) {
    out[n++] =
        (UiHintItem){.action = UI_BTN_L | UI_BTN_R, .label = HINT_PAGE, .low_priority = true};
  }
  out[n++] = (UiHintItem){.action = UI_BTN_CANCEL, .label = HINT_CLOSE};
  return n;
}
