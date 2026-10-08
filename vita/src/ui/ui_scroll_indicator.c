/**
 * @file ui_scroll_indicator.c
 * @brief C24 ScrollIndicator (SPEC.md C24)
 */

#include "ui/ui_scroll_indicator.h"

#include <vita2d.h>

#include "ui/ui_theme.h"

void ui_scroll_indicator_draw(int x, int y, int h, int total, int visible, int first) {
  if (visible <= 0 || total <= visible)
    return;
  if (first < 0)
    first = 0;
  if (first > total - visible)
    first = total - visible;

  vita2d_draw_rectangle((float)x, (float)y, (float)UI_SCROLL_W, (float)h, UI_LINE_FAINT);
  vita2d_draw_rectangle((float)x, (float)(y + h * first / total), (float)UI_SCROLL_W,
                        (float)(h * visible / total), UI_TEXT);
}
