/**
 * @file ui_category_bar.h
 * @brief C01 CategoryBar: the Consoles / Settings / Controller / Profile row (SPEC.md C01)
 *
 * The focused category sits at UI_CAT_X0, scaled up with a glow and a label; the
 * others sit to its left (scaled down) and right at fixed steps, dimmed. There is no
 * slide animation and no swipe yet (animation and swipe are separate tickets).
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
} UiCategoryBar;

/**
 * ui_category_bar_init() - Set the data and compute the rects for focus 0.
 * @bar:    Bar to initialise.
 * @icons:  UI_CAT_COUNT icon textures (a NULL texture draws nothing for that slot).
 * @labels: UI_CAT_COUNT static label strings.
 */
void ui_category_bar_init(UiCategoryBar *bar, vita2d_texture *const icons[UI_CAT_COUNT],
                          const char *const labels[UI_CAT_COUNT]);

/** ui_category_bar_draw() - Draw glow, icons and the focused label. No state change. */
void ui_category_bar_draw(const UiCategoryBar *bar);

/**
 * ui_category_bar_input() - L/R or D-pad Left/Right and tap change the category.
 * @return UI_EVENT_MOVED when the focused category changed, otherwise UI_EVENT_NONE.
 */
UiEvent ui_category_bar_input(UiCategoryBar *bar, const UiInput *in);
