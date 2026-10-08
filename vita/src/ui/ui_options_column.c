/**
 * @file ui_options_column.c
 * @brief C05 OptionsColumn (SPEC.md C05)
 */

#include "ui/ui_options_column.h"

#include <string.h>

#include <vita2d.h>

#include "ui/ui_animation.h"
#include "ui/ui_chrome_layout.h"
#include "ui/ui_constants.h"
#include "ui/ui_gradient.h"
#include "ui/ui_motion.h"
#include "ui/ui_shapes.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"

static const char SUBTITLE[] = "Options";

_Static_assert(UI_OPTS_BAR_INSET + UI_OPTS_LABEL_PAD == UI_OPTS_PAD,
               "the label sits inside the focus bar at the column's padding");

static int measure_t28(const char *s, void *ctx) {
  (void)ctx;
  return ui_text_face_width(UI_FACE_T28, s);
}

/** How far in the column is, 0 (off screen) to 1 (settled). */
static float slide_progress(const UiOptionsColumn *col) {
  const float target = col->open ? 1.0f : 0.0f;
  if (col->slide_start_us == 0)
    return target;
  const float t =
      ui_motion_progress(ui_anim_elapsed_ms(col->slide_start_us), 0.0f, (float)UI_OPTS_SLIDE_MS);
  return col->slide_from + (target - col->slide_from) * UI_EASE_OUT(t);
}

void ui_options_column_init(UiOptionsColumn *col) {
  memset(col, 0, sizeof(*col));
  for (int i = 0; i < UI_OPTS_MAX_ROWS; i++) {
    const int y = UI_OPTS_ROW_Y + i * UI_OPTS_ROW_H;
    col->row_visible[i] = (UiRect){UI_OPTS_X + UI_OPTS_BAR_INSET, y,
                                   UI_OPTS_W - 2 * UI_OPTS_BAR_INSET, UI_OPTS_ROW_H};
    col->row_hit[i] = (UiRect){UI_OPTS_X, y, UI_OPTS_W, UI_OPTS_ROW_H};
  }
  col->column_hit = (UiRect){UI_OPTS_X, 0, UI_OPTS_W, UI_HINT_Y};
}

void ui_options_column_open(UiOptionsColumn *col) {
  col->slide_from = slide_progress(col);
  col->slide_start_us = ui_anim_now_us();
  col->open = true;
  col->focus = 0;
}

void ui_options_column_close(UiOptionsColumn *col) {
  if (!col->open)
    return;
  col->slide_from = slide_progress(col);
  col->slide_start_us = ui_anim_now_us();
  col->open = false;
}

void ui_options_column_close_now(UiOptionsColumn *col) {
  col->open = false;
  col->slide_start_us = 0;
}

void ui_options_column_set_rows(UiOptionsColumn *col, const char *name, const UiOptionsRow *rows,
                                int count) {
  if (strncmp(col->name_src, name, sizeof(col->name_src) - 1) != 0) {
    strncpy(col->name_src, name, sizeof(col->name_src) - 1);
    col->name_src[sizeof(col->name_src) - 1] = '\0';
    ui_ellipsize_to_fit(col->name_src, UI_OPTS_W - 2 * UI_OPTS_PAD, measure_t28, NULL, col->name,
                        sizeof(col->name));
  }
  col->count = count < UI_OPTS_MAX_ROWS ? count : UI_OPTS_MAX_ROWS;
  memcpy(col->rows, rows, (size_t)col->count * sizeof(rows[0]));
  if (col->focus >= col->count)
    col->focus = col->count > 0 ? col->count - 1 : 0;
}

float ui_options_column_behind_alpha(const UiOptionsColumn *col) {
  const float dim = (float)UI_OPTS_BEHIND_PCT / 100.0f;
  return 1.0f - (1.0f - dim) * slide_progress(col);
}

/** Draw the feathered panel with its left edge at @left. */
static void draw_panel(int left) {
  const int feather = UI_OPTS_W * UI_OPTS_FEATHER_PCT / 100;
  const UiGradientStop stops[] = {
      {left, UI_EDGE_0},
      {left + feather, UI_PANEL_EDGE},
      {left + UI_OPTS_W, UI_PANEL_EDGE},
  };
  ui_gradient_draw_h(stops, (int)(sizeof(stops) / sizeof(stops[0])), 0, VITA_HEIGHT);
}

/** Draw row @i, @dx pixels right of its place. */
static void draw_row(const UiOptionsColumn *col, int i, int dx) {
  const UiOptionsRow *row = &col->rows[i];
  const bool focused = i == col->focus;
  const float k = row->disabled ? (float)UI_OPTS_DISABLED_PCT / 100.0f : 1.0f;
  const UiRect bar = col->row_visible[i];
  const int text_x = UI_OPTS_X + UI_OPTS_PAD + dx;

  if (focused) {
    const UiRect label = {text_x, bar.y + (bar.h - UI_T20_LINE) / 2,
                          ui_text_face_width(UI_FACE_T20, row->label), UI_T20_LINE};
    ui_glow_draw_rect(label, UI_OPTS_GLOW,
                      ui_color_scale_alpha(UI_GLOW, (float)UI_OPTS_GLOW_PCT / 100.0f * k));
    ui_shape3_draw(UI_SHAPE3_BAR_56, bar.x + dx, bar.y, bar.w,
                   ui_color_scale_alpha(UI_FILL_FOCUS, k));
  } else {
    vita2d_draw_rectangle((float)text_x, (float)(bar.y + bar.h - UI_LW1),
                          (float)(UI_OPTS_W - 2 * UI_OPTS_PAD), (float)UI_LW1,
                          ui_layer_color(UI_LINE_FAINT));
  }
  ui_text_draw_face_centered_v(UI_FACE_T20, text_x, bar.y, bar.h,
                               ui_color_scale_alpha(focused ? UI_TEXT : UI_TEXT_2, k), row->label);
}

void ui_options_column_draw(const UiOptionsColumn *col) {
  const float p = slide_progress(col);
  if (p <= 0.0f)
    return;
  const int dx = (int)((1.0f - p) * (float)UI_OPTS_W + 0.5f);
  const int text_x = UI_OPTS_X + UI_OPTS_PAD + dx;

  draw_panel(UI_OPTS_X + dx);
  ui_text_draw_face_centered_v(UI_FACE_T28, text_x, UI_OPTS_TITLE_Y, UI_T28_LINE, UI_TEXT,
                               col->name);
  ui_text_draw_face_centered_v(UI_FACE_T16, text_x, UI_OPTS_SUB_Y, UI_T16_LINE, UI_TEXT_2,
                               SUBTITLE);
  for (int i = 0; i < col->count; i++)
    draw_row(col, i, dx);
}

/** Activate the focused row unless it is disabled. */
static UiEvent activate_focused(UiOptionsColumn *col) {
  if (col->rows[col->focus].disabled)
    return UI_EVENT_NONE;
  col->activated = col->rows[col->focus].id;
  return UI_EVENT_ACTIVATED;
}

UiEvent ui_options_column_input(UiOptionsColumn *col, const UiInput *in) {
  if (!col->open)
    return UI_EVENT_NONE;

  if (in->pressed & (UI_BTN_OPTIONS | UI_BTN_CANCEL)) {
    ui_options_column_close(col);
    return UI_EVENT_CANCELLED;
  }
  if ((in->repeat & UI_BTN_UP) && col->focus > 0) {
    col->focus--;
    return UI_EVENT_MOVED;
  }
  if ((in->repeat & UI_BTN_DOWN) && col->focus < col->count - 1) {
    col->focus++;
    return UI_EVENT_MOVED;
  }
  if (col->count > 0 && (in->pressed & UI_BTN_CONFIRM))
    return activate_focused(col);

  if (ui_touch_tap(in)) {
    for (int i = 0; i < col->count; i++) {
      if (!ui_rect_contains(col->row_hit[i], in->touch.x, in->touch.y))
        continue;
      col->focus = i;
      return col->rows[i].disabled ? UI_EVENT_MOVED : activate_focused(col);
    }
    if (!ui_rect_contains(col->column_hit, in->touch.x, in->touch.y)) {
      ui_options_column_close(col);
      return UI_EVENT_CANCELLED;
    }
  }
  return UI_EVENT_NONE;
}
