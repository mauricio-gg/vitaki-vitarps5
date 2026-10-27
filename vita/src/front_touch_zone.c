// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "front_touch_zone.h"

#include <string.h>

/* Adds one mapped zone to @p out: a zone mapped as the L2 (or R2) trigger holds it fully, a
 * touchpad mapping makes the touch a touchpad touch, anything else is a button bit. */
static void apply_trigger_zone(FrontTouchOutcome *out, const VitakiCtrlMapInfo *vcmi,
                               VitakiCtrlIn input, bool left) {
  if ((left ? vcmi->in_l2 : vcmi->in_r2) == (int)input) {
    if (left)
      out->l2 = true;
    else
      out->r2 = true;
    return;
  }
  int mapped = vcmi->in_out_btn[input];
  if (mapped == VITAKI_CTRL_OUT_TOUCHPAD)
    out->to_touchpad = true;
  else
    out->buttons |= (uint32_t)mapped;
}

/* Adds one mapping that is a plain button or touchpad (no trigger handling). */
static void apply_plain_zone(FrontTouchOutcome *out, int mapped) {
  if (mapped == VITAKI_CTRL_OUT_TOUCHPAD)
    out->to_touchpad = true;
  else
    out->buttons |= (uint32_t)mapped;
}

/* Index of the front grid zone under (x, y), or VITAKI_CTRL_IN_NONE for a negative position. */
static VitakiCtrlIn front_grid_input_from_touch(int x, int y, int max_w, int max_h) {
  if (x < 0 || y < 0)
    return VITAKI_CTRL_IN_NONE;
  if (x >= max_w)
    x = max_w - 1;
  if (y >= max_h)
    y = max_h - 1;
  int col = (x * VITAKI_FRONT_TOUCH_GRID_COLS) / max_w;
  int row = (y * VITAKI_FRONT_TOUCH_GRID_ROWS) / max_h;
  if (col < 0)
    col = 0;
  if (col >= VITAKI_FRONT_TOUCH_GRID_COLS)
    col = VITAKI_FRONT_TOUCH_GRID_COLS - 1;
  if (row < 0)
    row = 0;
  if (row >= VITAKI_FRONT_TOUCH_GRID_ROWS)
    row = VITAKI_FRONT_TOUCH_GRID_ROWS - 1;
  return (VitakiCtrlIn)(VITAKI_CTRL_IN_FRONTTOUCH_GRID_START + row * VITAKI_FRONT_TOUCH_GRID_COLS +
                        col);
}

/* Works out what a touch starting at (x, y) does: the any-touch mapping, the grid zone, the
 * half and corner arc it lies in, and the center zone. */
static FrontTouchOutcome resolve_outcome(int x, int y, int max_w, int max_h,
                                         const VitakiCtrlMapInfo *vcmi) {
  FrontTouchOutcome out = {0};
  const int w_by_2 = max_w / 2;
  const int w_by_4 = max_w / 4;
  const int h_by_4 = max_h / 4;
  const int arc_radius = max_h / 3;
  const int arc_radius_2 = arc_radius * arc_radius;

  apply_plain_zone(&out, vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_ANY]);

  VitakiCtrlIn grid_input = front_grid_input_from_touch(x, y, max_w, max_h);
  out.grid = grid_input;
  out.grid_out = grid_input != VITAKI_CTRL_IN_NONE ? vcmi->in_out_btn[grid_input] : 0;
  if (grid_input != VITAKI_CTRL_IN_NONE) {
    int mapped = vcmi->in_out_btn[grid_input];
    if (mapped == VITAKI_CTRL_OUT_L2)
      out.l2 = true;
    else if (mapped == VITAKI_CTRL_OUT_R2)
      out.r2 = true;
    else if (mapped != VITAKI_CTRL_OUT_NONE)
      apply_plain_zone(&out, mapped);
  }

  if (x > w_by_2) {
    apply_trigger_zone(&out, vcmi, VITAKI_CTRL_IN_FRONTTOUCH_RIGHT, false);
    out.zones |= FRONT_TOUCH_ZONE_RIGHT_HALF;
    if (y * y + (x - max_w) * (x - max_w) <= arc_radius_2) {
      apply_trigger_zone(&out, vcmi, VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC, false);
      out.zones |= FRONT_TOUCH_ZONE_UR_ARC;
    } else if ((y - max_h) * (y - max_h) + (x - max_w) * (x - max_w) <= arc_radius_2) {
      apply_trigger_zone(&out, vcmi, VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC, false);
      out.zones |= FRONT_TOUCH_ZONE_LR_ARC;
    }
  } else if (x < w_by_2) {
    apply_trigger_zone(&out, vcmi, VITAKI_CTRL_IN_FRONTTOUCH_LEFT, true);
    out.zones |= FRONT_TOUCH_ZONE_LEFT_HALF;
    if (y * y + x * x <= arc_radius_2) {
      apply_trigger_zone(&out, vcmi, VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC, true);
      out.zones |= FRONT_TOUCH_ZONE_UL_ARC;
    } else if ((y - max_h) * (y - max_h) + x * x <= arc_radius_2) {
      apply_trigger_zone(&out, vcmi, VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC, true);
      out.zones |= FRONT_TOUCH_ZONE_LL_ARC;
    }
  }

  if (x >= w_by_4 && x <= max_w - w_by_4 && y >= h_by_4 && y <= max_h - h_by_4) {
    out.zones |= FRONT_TOUCH_ZONE_CENTER;
    apply_plain_zone(&out, vcmi->in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_CENTER]);
  }
  return out;
}

void front_touch_zone_init(FrontTouchZones *z) {
  memset(z, 0, sizeof(*z));
}

void front_touch_zone_begin_pass(FrontTouchZones *z) {
  for (int i = 0; i < FRONT_TOUCH_ZONE_MAX_TOUCHES; i++)
    z->entry[i].seen = false;
}

const FrontTouchOutcome *front_touch_zone_latch(FrontTouchZones *z, uint8_t vita_touch_id, int x,
                                                int y, int max_w, int max_h,
                                                const VitakiCtrlMapInfo *vcmi) {
  FrontTouchEntry *free_entry = NULL;
  for (int i = 0; i < FRONT_TOUCH_ZONE_MAX_TOUCHES; i++) {
    FrontTouchEntry *e = &z->entry[i];
    if (e->active && e->vita_touch_id == vita_touch_id) {
      e->seen = true;
      return &e->outcome;
    }
    if (!e->active && !free_entry)
      free_entry = e;
  }
  if (!free_entry)
    return NULL;
  free_entry->active = true;
  free_entry->seen = true;
  free_entry->vita_touch_id = vita_touch_id;
  free_entry->outcome = resolve_outcome(x, y, max_w, max_h, vcmi);
  return &free_entry->outcome;
}

void front_touch_zone_end_pass(FrontTouchZones *z) {
  for (int i = 0; i < FRONT_TOUCH_ZONE_MAX_TOUCHES; i++) {
    if (z->entry[i].active && !z->entry[i].seen)
      z->entry[i].active = false;
  }
}
