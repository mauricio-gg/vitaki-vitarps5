/**
 * @file ui_category_bar.h
 * @brief C01 CategoryBar: the Consoles / Settings / Controller / Profile row (SPEC.md C01)
 *
 * The focused category sits at UI_CAT_X0, scaled up with a glow and a label; the
 * others sit to its left (scaled down) and right at fixed steps, dimmed.
 *
 * A focus change slides every icon to its new place over UI_D2_MS on the ease-out curve while
 * the glow and the new label fade in and the old label fades out. A change during a slide
 * restarts from where each icon is now. visible[] and hit[] always hold the settled layout, so
 * a tap during a slide hits the icon where it will end up.
 *
 * A horizontal swipe that starts on the strip (UI_CAT_STRIP_Y, UI_CAT_STRIP_H, full width) moves
 * one category per UI_CAT_SWIPE_PX of finger travel from touch-down; swiping left is the next
 * category. Focus follows the finger with the same slide, clamped, no wrap.
 */

#pragma once

#include <vita2d.h>

#include "ui/ui_component.h"
#include "ui/ui_theme.h"

typedef struct ui_category_bar_t {
  vita2d_texture *icons[UI_CAT_COUNT];  ///< borrowed; 48 px flat white art
  const char *labels[UI_CAT_COUNT];     ///< borrowed, static strings
  int focus;                            ///< focused category index
  UiRect visible[UI_CAT_COUNT];         ///< drawn icon rect per category, for the current focus
  UiRect hit[UI_CAT_COUNT];             ///< touch rect per category (at least UI_CAT_HIT square)

  /* Slide state. A zero start timestamp means the bar is settled. */
  uint64_t slide_start_us;
  float from_cx[UI_CAT_COUNT];       ///< icon centre x (px) when the slide started
  float from_scale[UI_CAT_COUNT];    ///< icon scale when the slide started
  float from_opacity[UI_CAT_COUNT];  ///< icon opacity (0..1) when the slide started
  float from_label[UI_CAT_COUNT];    ///< label opacity (0..1) when the slide started

  /* Horizontal swipe (SPEC C01). */
  bool swipe_active;  ///< the current touch went down on the strip and may swipe the bar
  int swipe_base;     ///< focus when that touch went down; the swipe moves relative to it
} UiCategoryBar;

/**
 * ui_category_bar_init() - Set the data and compute the rects for focus 0.
 * @bar:    Bar to initialise.
 * @icons:  UI_CAT_COUNT icon textures (a NULL texture draws nothing for that slot).
 * @labels: UI_CAT_COUNT static label strings.
 */
void ui_category_bar_init(UiCategoryBar *bar, vita2d_texture *const icons[UI_CAT_COUNT],
                          const char *const labels[UI_CAT_COUNT]);

/** ui_category_bar_set_focus() - Focus category @index (clamped) at once, without a slide. */
void ui_category_bar_set_focus(UiCategoryBar *bar, int index);

/** ui_category_bar_draw() - Draw glow, icons and labels at their place in the slide. No state
 * change. */
void ui_category_bar_draw(const UiCategoryBar *bar);

/**
 * ui_category_bar_input() - L/R or D-pad Left/Right, a tap and a horizontal swipe on the strip
 * change the category.
 * @return UI_EVENT_MOVED when the focused category changed, otherwise UI_EVENT_NONE.
 */
UiEvent ui_category_bar_input(UiCategoryBar *bar, const UiInput *in);
