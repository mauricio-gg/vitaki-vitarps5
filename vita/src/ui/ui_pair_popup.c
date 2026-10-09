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
#include "ui/ui_gesture.h"
#include "ui/ui_internal.h"
#include "ui/ui_list_popup.h"
#include "ui/ui_pair_hosts.h"
#include "ui/ui_popup.h"
#include "ui/ui_scroll_indicator.h"
#include "ui/ui_spinner.h"
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
static const char NOTE_DISCOVERY_OFF[] =
    "Auto Discovery is off. Turn it on in Settings > Network, or enter the IP address.";
static const char ENTER_IP_LABEL[] = "Enter IP address";
static const char RIGHT_LABEL_FORMAT[] = "%s \xC2\xB7 %s";
static const char MODEL_PS5[] = "PS5";
static const char MODEL_PS4[] = "PS4";
static const char HINT_PAIR[] = "Pair";
static const char HINT_ENTER_IP[] = "Enter IP";
static const char HINT_CLOSE[] = "Close";

/** Bytes of the count text, "<N> found". */
#define COUNT_TEXT_MAX 16
/** Microseconds in a millisecond. */
#define US_PER_MS 1000ULL

static UiPopup s_popup;
static vita2d_texture *s_chevron = NULL;

/* Layout, computed once in ui_pair_popup_open() */
static int s_instruction_y;
static int s_label_y;
static UiRect s_viewport;  ///< UI_PAIR_VISIBLE_ROWS rows tall, always
static UiRect s_pinned;    ///< the Enter IP address row
static UiWrapped s_off_note;

/* State */
static UiPairHost s_rows[UI_PAIR_HOSTS_MAX];
static int s_count;
/** Focused console, an index into s_rows; meaningful while s_on_pinned is false. */
static int s_focus;
/** The focus is on the pinned Enter IP address row. */
static bool s_on_pinned;
/** The console the viewport is scrolled around: the last console that had the focus. */
static int s_scroll_ref;
static uint64_t s_open_us;
/** A vertical swipe that began on the viewport: the focus when the touch went down. */
static bool s_swipe_active;
static int s_swipe_base;

void ui_pair_popup_init(void) {
  s_chevron = ui_chevron_bake(UI_CHEVRON_RIGHT, UI_PAIR_CHEVRON_ART, UI_PAIR_CHEVRON_STROKE);
}

/* ============================================================================
 * Rows
 * ============================================================================ */

/** The console the viewport shows first: the reference console kept near the middle. */
static int first_visible(void) {
  int first = s_scroll_ref - UI_PAIR_VISIBLE_ROWS / 2 + 1;
  if (first > s_count - UI_PAIR_VISIBLE_ROWS)
    first = s_count - UI_PAIR_VISIBLE_ROWS;
  return first < 0 ? 0 : first;
}

/** Screen rect of console row @i, which must be on screen. */
static UiRect row_rect(int i) {
  return (UiRect){s_viewport.x, s_viewport.y + (i - first_visible()) * UI_LISTPOP_ROW_H,
                  s_viewport.w, UI_LISTPOP_ROW_H};
}

/** Number of console rows on screen. */
static int visible_count(void) {
  return s_count < UI_PAIR_VISIBLE_ROWS ? s_count : UI_PAIR_VISIBLE_ROWS;
}

/** Put the focus on console @index (clamped); keeps the viewport around it. */
static void focus_console(int index) {
  if (s_count == 0)
    return;
  s_focus = index < 0 ? 0 : (index >= s_count ? s_count - 1 : index);
  s_on_pinned = false;
  s_scroll_ref = s_focus;
}

/**
 * Read the consoles again and keep the focus where it was: on the same console while it is
 * listed, else on the row now in its place; on the pinned row when none is left. A focus on
 * the pinned row stays there.
 */
static void refresh_rows(void) {
  const VitaChiakiHost *focused = (!s_on_pinned && s_focus < s_count) ? s_rows[s_focus].host : NULL;
  const int previous = s_focus;
  s_count = ui_pair_hosts_collect(s_rows);

  if (s_on_pinned)
    return;
  if (s_count == 0) {
    s_on_pinned = true;
    return;
  }
  for (int i = 0; i < s_count; i++) {
    if (s_rows[i].host == focused) {
      focus_console(i);
      return;
    }
  }
  focus_console(previous);
}

/** What the viewport says now. */
static UiPairPhase pair_phase(void) {
  const uint64_t elapsed_ms = (sceKernelGetProcessTimeWide() - s_open_us) / US_PER_MS;
  return ui_console_rows_pair_phase(context.discovery_enabled, s_count, (uint32_t)elapsed_ms);
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
  s_focus = 0;
  s_scroll_ref = 0;
  s_on_pinned = false;
  s_count = ui_pair_hosts_collect(s_rows);
  if (s_count == 0)
    s_on_pinned = true;
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

/** Move the focus one row with the D-pad: consoles, then the pinned row; no wrap. */
static void move_focus(const UiInput *in) {
  if (in->repeat & UI_BTN_UP) {
    if (s_on_pinned && s_count > 0)
      focus_console(s_count - 1);
    else if (!s_on_pinned && s_focus > 0)
      focus_console(s_focus - 1);
  } else if (in->repeat & UI_BTN_DOWN) {
    if (!s_on_pinned && s_focus < s_count - 1)
      focus_console(s_focus + 1);
    else if (!s_on_pinned)
      s_on_pinned = true;
  }
}

/** Follow a vertical swipe that began on the viewport: one row per UI_ROW_SWIPE_PX from
 * touch-down, finger up = next row. */
static void follow_swipe(const UiTouch *touch) {
  if (touch->pressed) {
    s_swipe_active = s_count > 0 && ui_rect_contains(s_viewport, touch->x, touch->y);
    s_swipe_base = s_on_pinned ? s_scroll_ref : s_focus;
  }
  if (!s_swipe_active)
    return;
  if (!touch->down) {
    s_swipe_active = false;
    return;
  }
  if (touch->dragged)
    focus_console(s_swipe_base + ui_gesture_swipe_steps(-touch->dy, UI_ROW_SWIPE_PX));
}

/** The console row under a tap at (@x, @y), or -1. */
static int row_at(float x, float y) {
  const int first = first_visible();
  for (int i = first; i < first + visible_count(); i++) {
    if (ui_rect_contains(row_rect(i), x, y))
      return i;
  }
  return -1;
}

UiPairPopupResult ui_pair_popup_input(const UiInput *in, VitaChiakiHost **chosen) {
  if (!ui_popup_is_open(&s_popup))
    return UI_PAIR_POPUP_NONE;
  if (ui_popup_input(&s_popup, in) == UI_EVENT_CANCELLED)
    return UI_PAIR_POPUP_CLOSE;

  refresh_rows();
  follow_swipe(&in->touch);
  move_focus(in);

  if (in->pressed & UI_BTN_CONFIRM) {
    if (s_on_pinned)
      return UI_PAIR_POPUP_ENTER_IP;
    *chosen = s_rows[s_focus].host;
    return UI_PAIR_POPUP_PAIR;
  }
  if (ui_touch_tap(in)) {
    if (ui_rect_contains(s_pinned, in->touch.x, in->touch.y)) {
      s_on_pinned = true;
      return UI_PAIR_POPUP_ENTER_IP;
    }
    const int row = row_at(in->touch.x, in->touch.y);
    if (row >= 0) {
      focus_console(row);
      *chosen = s_rows[row].host;
      return UI_PAIR_POPUP_PAIR;
    }
  }
  return UI_PAIR_POPUP_NONE;
}

/* ============================================================================
 * Draw
 * ============================================================================ */

/** Draw @text in T16 TEXT_3 vertically centred in the line starting at (@x, @y). */
static void draw_quiet(int x, int y, const char *text) {
  ui_text_draw_face_centered_v(UI_FACE_T16, x, y, UI_T16_LINE, UI_TEXT_3, text);
}

/** Draw the section label with its spinner and the found count. */
static void draw_section_label(int dy, UiPairPhase phase) {
  const int x = s_viewport.x;
  const int y = s_label_y + dy;
  draw_quiet(x, y, SECTION_LABEL);
  if (s_count == 0)
    return;

  if (phase == UI_PAIR_PHASE_FOUND) {
    const int spinner_cx = x + ui_text_face_width(UI_FACE_T16, SECTION_LABEL) +
                           UI_PAIR_LABEL_SPINNER_GAP + UI_SPINNER_SMALL / 2;
    ui_spinner_draw(UI_SPINNER_INLINE, spinner_cx, y + UI_T16_LINE / 2);
  }
  char count[COUNT_TEXT_MAX];
  snprintf(count, sizeof(count), COUNT_FORMAT, s_count);
  draw_quiet(x + s_viewport.w - ui_text_face_width(UI_FACE_T16, count), y, count);
}

/** Draw the note that stands in for the rows when there are none, with its divider. */
static void draw_note(int dy, UiPairPhase phase) {
  const int x = s_viewport.x;
  const int w = s_viewport.w;
  int lines = 1;
  int y = s_viewport.y + dy + UI_PAIR_NOTE_PAD;

  if (phase == UI_PAIR_PHASE_OFF) {
    lines = s_off_note.count;
    for (int i = 0; i < lines; i++) {
      draw_quiet(x + (w - ui_text_face_width(UI_FACE_T16, s_off_note.lines[i])) / 2,
                 y + i * UI_T16_LINE, s_off_note.lines[i]);
    }
  } else if (phase == UI_PAIR_PHASE_SEARCHING) {
    ui_spinner_draw(UI_SPINNER_INLINE, x + UI_PAIR_NOTE_SPINNER_CX, y + UI_T16_LINE / 2);
    draw_quiet(x + UI_PAIR_NOTE_TEXT_X, y, NOTE_SEARCHING);
  } else {
    draw_quiet(x + UI_LISTPOP_ROW_PAD, y, NOTE_NONE);
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
  ui_list_popup_draw_row(&row, !s_on_pinned && i == s_focus, r);
}

/** Draw the pinned Enter IP address row: a rule above it, the row, a chevron at its right. */
static void draw_pinned_row(int dy) {
  UiRect r = s_pinned;
  r.y += dy;
  vita2d_draw_rectangle((float)r.x, (float)(r.y - UI_LW1), (float)r.w, (float)UI_LW1,
                        ui_layer_color(UI_LINE_FAINT));
  UiListRow row = {0};
  snprintf(row.label, sizeof(row.label), "%s", ENTER_IP_LABEL);
  ui_list_popup_draw_row(&row, s_on_pinned, r);
  if (s_chevron) {
    vita2d_draw_texture_tint(s_chevron,
                             (float)(r.x + r.w - UI_LISTPOP_ROW_PAD - UI_PAIR_CHEVRON_ART),
                             (float)(r.y + (r.h - UI_PAIR_CHEVRON_ART) / 2),
                             ui_layer_color(s_on_pinned ? UI_TEXT : UI_TEXT_2));
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
  if (s_count == 0) {
    draw_note(dy, phase);
  } else {
    const int first = first_visible();
    for (int i = first; i < first + visible_count(); i++) {
      UiRect r = row_rect(i);
      r.y += dy;
      draw_console_row(i, r);
    }
    ui_scroll_indicator_draw(s_viewport.x + s_viewport.w - UI_SCROLL_W, s_viewport.y + dy,
                             s_viewport.h, s_count, UI_PAIR_VISIBLE_ROWS, first);
  }
  draw_pinned_row(dy);
  ui_layer_set_alpha(1.0f);
}

int ui_pair_popup_hints(UiHintItem out[UI_HINT_MAX_ITEMS]) {
  out[0] = (UiHintItem){.action = UI_BTN_CONFIRM, .label = s_on_pinned ? HINT_ENTER_IP : HINT_PAIR};
  out[1] = (UiHintItem){.action = UI_BTN_CANCEL, .label = HINT_CLOSE};
  return 2;
}
