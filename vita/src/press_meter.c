// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "press_meter.h"

#include <string.h>

#define US_PER_MS 1000ULL

/* Largest size in the quiet window (0 when the window is empty). */
static uint32_t quiet_reference(const PressMeter *m) {
  uint32_t ref = 0;
  for (uint32_t i = 0; i < m->quiet_count; i++) {
    if (m->quiet[i] > ref)
      ref = m->quiet[i];
  }
  return ref;
}

static void quiet_push(PressMeter *m, uint32_t size) {
  m->quiet[m->quiet_next] = size;
  m->quiet_next = (m->quiet_next + 1) % PRESS_METER_QUIET_WINDOW;
  if (m->quiet_count < PRESS_METER_QUIET_WINDOW)
    m->quiet_count++;
}

/* Ends the wait of a press that saw no jump in time. */
static void expire_if_due(PressMeter *m, uint64_t now_us) {
  if (m->waiting && now_us >= m->waiting_press_us &&
      now_us - m->waiting_press_us >= PRESS_METER_EXPIRY_US) {
    m->waiting = false;
    m->expired_total++;
  }
}

/* Signed microsecond difference a - b, saturated to the int32 range (about +-35 minutes). */
static int32_t diff_us(uint64_t a, uint64_t b) {
  int64_t d = (int64_t)a - (int64_t)b;
  if (d > INT32_MAX)
    return INT32_MAX;
  if (d < INT32_MIN)
    return INT32_MIN;
  return (int32_t)d;
}

void press_meter_reset(PressMeter *m) {
  memset(m, 0, sizeof(*m));
}

void press_meter_on_press(PressMeter *m, uint32_t press_count, uint64_t press_us) {
  /* The sender restarted its counter (new session on the same struct): start over. */
  if (press_count < m->last_press_count)
    m->last_press_count = 0;
  if (press_count == m->last_press_count)
    return;

  uint32_t skipped = press_count - m->last_press_count - 1;
  m->last_press_count = press_count;
  m->overlap_total += skipped;

  expire_if_due(m, press_us);
  if (m->waiting) {
    m->overlap_total++;
    return;
  }
  m->waiting = true;
  m->waiting_press_count = press_count;
  m->waiting_press_us = press_us;
}

bool press_meter_on_frame(PressMeter *m, uint32_t size, bool corrupt, uint64_t first_us,
                          uint64_t arrive_us, PressJump *out) {
  if (corrupt)
    return false;

  expire_if_due(m, arrive_us);

  if (m->waiting && m->quiet_count == PRESS_METER_QUIET_WINDOW) {
    uint32_t ref = quiet_reference(m);
    bool large = (uint64_t)size >= (uint64_t)ref * PRESS_METER_JUMP_RATIO &&
                 (uint64_t)size >= (uint64_t)ref + PRESS_METER_JUMP_MIN_EXTRA_BYTES;
    if (large) {
      /* The first packet was read in an earlier millisecond than the press was sent: this
       * frame was already under way, so it cannot be the answer. Ignore it entirely. */
      if (first_us != 0 && first_us / US_PER_MS < m->waiting_press_us / US_PER_MS)
        return false;
      out->press_count = m->waiting_press_count;
      out->press_us = m->waiting_press_us;
      out->first_us = first_us;
      out->arrive_us = arrive_us;
      out->frame_size = size;
      out->quiet_ref = ref;
      m->waiting = false;
      m->jumps_total++;
      return true;
    }
  }

  quiet_push(m, size);
  return false;
}

void press_meter_complete(const PressJump *jump, uint64_t decoded_us, uint64_t shown_us,
                          PressSample *out) {
  out->press_count = jump->press_count;
  out->frame_size = jump->frame_size;
  out->quiet_ref = jump->quiet_ref;
  out->first_us = diff_us(jump->first_us, jump->press_us);
  out->assembled_us = diff_us(jump->arrive_us, jump->press_us);
  out->decoded_us = diff_us(decoded_us, jump->press_us);
  out->shown_us = diff_us(shown_us, jump->press_us);
  out->order_ok = jump->first_us != 0 && out->first_us >= 0 && out->first_us <= out->assembled_us &&
                  out->assembled_us <= out->decoded_us && out->decoded_us <= out->shown_us;
}
