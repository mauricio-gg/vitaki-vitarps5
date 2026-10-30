// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "mic_touch.h"

#include <stddef.h>

/* True when the screen-pixel point lies inside the rectangle (right and bottom edges excluded). */
static bool in_rect(int px, int py, int x, int y, int w, int h) {
  return px >= x && px < x + w && py >= y && py < y + h;
}

bool mic_touch_hit(MicTouchMode mode, int label_w, int touch_x, int touch_y) {
  const int sx = touch_x / MIC_TOUCH_PER_SCREEN_PX;
  const int sy = touch_y / MIC_TOUCH_PER_SCREEN_PX;
  switch (mode) {
    case MIC_TOUCH_MODE_BUTTON: {
      const int pill_w = label_w > 0 ? label_w : MIC_BTN_W;
      const int pad_x = label_w > 0 ? MIC_BTN_LABEL_TAP_PAD : MIC_BTN_TAP_PAD_X;
      const int pad_y = label_w > 0 ? MIC_BTN_LABEL_TAP_PAD : MIC_BTN_TAP_PAD_Y;
      const int pill_x = (MIC_SCREEN_W - pill_w) / 2;
      return in_rect(sx, sy, pill_x - pad_x, MIC_BTN_Y - pad_y, pill_w + 2 * pad_x,
                     MIC_BTN_H + 2 * pad_y);
    }
    case MIC_TOUCH_MODE_CORNER:
      return in_rect(sx, sy, MIC_CORNER_X - MIC_CORNER_TAP_PAD, MIC_CORNER_Y - MIC_CORNER_TAP_PAD,
                     MIC_CORNER_SIZE + 2 * MIC_CORNER_TAP_PAD,
                     MIC_CORNER_SIZE + 2 * MIC_CORNER_TAP_PAD);
    case MIC_TOUCH_MODE_NONE:
      break;
  }
  return false;
}

void mic_touch_pass_begin(MicTouchTracker *t) {
  for (int i = 0; i < MIC_TOUCH_SLOTS; i++)
    t->slots[i].seen = false;
}

bool mic_touch_judge(MicTouchTracker *t, uint8_t id, int touch_x, int touch_y, MicTouchMode mode,
                     int label_w, bool *tap_out) {
  MicTouchSlot *free_slot = NULL;
  *tap_out = false;
  for (int i = 0; i < MIC_TOUCH_SLOTS; i++) {
    if (t->slots[i].active && t->slots[i].id == id) {
      t->slots[i].seen = true;
      return t->slots[i].swallowed;
    }
    if (!t->slots[i].active && !free_slot)
      free_slot = &t->slots[i];
  }
  const bool hit = mic_touch_hit(mode, label_w, touch_x, touch_y);
  if (!free_slot)
    return hit; /* cannot happen: the panel reports no more touches than there are slots */
  *free_slot = (MicTouchSlot){.active = true, .seen = true, .swallowed = hit, .id = id};
  *tap_out = hit;
  return hit;
}

void mic_touch_pass_end(MicTouchTracker *t) {
  for (int i = 0; i < MIC_TOUCH_SLOTS; i++) {
    if (t->slots[i].active && !t->slots[i].seen)
      t->slots[i].active = false;
  }
}
