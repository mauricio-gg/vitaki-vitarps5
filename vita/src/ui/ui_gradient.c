/**
 * @file ui_gradient.c
 * @brief Horizontal gradient strip (see ui_gradient.h)
 */

#include "ui/ui_gradient.h"

#include <stdbool.h>

#include <vita2d.h>

#include "context.h"
#include "ui/ui_component.h"
#include "ui/ui_theme.h"

#define VERTS_PER_STOP 2

void ui_gradient_draw_h(const UiGradientStop *stops, int count, int y, int h) {
  static bool s_pool_warned = false;
  if (count < 2 || count > UI_GRADIENT_MAX_STOPS)
    return;

  const unsigned int vertex_count = (unsigned int)(count * VERTS_PER_STOP);
  vita2d_color_vertex *v =
      vita2d_pool_memalign(vertex_count * sizeof(vita2d_color_vertex), sizeof(vita2d_color_vertex));
  if (!v) {
    if (!s_pool_warned) {
      LOGE("UI/GRADIENT vita2d pool exhausted, skipping draw");
      s_pool_warned = true;
    }
    return;
  }
  for (int i = 0; i < count; i++) {
    const uint32_t color = ui_layer_color(stops[i].color);
    v[i * VERTS_PER_STOP] = (vita2d_color_vertex){(float)stops[i].x, (float)y, UI_BG_Z, color};
    v[i * VERTS_PER_STOP + 1] =
        (vita2d_color_vertex){(float)stops[i].x, (float)(y + h), UI_BG_Z, color};
  }
  vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, vertex_count);
}
