// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "press_meter.h"

#include <string.h>

#define US_PER_MS 1000ULL

/* Second largest size in the quiet window (0 when it holds fewer than two frames). Using the
 * second largest instead of the largest means one outlier frame cannot hide an answer. */
static uint32_t quiet_reference(const PressMeter *m) {
  uint32_t largest = 0;
  uint32_t second = 0;
  for (uint32_t i = 0; i < m->quiet_count; i++) {
    uint32_t v = m->quiet[i];
    if (v > largest) {
      second = largest;
      largest = v;
    } else if (v > second) {
      second = v;
    }
  }
  return second;
}

static void quiet_push(PressMeter *m, uint32_t size) {
  m->quiet[m->quiet_next] = size;
  m->quiet_next = (m->quiet_next + 1) % PRESS_METER_QUIET_WINDOW;
  if (m->quiet_count < PRESS_METER_QUIET_WINDOW)
    m->quiet_count++;
}

/* Removes the oldest waiting press. */
static void queue_pop_front(PressMeter *m) {
  m->queue_len--;
  for (uint32_t i = 0; i < m->queue_len; i++)
    m->queue[i] = m->queue[i + 1];
}

/* Ends the wait of presses (oldest first) that saw no jump in time. */
static void expire_due_presses(PressMeter *m, uint64_t now_us) {
  while (m->queue_len > 0 && now_us >= m->queue[0].press_us &&
         now_us - m->queue[0].press_us >= PRESS_METER_EXPIRY_US) {
    queue_pop_front(m);
    m->expired_total++;
  }
}

/* Cuts off a burst that has lasted too long and rebuilds the quiet window from scratch. */
static void end_overlong_burst(PressMeter *m, uint64_t now_us) {
  if (m->in_burst && now_us >= m->burst_start_us &&
      now_us - m->burst_start_us >= PRESS_METER_BURST_MAX_US) {
    m->in_burst = false;
    m->quiet_count = 0;
    m->quiet_next = 0;
  }
}

/* A frame inside a burst: only calm frames enter the quiet window (so burst frames never raise
 * the reference), and enough calm frames in a row end the burst. */
static void burst_frame(PressMeter *m, uint32_t size) {
  if ((uint64_t)size < (uint64_t)m->burst_ref + PRESS_METER_CALM_MARGIN_BYTES) {
    quiet_push(m, size);
    if (++m->burst_calm_run >= PRESS_METER_BURST_END_CALM_FRAMES)
      m->in_burst = false;
  } else {
    m->burst_calm_run = 0;
  }
}

/* True when a press sent at press_us can be the cause of a frame whose first packet was read at
 * first_us. The stamps have 1 ms resolution, so compare whole milliseconds. Unknown first_us
 * (0) cannot rule anything out. */
static bool press_can_answer(uint64_t press_us, uint64_t first_us) {
  return first_us == 0 || first_us / US_PER_MS >= press_us / US_PER_MS;
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

  m->unseen_total += press_count - m->last_press_count - 1;
  m->last_press_count = press_count;

  expire_due_presses(m, press_us);
  end_overlong_burst(m, press_us);
  if (m->queue_len >= PRESS_METER_QUEUE_DEPTH) {
    m->full_total++;
    return;
  }
  if (m->in_burst)
    m->inburst_total++;
  m->queue[m->queue_len].press_count = press_count;
  m->queue[m->queue_len].press_us = press_us;
  m->queue[m->queue_len].in_burst = m->in_burst;
  m->queue_len++;
}

bool press_meter_on_frame(PressMeter *m, uint32_t size, bool corrupt, uint64_t first_us,
                          uint64_t arrive_us, PressJump *out) {
  if (corrupt)
    return false;

  expire_due_presses(m, arrive_us);
  end_overlong_burst(m, arrive_us);

  if (m->in_burst) {
    burst_frame(m, size);
    return false;
  }
  if (m->quiet_count < PRESS_METER_QUIET_WINDOW) {
    quiet_push(m, size);
    return false;
  }

  uint32_t ref = quiet_reference(m);
  bool large = (uint64_t)size >= (uint64_t)ref * PRESS_METER_JUMP_RATIO &&
               (uint64_t)size >= (uint64_t)ref + PRESS_METER_JUMP_MIN_EXTRA_BYTES;
  if (!large) {
    quiet_push(m, size);
    return false;
  }

  /* A large frame starts a burst whether or not anyone asked for it: its follow-up frames must
   * neither become the next press's answer nor raise the quiet reference. */
  m->in_burst = true;
  m->burst_start_us = arrive_us;
  m->burst_ref = ref;
  m->burst_calm_run = 0;

  /* Candidates: waiting presses sent no later than this frame's first packet. The queue is
   * ordered by send time, so they are a prefix of it. */
  uint32_t candidates = 0;
  while (candidates < m->queue_len && press_can_answer(m->queue[candidates].press_us, first_us))
    candidates++;
  if (candidates == 0) {
    m->unmatched_total++;
    return false;
  }

  out->press_count = m->queue[0].press_count;
  out->press_us = m->queue[0].press_us;
  out->first_us = first_us;
  out->arrive_us = arrive_us;
  out->frame_size = size;
  out->quiet_ref = ref;
  out->waiting = candidates;
  out->in_burst = m->queue[0].in_burst;
  queue_pop_front(m);
  m->jumps_total++;
  return true;
}

void press_meter_complete(const PressJump *jump, uint64_t decoded_us, uint64_t shown_us,
                          PressSample *out) {
  out->press_count = jump->press_count;
  out->frame_size = jump->frame_size;
  out->quiet_ref = jump->quiet_ref;
  out->waiting = jump->waiting;
  out->in_burst = jump->in_burst;
  out->first_us = diff_us(jump->first_us, jump->press_us);
  out->assembled_us = diff_us(jump->arrive_us, jump->press_us);
  out->decoded_us = diff_us(decoded_us, jump->press_us);
  out->shown_us = diff_us(shown_us, jump->press_us);
  out->order_ok = jump->first_us != 0 && out->first_us >= 0 && out->first_us <= out->assembled_us &&
                  out->assembled_us <= out->decoded_us && out->decoded_us <= out->shown_us;
}

void press_meter_counters(const PressMeter *m, PressCounters *out) {
  out->expired = m->expired_total;
  out->full = m->full_total;
  out->inburst = m->inburst_total;
  out->unseen = m->unseen_total;
  out->unmatched = m->unmatched_total;
  out->lost = 0;
}

/* now - prev, or now when the counter went down (session restart). */
static uint32_t counter_delta(uint32_t now, uint32_t prev) {
  return now >= prev ? now - prev : now;
}

void press_counters_delta(const PressCounters *now, const PressCounters *prev, PressCounters *out) {
  out->expired = counter_delta(now->expired, prev->expired);
  out->full = counter_delta(now->full, prev->full);
  out->inburst = counter_delta(now->inburst, prev->inburst);
  out->unseen = counter_delta(now->unseen, prev->unseen);
  out->unmatched = counter_delta(now->unmatched, prev->unmatched);
  out->lost = counter_delta(now->lost, prev->lost);
}
