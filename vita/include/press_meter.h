// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

/*
 * Press-to-picture meter (#413): the pure decision logic. It is told when a button press was sent
 * and when each video frame arrived, and picks the first frame that is clearly larger than the
 * quiet frames before it (the console's answer to a press, e.g. a menu move). It has no SDK
 * dependency so it compiles and runs natively. All times are microseconds on one clock
 * (sceKernelGetProcessTimeWide() on the Vita). See docs/design/streaming.md, section 9.1.
 *
 * How it decides (the short version; the design doc has the reasoning):
 *  - Presses wait in a small FIFO queue. An answer is matched to the OLDEST waiting press whose
 *    send time is not later than the answer's first packet. We cannot know which press an answer
 *    belongs to (a press at the end of a list has none), so every sample carries `waiting`: how
 *    many presses were candidates. waiting == 1 is unambiguous; keep only those when in doubt.
 *  - A menu move is a run of large frames (a burst). The first large frame after quiet is the
 *    jump; the burst lasts until the picture has been calm for PRESS_METER_BURST_END_CALM_FRAMES
 *    frames in a row. No new jump is accepted inside a burst, so the tail of one move can never
 *    become the answer to the next press. A press sent while a burst is running still waits (its
 *    answer comes at least 150 ms later, usually after the burst has ended) but is counted as
 *    `inburst` and marked on its sample, because if its own answer merges into the running burst
 *    it cannot be seen, and a later jump could then be taken for it. At one to two presses a
 *    second most presses are sent inside the previous move's burst, so refusing them would
 *    discard most of the data.
 *  - The quiet reference is the second largest of the last PRESS_METER_QUIET_WINDOW calm frames,
 *    so one outlier does not hide an answer. Frames of a burst never enter it.
 *
 * Numbers marked "log" come from the first hardware log (26017450183, PS5 menu navigation at one
 * to two presses per second, 60 fps): measured answers arrived 150-650 ms after the press with
 * jump frames of 3033-7995 bytes, quiet references of 39-1612 bytes, and a menu move shows about
 * 22-30 frames above the previous window's average size (about 0.4 s of burst).
 *
 * Threading: press_meter_on_press() and press_meter_on_frame() are called from one thread only
 * (the Takion receive thread). The *_total counters are plain 32-bit words that other threads
 * may read without a lock; they only ever go up.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Number of most recent calm frames remembered. 8 frames = 133 ms at 60 fps (log), so the
 * reference follows the picture within about a tenth of a second. */
#define PRESS_METER_QUIET_WINDOW 8
/* A frame is "the jump" when it is at least this many times the quiet reference ... */
#define PRESS_METER_JUMP_RATIO 3
/* ... AND at least this many bytes larger than it, so noise on tiny frames cannot trigger. Log:
 * the smallest answer was 3033 bytes over a reference of 558, and quiet windows never had a frame
 * more than about 1000 bytes above their reference; 1536 sits between the two. */
#define PRESS_METER_JUMP_MIN_EXTRA_BYTES 1536
/* A frame inside a burst is "calm" when it is below the pre-burst reference plus this margin
 * (log: the biggest quiet frames of a window were about 850-1000 bytes above its average). */
#define PRESS_METER_CALM_MARGIN_BYTES 1024
/* A burst ends after this many calm frames in a row (133 ms at 60 fps; log: menu-move frames
 * stay large for 22-30 frames and the pauses inside one move are shorter than this). */
#define PRESS_METER_BURST_END_CALM_FRAMES 8
/* A burst that has not calmed down by this time is cut off and the quiet window is rebuilt from
 * scratch, so a permanently busier picture cannot blind the meter for good. */
#define PRESS_METER_BURST_MAX_US 2000000ULL
/* Presses that may wait for an answer at once. At two presses a second and the expiry below at
 * most three can be pending; the fourth slot is headroom. A press finding the queue full is
 * refused and counted. */
#define PRESS_METER_QUEUE_DEPTH 4
/* A press with no jump within this long expires. Log: the slowest answer reached the decode
 * hand-off 667 ms after the press (shown 820 ms), so 1 s keeps a margin of about 1.5x; longer
 * would let a press that got no answer wait through the next press and take its answer. */
#define PRESS_METER_EXPIRY_US 1000000ULL

/* Cumulative counters, all only ever going up within a session. The first five are kept by the
 * meter; `lost` is kept by the hand-off to the UI thread (vita/src/video.c). */
typedef struct {
  uint32_t expired; /* presses that saw no jump in time */
  uint32_t full;    /* presses refused because the queue was full */
  uint32_t inburst; /* presses sent while a burst was running (queued, marked on the sample) */
  uint32_t unseen;  /* presses that fell between two polls of the press stamp (times unknown) */
  uint32_t
      unmatched; /* jumps with no eligible waiting press (not asked for, or already under way) */
  uint32_t lost; /* jumps dropped because the UI thread had not shown the earlier ones */
} PressCounters;

typedef struct {
  uint64_t press_us;
  uint32_t press_count;
  bool in_burst; /* sent while a burst was running */
} PressEntry;

typedef struct {
  uint32_t quiet[PRESS_METER_QUIET_WINDOW];  /* ring of recent calm frame sizes */
  uint32_t quiet_count;                      /* filled slots, up to PRESS_METER_QUIET_WINDOW */
  uint32_t quiet_next;                       /* next slot to overwrite */
  PressEntry queue[PRESS_METER_QUEUE_DEPTH]; /* waiting presses, oldest first */
  uint32_t queue_len;
  uint32_t last_press_count; /* newest press counter already handed to on_press() */
  bool in_burst;             /* a burst of large frames is running */
  uint64_t burst_start_us;
  uint32_t burst_ref;      /* quiet reference when the burst began (frozen for its duration) */
  uint32_t burst_calm_run; /* consecutive calm frames seen inside the burst */
  /* Running totals, readable from other threads (see the threading note above). */
  volatile uint32_t expired_total;
  volatile uint32_t full_total;
  volatile uint32_t inburst_total;
  volatile uint32_t unseen_total;
  volatile uint32_t unmatched_total;
  volatile uint32_t jumps_total; /* jumps matched to a press (each becomes one sample once shown) */
} PressMeter;

/* The frame chosen as the answer to a press, as seen at the moment it arrived. */
typedef struct {
  uint32_t press_count;
  uint64_t press_us;   /* when the press was sent */
  uint64_t first_us;   /* when the frame's first packet was read (1 ms resolution, 0 = unknown) */
  uint64_t arrive_us;  /* when the assembled frame reached the decode hand-off */
  uint32_t frame_size; /* jump frame size in bytes */
  uint32_t quiet_ref;  /* quiet reference at that moment */
  uint32_t waiting;    /* presses that could have been this frame's answer (1 = unambiguous) */
  bool in_burst;       /* the matched press was sent while a burst was running */
} PressJump;

/* A finished sample: stage times in microseconds since the press was sent. Signed on purpose:
 * values are never clamped, so an impossible order stays visible (see order_ok). */
typedef struct {
  uint32_t press_count;
  uint32_t frame_size;
  uint32_t quiet_ref;
  uint32_t waiting;     /* see PressJump.waiting */
  bool in_burst;        /* see PressJump.in_burst */
  int32_t first_us;     /* press sent -> first packet of the frame read */
  int32_t assembled_us; /* press sent -> frame handed to vita_h264_decode_frame() */
  int32_t decoded_us;   /* press sent -> decode finished */
  int32_t shown_us;     /* press sent -> swapped to the screen */
  bool order_ok;        /* 0 <= first <= assembled <= decoded <= shown, first known */
} PressSample;

/* Clears everything, including the totals. Call at the start of a stream session. */
void press_meter_reset(PressMeter *m);

/*
 * Reports the newest press stamp seen on the sender. Call it before each frame with the current
 * stamp; it does nothing unless the counter moved. The press joins the queue, unless the queue
 * is full (counted as full). A press sent while a burst is running is queued but counted as
 * inburst. Presses skipped between two calls (counter moved by more than one) are counted as
 * unseen: only the newest time is known.
 */
void press_meter_on_press(PressMeter *m, uint32_t press_count, uint64_t press_us);

/*
 * Feeds one arrived video frame. Corrupt frames are ignored entirely. Returns true and fills
 * *out when this frame is the jump that answers the oldest eligible waiting press; that press
 * leaves the queue. A jump starts a burst whether or not a press is matched, and frames inside
 * the burst are never jumps, so the tail of one menu move cannot become the next press's sample.
 * A press is eligible only if it was sent no later (1 ms resolution) than the frame's first
 * packet; first_us is that time, 0 if unknown (then every waiting press is eligible).
 */
bool press_meter_on_frame(PressMeter *m, uint32_t size, bool corrupt, uint64_t first_us,
                          uint64_t arrive_us, PressJump *out);

/* Builds the finished sample from the jump and the decode-done / swap times of the picture that
 * was actually shown. Clamps nothing; order_ok is false when any stage is out of order. */
void press_meter_complete(const PressJump *jump, uint64_t decoded_us, uint64_t shown_us,
                          PressSample *out);

/* Fills *out with the meter's cumulative counters; out->lost is set to 0 (caller fills it). */
void press_meter_counters(const PressMeter *m, PressCounters *out);

/* Per-window figures: now - prev field by field. A field that went down (the session restarted
 * and the counters began again) yields its new value rather than a huge unsigned difference. */
void press_counters_delta(const PressCounters *now, const PressCounters *prev, PressCounters *out);
