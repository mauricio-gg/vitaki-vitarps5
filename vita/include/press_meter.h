// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

/*
 * Press-to-picture meter (#413): the pure decision logic. It is told when a button press was sent
 * and when each video frame arrived, and picks the first frame that is clearly larger than the
 * quiet frames before it (the console's answer to the press, e.g. a menu move). It has no SDK
 * dependency so it compiles and runs natively. All times are microseconds on one clock
 * (sceKernelGetProcessTimeWide() on the Vita). See docs/design/streaming.md, "Delay probes (#413)".
 *
 * Threading: press_meter_on_press() and press_meter_on_frame() are called from one thread only
 * (the Takion receive thread). The three *_total counters are plain 32-bit words that other
 * threads may read without a lock; they only ever go up.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Number of most recent quiet frames remembered. The quiet reference is the largest of them. */
#define PRESS_METER_QUIET_WINDOW 16
/* A frame is "the jump" when it is at least this many times the quiet reference ...
 * First guesses; tune them from the first hardware log. */
#define PRESS_METER_JUMP_RATIO 3
/* ... AND at least this many bytes larger than it, so noise on tiny frames cannot trigger. */
#define PRESS_METER_JUMP_MIN_EXTRA_BYTES 2048
/* A press with no jump within this long expires (below the maintainer's two-second press rhythm). */
#define PRESS_METER_EXPIRY_US 1500000ULL

typedef struct {
  uint32_t quiet[PRESS_METER_QUIET_WINDOW]; /* ring of recent quiet frame sizes */
  uint32_t quiet_count;                     /* filled slots, up to PRESS_METER_QUIET_WINDOW */
  uint32_t quiet_next;                      /* next slot to overwrite */
  bool waiting;                             /* a press is waiting for its picture */
  uint32_t waiting_press_count;
  uint64_t waiting_press_us;
  uint32_t last_press_count; /* newest press counter already handed to on_press() */
  /* Running totals, readable from other threads (see the threading note above). */
  volatile uint32_t expired_total; /* presses that saw no jump in time */
  volatile uint32_t overlap_total; /* presses ignored: another was pending, or not seen singly */
  volatile uint32_t jumps_total;   /* jumps found (each becomes one sample once shown) */
} PressMeter;

/* The frame chosen as the answer to a press, as seen at the moment it arrived. */
typedef struct {
  uint32_t press_count;
  uint64_t press_us;    /* when the press was sent */
  uint64_t first_us;    /* when the frame's first packet was read (1 ms resolution, 0 = unknown) */
  uint64_t arrive_us;   /* when the assembled frame reached the decode hand-off */
  uint32_t frame_size;  /* jump frame size in bytes */
  uint32_t quiet_ref;   /* largest recent quiet frame at that moment */
} PressJump;

/* A finished sample: stage times in microseconds since the press was sent. Signed on purpose:
 * values are never clamped, so an impossible order stays visible (see order_ok). */
typedef struct {
  uint32_t press_count;
  uint32_t frame_size;
  uint32_t quiet_ref;
  int32_t first_us;    /* press sent -> first packet of the frame read */
  int32_t assembled_us; /* press sent -> frame handed to vita_h264_decode_frame() */
  int32_t decoded_us;  /* press sent -> decode finished */
  int32_t shown_us;    /* press sent -> swapped to the screen */
  bool order_ok;       /* 0 <= first <= assembled <= decoded <= shown, first known */
} PressSample;

/* Clears everything, including the totals. Call at the start of a stream session. */
void press_meter_reset(PressMeter *m);

/*
 * Reports the newest press stamp seen on the sender. Call it before each frame with the current
 * stamp; it does nothing unless the counter moved. A press arriving while another waits is
 * counted as an overlap and ignored. Presses skipped between two calls (counter moved by more
 * than one) are counted as overlaps too, because only the newest time is known.
 */
void press_meter_on_press(PressMeter *m, uint32_t press_count, uint64_t press_us);

/*
 * Feeds one arrived video frame. Corrupt frames are ignored entirely. Returns true and fills
 * *out when this frame is the jump for the waiting press; the press then stops waiting, so the
 * following large frames of the same animation give no further samples. A large frame whose
 * first packet was read before the press was sent cannot be an answer to it; it is ignored
 * (not counted as quiet either). first_us is the frame's first-packet time, 0 if unknown.
 */
bool press_meter_on_frame(PressMeter *m, uint32_t size, bool corrupt, uint64_t first_us,
                          uint64_t arrive_us, PressJump *out);

/* Builds the finished sample from the jump and the decode-done / swap times of the picture that
 * was actually shown. Clamps nothing; order_ok is false when any stage is out of order. */
void press_meter_complete(const PressJump *jump, uint64_t decoded_us, uint64_t shown_us,
                          PressSample *out);
