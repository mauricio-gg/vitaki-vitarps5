/**
 * @file ui_zone_grid.c
 * @brief C21 ZoneGrid (SPEC.md C21)
 */

#include "ui/ui_zone_grid.h"

#include <string.h>
#include <vita2d.h>

#include "context.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"

/** Opaque white tint: the baked cell textures already carry their own colours and alpha. */
#define TEX_TINT 0xFFFFFFFFu

/** One straight (non-premultiplied) colour, channels 0..1. */
typedef struct {
  float r, g, b, a;
} Rgba;

/** unpack() - Split a packed ABGR theme colour into channels. */
static Rgba unpack(uint32_t c) {
  return (Rgba){(float)(c & 0xFF) / 255.0f, (float)((c >> 8) & 0xFF) / 255.0f,
                (float)((c >> 16) & 0xFF) / 255.0f, (float)(c >> 24) / 255.0f};
}

/** over() - @src composited over @dst. */
static Rgba over(Rgba src, Rgba dst) {
  const float a = src.a + dst.a * (1.0f - src.a);
  if (a <= 0.0f)
    return (Rgba){0, 0, 0, 0};
  const float k = dst.a * (1.0f - src.a);
  return (Rgba){(src.r * src.a + dst.r * k) / a, (src.g * src.a + dst.g * k) / a,
                (src.b * src.a + dst.b * k) / a, a};
}

/** pack() - Channels back to the packed ABGR texel. */
static uint32_t pack(Rgba c) {
  return RGBA8((int)(c.r * 255.0f + 0.5f), (int)(c.g * 255.0f + 0.5f), (int)(c.b * 255.0f + 0.5f),
               (int)(c.a * 255.0f + 0.5f));
}

/** Per-texture look: border, fill and an optional inner glow. */
typedef struct {
  uint32_t border;
  int border_px;
  uint32_t fill;
  uint32_t glow;  ///< inner glow colour at the cell edge, 0 for none
} CellLook;

/** texel() - The colour of a cell texture at distance @edge pixels inside its outline. */
static uint32_t texel(const CellLook *look, int edge) {
  if (edge < look->border_px)
    return look->border;
  Rgba c = unpack(look->fill);
  if (look->glow != 0 && edge < UI_ZONE_GLOW_PX) {
    Rgba glow = unpack(look->glow);
    glow.a *= 1.0f - (float)edge / (float)UI_ZONE_GLOW_PX;
    c = over(glow, c);
  }
  return pack(c);
}

/** new_texture() - A w x h point-sampled empty texture, or NULL (logged). */
static vita2d_texture *new_texture(int w, int h) {
  vita2d_texture *tex = vita2d_create_empty_texture((unsigned int)w, (unsigned int)h);
  if (!tex) {
    LOGE("ui_zone_grid: could not allocate %dx%d texture", w, h);
    return NULL;
  }
  vita2d_texture_set_filters(tex, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
  return tex;
}

/** bake_cell() - One cell-sized texture drawn from @look. */
static vita2d_texture *bake_cell(int w, int h, const CellLook *look) {
  vita2d_texture *tex = new_texture(w, h);
  if (!tex)
    return NULL;
  const int stride_px = (int)vita2d_texture_get_stride(tex) / (int)sizeof(uint32_t);
  uint32_t *pixels = (uint32_t *)vita2d_texture_get_datap(tex);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      int edge = x < w - 1 - x ? x : w - 1 - x;
      const int edge_y = y < h - 1 - y ? y : h - 1 - y;
      edge = edge < edge_y ? edge : edge_y;
      pixels[y * stride_px + x] = texel(look, edge);
    }
  }
  return tex;
}

/**
 * bake_lines() - The whole-grid texture: the 1 px outline of every cell in ZONE_LINE, the rest
 * transparent. One texture so the idle grid costs one draw instead of 18.
 */
static vita2d_texture *bake_lines(const UiZoneGrid *g) {
  const int w = g->visible.w;
  const int h = g->visible.h;
  vita2d_texture *tex = new_texture(w, h);
  if (!tex)
    return NULL;
  const int stride_px = (int)vita2d_texture_get_stride(tex) / (int)sizeof(uint32_t);
  uint32_t *pixels = (uint32_t *)vita2d_texture_get_datap(tex);
  for (int y = 0; y < h; y++) {
    const int cy = y % g->cell_h;
    const bool line_y = cy < UI_ZONE_BORDER_LINE || cy >= g->cell_h - UI_ZONE_BORDER_LINE;
    for (int x = 0; x < w; x++) {
      const int cx = x % g->cell_w;
      const bool line = line_y || cx < UI_ZONE_BORDER_LINE || cx >= g->cell_w - UI_ZONE_BORDER_LINE;
      pixels[y * stride_px + x] = line ? UI_ZONE_LINE : 0;
    }
  }
  return tex;
}

bool ui_zone_grid_init(UiZoneGrid *grid, UiRect area, bool read_only) {
  memset(grid, 0, sizeof(*grid));
  grid->cell_w = area.w / UI_ZONE_COLS;
  grid->cell_h = area.h / UI_ZONE_ROWS;
  if (grid->cell_w <= 2 * UI_ZONE_BORDER_PICK || grid->cell_h <= 2 * UI_ZONE_BORDER_PICK) {
    LOGE("ui_zone_grid: area %dx%d is too small for a %dx%d grid", area.w, area.h, UI_ZONE_COLS,
         UI_ZONE_ROWS);
    return false;
  }
  grid->visible =
      (UiRect){area.x, area.y, grid->cell_w * UI_ZONE_COLS, grid->cell_h * UI_ZONE_ROWS};
  grid->hit = grid->visible;
  grid->read_only = read_only;

  const CellLook mapped = {UI_ZONE_MAPPED_LINE, UI_ZONE_BORDER_LINE, UI_ACCENT_ZONE, 0};
  const CellLook cursor = {UI_TEXT, UI_ZONE_BORDER_PICK, UI_FILL_FOCUS, 0};
  const CellLook picked = {UI_TEXT, UI_ZONE_BORDER_PICK, UI_FILL_ON, UI_GLOW_INNER};
  grid->tex_lines = bake_lines(grid);
  grid->tex_mapped = bake_cell(grid->cell_w, grid->cell_h, &mapped);
  grid->tex_cursor = bake_cell(grid->cell_w, grid->cell_h, &cursor);
  grid->tex_picked = bake_cell(grid->cell_w, grid->cell_h, &picked);
  if (!grid->tex_lines || !grid->tex_mapped || !grid->tex_cursor || !grid->tex_picked) {
    ui_zone_grid_destroy(grid);
    return false;
  }
  return true;
}

void ui_zone_grid_destroy(UiZoneGrid *grid) {
  vita2d_texture **textures[] = {&grid->tex_lines, &grid->tex_mapped, &grid->tex_cursor,
                                 &grid->tex_picked};
  for (size_t i = 0; i < sizeof(textures) / sizeof(textures[0]); i++) {
    if (*textures[i]) {
      vita2d_wait_rendering_done();
      vita2d_free_texture(*textures[i]);
      *textures[i] = NULL;
    }
  }
}

void ui_zone_grid_set_cell(UiZoneGrid *grid, int cell, const char *label, bool mapped) {
  if (cell < 0 || cell >= UI_ZONE_COUNT)
    return;
  grid->labels[cell][0] = '\0';
  if (label) {
    strncpy(grid->labels[cell], label, UI_ZONE_LABEL_MAX - 1);
    grid->labels[cell][UI_ZONE_LABEL_MAX - 1] = '\0';
  }
  grid->label_x[cell] = (grid->cell_w - ui_text_face_width(UI_FACE_T16, grid->labels[cell])) / 2;
  grid->mapped[cell] = mapped;
}

void ui_zone_grid_clear_selection(UiZoneGrid *grid) {
  ui_zone_sel_clear(&grid->selection);
  grid->hold_active = false;
  grid->paint_active = false;
}

/** cell_x() / cell_y() - Top-left corner of a cell in screen pixels. */
static int cell_x(const UiZoneGrid *g, int cell) {
  return g->visible.x + (cell % UI_ZONE_COLS) * g->cell_w;
}
static int cell_y(const UiZoneGrid *g, int cell) {
  return g->visible.y + (cell / UI_ZONE_COLS) * g->cell_h;
}

void ui_zone_grid_draw(const UiZoneGrid *grid) {
  if (!grid->tex_lines)
    return;
  const uint32_t tint = ui_layer_color(TEX_TINT);
  vita2d_draw_texture_tint(grid->tex_lines, (float)grid->visible.x, (float)grid->visible.y, tint);

  for (int i = 0; i < UI_ZONE_COUNT; i++) {
    const bool picked = !grid->read_only && grid->selection.picked[i];
    const bool cursor = !grid->read_only && i == grid->cursor;
    vita2d_texture *state = picked            ? grid->tex_picked
                            : cursor          ? grid->tex_cursor
                            : grid->mapped[i] ? grid->tex_mapped
                                              : NULL;
    if (state)
      vita2d_draw_texture_tint(state, (float)cell_x(grid, i), (float)cell_y(grid, i), tint);
  }
  for (int i = 0; i < UI_ZONE_COUNT; i++) {
    if (grid->labels[i][0] == '\0')
      continue;
    ui_text_draw_face_centered_v(UI_FACE_T16, cell_x(grid, i) + grid->label_x[i], cell_y(grid, i),
                                 grid->cell_h, ui_layer_color(UI_TEXT), grid->labels[i]);
  }
}

/** touch_begin() - A finger went down on the grid: start a paint gesture on its cell. */
static void touch_begin(UiZoneGrid *g, const UiTouch *t) {
  const int cell = ui_zone_cell_from_point(g->visible, t->x, t->y);
  ui_zone_sel_clear(&g->selection);
  g->hold_active = false;
  g->paint_active = true;
  ui_zone_sel_add(&g->selection, cell);
  g->cursor = cell;
}

/**
 * touch_input() - Advance a finger gesture.
 * Cells beyond the first are visited only once the finger has dragged past UI_TOUCH_DRAG_PX, so
 * a tap that wobbles over a cell edge stays a one-cell tap.
 */
static UiEvent touch_input(UiZoneGrid *g, const UiTouch *t) {
  UiEvent ev = UI_EVENT_NONE;
  if (t->pressed && ui_rect_contains(g->hit, t->x, t->y)) {
    touch_begin(g, t);
    ev = UI_EVENT_MOVED;
  }
  if (!g->paint_active)
    return ev;

  if (t->down && t->dragged) {
    const int cell = ui_zone_cell_from_point(g->visible, t->x, t->y);
    if (ui_zone_sel_paint_visit(&g->selection, cell))
      ev = UI_EVENT_MOVED;
    if (cell >= 0 && cell != g->cursor) {
      g->cursor = cell;
      ev = UI_EVENT_MOVED;
    }
  }
  if (t->released) {
    g->paint_active = false;
    return g->selection.count > 0 ? UI_EVENT_ACTIVATED : UI_EVENT_NONE;
  }
  if (!t->down) {
    /* The finger was taken away without a release (touch blocked): abandon the gesture. */
    ui_zone_grid_clear_selection(g);
  }
  return ev;
}

/** pad_input() - D-pad cursor moves and the hold-Confirm selection. */
static UiEvent pad_input(UiZoneGrid *g, const UiInput *in) {
  UiEvent ev = UI_EVENT_NONE;
  if (in->pressed & UI_BTN_CONFIRM) {
    ui_zone_sel_clear(&g->selection);
    ui_zone_sel_add(&g->selection, g->cursor);
    g->hold_active = true;
    ev = UI_EVENT_MOVED;
  }
  if (in->repeat & UI_BTN_DPAD) {
    const int next = ui_zone_cursor_move(g->cursor, in->repeat);
    if (next != g->cursor) {
      g->cursor = next;
      ev = UI_EVENT_MOVED;
      if (g->hold_active)
        ui_zone_sel_add(&g->selection, next);
    }
    if (!g->hold_active && g->selection.count > 0) {
      /* Picks left over from a cancelled popup do not outlive the next move. */
      ui_zone_sel_clear(&g->selection);
      ev = UI_EVENT_MOVED;
    }
  }
  if ((in->released & UI_BTN_CONFIRM) && g->hold_active) {
    g->hold_active = false;
    return UI_EVENT_ACTIVATED;
  }
  return ev;
}

UiEvent ui_zone_grid_input(UiZoneGrid *grid, const UiInput *in) {
  if (grid->read_only)
    return ui_touch_tap(in) && ui_rect_contains(grid->hit, in->touch.x, in->touch.y)
               ? UI_EVENT_ACTIVATED
               : UI_EVENT_NONE;

  const UiEvent touch = touch_input(grid, &in->touch);
  if (grid->paint_active || touch == UI_EVENT_ACTIVATED)
    return touch;
  const UiEvent pad = pad_input(grid, in);
  return touch != UI_EVENT_NONE && pad == UI_EVENT_NONE ? touch : pad;
}
