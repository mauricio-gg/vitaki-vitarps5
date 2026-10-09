/**
 * @file ui_pill.c
 * @brief C19 Pill (SPEC.md C19)
 */

#include "ui/ui_pill.h"

#include <math.h>

#include <vita2d.h>
#include <psp2/kernel/processmgr.h>

#include "ui/ui_component.h"
#include "ui/ui_hint_row.h"
#include "ui/ui_shapes.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"

/** Space the status dot takes before the text of an unstable pill. */
#define DOT_SPACE (UI_PILL_DOT_R * 2 + UI_PILL_DOT_GAP)

/** Opacity of the pulsing pill now: 1 at the start of each period, UI_PILL_PULSE_MIN_PCT at half.
 */
static float pulse_opacity(void) {
  const float min_k = (float)UI_PILL_PULSE_MIN_PCT / 100.0f;
  const uint64_t period_us = (uint64_t)UI_PILL_PULSE_MS * 1000ULL;
  float phase = (float)(sceKernelGetProcessTimeWide() % period_us) / (float)period_us;
  float wave = 0.5f + 0.5f * cosf(phase * 2.0f * (float)M_PI);
  return min_k + (1.0f - min_k) * wave;
}

int ui_pill_width(UiPillKind kind, const char *text) {
  int w = UI_PILL_PAD * 2 + ui_text_face_width(UI_FACE_T16, text);
  return kind == UI_PILL_UNSTABLE ? w + DOT_SPACE : w;
}

void ui_pill_draw(UiPillKind kind, int x, int y, int w, const char *text) {
  const float k = kind == UI_PILL_UNSTABLE ? pulse_opacity() : 1.0f;
  int text_x = x + UI_PILL_PAD;

  ui_shape3_draw(UI_SHAPE3_PILL_32, x, y, w, ui_color_scale_alpha(UI_HUD, k));
  if (kind == UI_PILL_WARN) {
    ui_shape3_draw(UI_SHAPE3_PILL_32_OUTLINE, x, y, w, UI_WARN);
  } else {
    vita2d_draw_fill_circle((float)(text_x + UI_PILL_DOT_R), (float)(y + UI_PILL_H / 2),
                            (float)UI_PILL_DOT_R, ui_layer_color(ui_color_scale_alpha(UI_ERR, k)));
    text_x += DOT_SPACE;
  }
  ui_text_draw_face_centered_v(UI_FACE_T16, text_x, y, UI_PILL_H, ui_color_scale_alpha(UI_TEXT, k),
                               text);
}

/** Width @part takes in the label. */
static int part_width(const UiPillPart *part) {
  if (part->text)
    return ui_text_face_width(UI_FACE_T16, part->text);
  const int glyph_w = ui_hint_row_glyph_width(part->glyph);
  return glyph_w > 0 ? glyph_w + UI_PILL_GLYPH_MARGIN * 2 : 0;
}

int ui_pill_plain_layout(UiPillPart *parts, int count) {
  int w = UI_PILL_PAD * 2;
  for (int i = 0; i < count; i++) {
    parts[i].width = part_width(&parts[i]);
    w += parts[i].width;
  }
  return w;
}

void ui_pill_plain_draw(int x, int y, int w, const UiPillPart *parts, int count) {
  ui_shape3_draw(UI_SHAPE3_PILL_32, x, y, w, UI_HUD);

  int part_x = x + UI_PILL_PAD;
  for (int i = 0; i < count; i++) {
    if (parts[i].text) {
      ui_text_draw_face_centered_v(UI_FACE_T16, part_x, y, UI_PILL_H, UI_TEXT, parts[i].text);
    } else if (parts[i].width > 0) {
      ui_hint_row_glyph_draw(parts[i].glyph, part_x + UI_PILL_GLYPH_MARGIN, y, UI_PILL_H, UI_TEXT);
    }
    part_x += parts[i].width;
  }
}
