/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2016 Ilya Zhuravlev
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include "video.h"
#include "video_overlay.h"
#include "context.h"
#include "ui.h"

#include <chiaki/thread.h>

#include <stdbool.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/display.h>
#include <psp2/videodec.h>
#include <vita2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static void draw_streaming(vita2d_texture *tex);

enum {
  VITA_VIDEO_INIT_OK = 0,
  VITA_VIDEO_ERROR_NO_MEM = 0x80010001,
  VITA_VIDEO_ERROR_INIT_LIB = 0x80010002,
  VITA_VIDEO_ERROR_QUERY_DEC_MEMSIZE = 0x80010003,
  VITA_VIDEO_ERROR_ALLOC_MEM = 0x80010004,
  VITA_VIDEO_ERROR_GET_MEMBASE = 0x80010005,
  VITA_VIDEO_ERROR_CREATE_DEC = 0x80010006,
};

enum {
  SCREEN_WIDTH = 960,
  SCREEN_HEIGHT = 544,
};

enum VideoStatus {
  NOT_INIT,
  INIT_GS,
  INIT_FRAMEBUFFER,
  INIT_AVC_LIB,
  INIT_DECODER_MEMBLOCK,
  INIT_AVC_DEC,
};

/* GH #245: the texture the decoder currently targets ("decode target") AND,
 * whenever it holds the freshest clean frame, the texture presented as the
 * live picture. This pointer VALUE is reassigned only by the UI thread (the
 * ping-pong swap in promote_decoded_frame_to_last_good(), called from
 * vita_video_render_latest_frame()) -- the decode thread never repoints it,
 * it only writes pixels at whatever this currently points to. `volatile` on
 * the pointer itself (not the pointee) publishes those UI-thread writes to
 * the decode thread, which re-reads this under `mtx` before every decode
 * (decode_frame_now()) -- same single-writer/single-reader volatile
 * discipline already used by incoming_frame_corrupt et al. below. No longer
 * `extern`-visible outside this file (grepped: nothing else references it),
 * so this is now file-local. */
static vita2d_texture *volatile frame_texture = NULL;
enum VideoStatus video_status = NOT_INIT;

SceAvcdecCtrl *decoder = NULL;
SceUID decoderblock = -1;
SceUID videodecblock = -1;
SceUID videodecUnmap = -1;
SceUIntVAddr videodecContext = 0;
SceAvcdecQueryDecoderInfo *decoder_info = NULL;

/* GH #245 code review round 3: was plain `bool`, missing the `volatile` this file's
 * own convention requires for a cross-thread flag -- a pre-existing gap (written on
 * the session/stop thread in vita_h264_stop(), read on the decode thread in
 * decode_frame_now()), not introduced by GH #245, but now load-bearing: it is also
 * read on the UI thread at the top of vita_video_render_latest_frame() as the
 * authoritative "has teardown started" gate that keeps that function out of `mtx`
 * once vita_h264_stop() has begun (see that gate's comment, and the comment above
 * chiaki_mutex_fini(&mtx) in vita_h264_stop() for the full ordering invariant). */
static volatile bool active_video_thread = true;
static volatile bool frame_ready_for_display = false;

/* GH #245 code review round 2 (BLOCKING finding, now fixed): UI-thread render
 * quiescence signal for vita_h264_stop(). True for the entire duration of any
 * vita_video_render_latest_frame() call, set/cleared ONLY by the UI thread --
 * same single-writer/single-reader volatile discipline as frame_ready_for_display
 * above, just with the roles reversed (UI writes, vita_h264_stop() -- called on
 * the Chiaki session thread when it handles CHIAKI_EVENT_QUIT synchronously, see
 * host_lifecycle.c host_shutdown_media_pipeline() -- reads/polls).
 *
 * Why this exists: vita_video_render_latest_frame() can call
 * promote_decoded_frame_to_last_good(), which takes `mtx`. vita_h264_stop() calls
 * chiaki_mutex_fini(&mtx) (== sceKernelDeleteMutex on Vita). Deleting a mutex a
 * live thread currently owns (or is about to lock) is kernel UB, not a benign
 * race -- and unlike the pre-fix single-texture race this file already documents
 * a use-after-free history for, this one is NOT bounded by "no decode thread
 * holds mtx" (see the comment on the decode-thread join in vita_h264_stop()):
 * the UI thread is a SEPARATE, third thread that was never accounted for there.
 * `context.stream.is_streaming = false` (set by host_shutdown_media_pipeline()
 * right before calling vita_h264_stop()) is NOT a barrier against this -- it only
 * gates the UI thread's *next* main-loop iteration (ui.c), so the UI thread can
 * already be inside a render call, holding or about to take `mtx`, at the exact
 * moment vita_h264_stop() runs on the session thread. vita_h264_stop() polls this
 * flag (bounded, see UI_RENDER_QUIESCENCE_TIMEOUT_US below) before calling
 * chiaki_mutex_fini(&mtx), so that call only ever runs once the UI thread has
 * provably left this function and cannot be holding or acquiring `mtx`. */
static volatile bool ui_render_in_progress = false;

/* --- Freeze-on-corrupt: last-good frame texture and presentation state ---
 *
 * GH #245: frame_texture and last_good_texture are TWO FIXED ALLOCATIONS whose
 * ROLES ping-pong at runtime -- there is no per-frame copy between them. Both are
 * allocated in video_setup_framebuffer() and freed in video_cleanup_framebuffer().
 * At any moment exactly one of the two pointer variables holds each allocation;
 * a clean frame's presentation (vita_video_render_latest_frame(), via
 * promote_decoded_frame_to_last_good()) SWAPS which variable holds which
 * allocation -- an O(1) pointer exchange, not a ~2 MB sceClibMemcpy. See that
 * function's comment for the full state machine and the concurrency reasoning
 * for why two textures (not three) are sufficient here. */

/* Twin texture holding the last clean decoded frame. Allocated in video_setup_framebuffer(),
 * freed in video_cleanup_framebuffer(), NULL until first clean frame arrives (or permanently
 * NULL if the second allocation failed -- see video_setup_framebuffer()'s degradation path).
 * Same volatile-pointer cross-thread discipline as frame_texture above: only the UI thread
 * (in promote_decoded_frame_to_last_good()) ever reassigns this pointer; the decode thread
 * only ever reads it (under `mtx`, to compute frame_texture's next value indirectly via the
 * swap -- see decode_frame_now()). */
static vita2d_texture *volatile last_good_texture = NULL;

/* Set inside decode_frame_now() on the dedicated decode thread, under `mtx`,
 * after a successful sceAvcdecDecode. Read by vita_video_render_latest_frame() on
 * the UI thread. Single-writer/single-reader on Vita Cortex-A9 — volatile sufficient. */
static volatile bool incoming_frame_corrupt = false;

/* Latency investigation (2026-08): first-packet-arrival timestamp (lib-side
 * chiaki_time_now_monotonic_ms() clock, see videoreceiver.c cur_frame_first_packet_ms) for
 * the frame currently sitting in frame_texture. Set alongside incoming_frame_corrupt in
 * decode_frame_now() under `mtx`; read in vita_video_render_latest_frame() on the UI
 * thread right after vita2d_swap_buffers() to compute true end-to-end frame latency.
 * Same single-writer(decode thread)/single-reader(UI thread) volatile handshake as
 * incoming_frame_corrupt / frame_ready_for_display above -- Cortex-A9 cache coherency
 * makes plain volatile sufficient here, per established project convention. 0 = no valid
 * timestamp (e.g. header-only callback; see chiaki_video_receiver_stream_info() call site
 * in videoreceiver.c which never carries a real frame timestamp). */
static volatile uint64_t incoming_frame_first_packet_ms = 0;

/* PIPE/DISPLAY investigation (2026-08): process-time timestamp (sceKernelGetProcessTimeWide(),
 * same clock as every other stamp in this file) taken the instant decode_frame_now() finishes
 * a successful sceAvcdecDecode for the frame currently sitting in frame_texture. Set alongside
 * incoming_frame_first_packet_ms/incoming_frame_corrupt in decode_frame_now() under `mtx`, on
 * the decode thread; read by vita_video_render_latest_frame() on the UI thread at render entry
 * to measure the "decode done -> UI picked it up" span (see the display_pickup_* fields in
 * stream_state.h). Same single-writer(decode thread)/single-reader(UI thread) volatile
 * handshake as incoming_frame_corrupt / frame_ready_for_display above -- Cortex-A9 cache
 * coherency makes plain volatile sufficient here, per established project convention.
 * 0 = no valid timestamp yet (never decoded a frame this session); the reader must check for
 * this explicitly rather than computing a bogus multi-decade span against process-start. */
static volatile uint64_t incoming_frame_decode_done_us = 0;

/* Consecutive corrupt-frame presentations. Reset on any clean frame.
 * When it reaches FREEZE_MAX_STREAK the freeze is released unconditionally. */
static int frozen_frame_streak = 0;

/* Maximum consecutive frames we will hold a frozen image. At this cap the
 * live (possibly corrupted) frame is presented so the picture always resumes. */
#define FREEZE_MAX_STREAK 8

typedef struct {
  unsigned int texture_width;
  unsigned int texture_height;
  unsigned int source_width;
  unsigned int source_height;
  float origin_x;
  float origin_y;
  float region_x1;
  float region_y1;
  float region_x2;
  float region_y2;
} image_scaling_settings;

static image_scaling_settings image_scaling = {0};

/* Decode/display mutex. Was originally declared much further down this file (right
 * before the SPSC decode queue section); moved up here (GH #245 code review fix) so
 * promote_decoded_frame_to_last_good() below -- which now also takes this lock, see
 * its comment -- doesn't reference it before its declaration. Still the same single
 * mutex used throughout decode_frame_now() further down. */
ChiakiMutex mtx;

/* Promote the just-decoded frame (currently sitting in `frame_texture`) to be the
 * new last-good frame. Runs on the UI thread only, called from
 * vita_video_render_latest_frame() whenever the frame it just picked up is clean.
 *
 * GH #245 ping-pong state machine (replaces the old ~2 MB sceClibMemcpy):
 *   frame_texture and last_good_texture are the two fixed texture allocations from
 *   video_setup_framebuffer(). "Promoting" a clean frame means the texture that was
 *   just decoded (frame_texture) becomes the new last_good_texture, and the OLD
 *   last_good_texture -- a texture nobody is displaying right now -- becomes the new
 *   frame_texture, i.e. the decoder's next write target. That's a 2-pointer exchange,
 *   O(1), zero bytes copied.
 *
 *   Three presentation branches, one decode target each (see
 *   vita_video_render_latest_frame() below for the branch bodies):
 *     - corrupt, streak < cap:  present last_good_texture (held). NOT swapped here --
 *       frame_texture (the corrupt buffer) is left as the decode target, so the
 *       decode thread keeps overwriting the SAME non-last-good texture on every
 *       subsequent corrupt frame in the streak. last_good_texture is never touched,
 *       so the held picture never changes underneath the freeze.
 *     - clean:                 this function runs. frame_texture <-> last_good_texture
 *       swap. The decode thread's next call re-reads frame_texture (now the OLD
 *       last-good) as its target.
 *     - corrupt, cap-release:  present frame_texture directly (whatever decoded, even
 *       though corrupt). NOT swapped -- last_good_texture must only ever hold a
 *       CONFIRMED-clean frame, so a corrupt cap-release frame must never be promoted
 *       into it.
 *   Two textures are sufficient for all three branches because the decode thread
 *   NEVER repoints frame_texture itself (see its declaration comment) -- it only
 *   changes when THIS function swaps it. A third buffer would only add slack for
 *   the cap-release GPU-read race noted below; it is not required for the state
 *   machine itself.
 *
 * Concurrency -- CORRECTED after code review (the first version of this function
 * did not take `mtx` and was BLOCKING-REJECTED: `mtx` held by decode_frame_now()
 * only serializes the single decode thread against itself, which buys nothing
 * against the UI thread; without a lock here, decode_thread_func()'s next iteration
 * -- which has no backpressure on UI consumption, see frame_overwrite_count -- could
 * re-enter decode_frame_now(), re-read the still-unswapped frame_texture, and start
 * sceAvcdecDecode DMA-writing it while draw_streaming() below is having the GPU read
 * the very same buffer. Worse than the pre-fix design too: the corruption would be
 * baked into last_good_texture and redrawn on every subsequent freeze-hold frame
 * until the next clean promote, not just glitch one draw):
 *
 *   This function now takes `mtx` around the swap -- the SAME lock
 *   decode_frame_now() holds for its entire body, from reading frame_texture to set
 *   picture.frame.pPicture[0] through the sceAvcdecDecode call and the metadata
 *   publish. That makes this swap and one full decode_frame_now() call mutually
 *   exclusive; the two possible lock-acquisition orderings are both safe:
 *     - UI wins the race: the swap completes first, so frame_texture already holds
 *       the OTHER (old last-good) buffer by the time decode_frame_now() next
 *       acquires `mtx` and reads it -- decode never touches the buffer UI is about
 *       to draw.
 *     - decode wins the race: decode_frame_now()'s entire critical section (read,
 *       decode, publish) completes and releases `mtx` before this function's lock
 *       attempt can proceed -- so by the time the swap runs and draw_streaming()
 *       is later called, decode's write to that buffer already finished. No
 *       concurrent GPU read vs. decode-thread write is reachable in either
 *       ordering.
 *   The critical section is scoped to the swap alone -- it does NOT extend across
 *   draw_streaming()/vita2d_wait_rendering_done() a few lines below in the caller,
 *   so the GPU wait is never done while holding `mtx`.
 *   This makes the "genuine improvement over pre-fix" claim actually true: pre-fix,
 *   frame_ready_for_display was cleared before drawing and the decode thread could
 *   freely start overwriting the very texture still being drawn on every frame (see
 *   the field comment on frame_ready_for_display); post-fix, that specific hazard is
 *   closed by this lock for the clean-frame path, which is the common case.
 *   Cost / lock-order: this is a deliberate, narrow exception to "the UI thread
 *   reads the texture pointers without mtx" -- every OTHER UI-thread read in this
 *   file (frame_ready_for_display, incoming_frame_corrupt, incoming_frame_*)
 *   remains lock-free, unaffected. The UI thread can now block here for up to one
 *   sceAvcdecDecode call (tracked at runtime via context.stream.decode_avg_us /
 *   decode_max_us) if it loses the race -- negligible against the 30.5ms memcpy
 *   this change removes. No other lock is held by the UI thread at this call site
 *   (it does not touch decode_q_mtx, which is decode-thread-internal and always
 *   released before decode_frame_now() is entered -- see decode_thread_func()), so
 *   there is no lock-order cycle to introduce.
 *
 * The one case this lock does NOT cover: the corrupt/cap-release branch presents
 * frame_texture directly without calling this function at all (by design -- see
 * above), so frame_texture remains the decode thread's target through that draw,
 * completely outside this mutex. If the decode thread starts a new decode while
 * that particular draw's vita2d_wait_rendering_done() has not yet returned, it can
 * still race the GPU read -- identical in kind to the pre-fix race, genuinely
 * narrowed (not eliminated) to the rare cap-release path (FREEZE_MAX_STREAK
 * consecutive corrupt frames) and the startup edge case before the first clean
 * frame, instead of firing on every single frame as it did before this change.
 * Closing it completely would need either extending the lock to that branch too
 * (which, since that branch's draw is fast, would only add a comparably small
 * stall) or a third buffer -- deliberately not implemented here; flagging it
 * rather than papering over it. */
static vita2d_texture *promote_decoded_frame_to_last_good(void) {
  chiaki_mutex_lock(&mtx);
  if (last_good_texture == NULL) {
    /* Degraded mode: the second allocation failed in video_setup_framebuffer().
     * There is nothing to swap into -- keep presenting the sole texture, matching
     * the pre-fix single-texture behavior exactly (freeze suppression stays
     * disabled via the last_good_texture == NULL checks elsewhere in this file). */
    chiaki_mutex_unlock(&mtx);
    return frame_texture;
  }
  vita2d_texture *newly_decoded = frame_texture;
  frame_texture = last_good_texture;
  last_good_texture = newly_decoded;
  vita2d_texture *promoted = last_good_texture;
  chiaki_mutex_unlock(&mtx);
  return promoted;
}

static bool should_drop_frame_for_pacing(void) {
  if (!context.config.force_30fps)
    return false;

  uint32_t target = context.stream.target_fps;
  if (target == 0)
    return false;

  uint32_t source = context.stream.measured_incoming_fps ? context.stream.measured_incoming_fps
                                                         : context.stream.negotiated_fps;
  if (source == 0 || target >= source)
    return false;

  context.stream.pacing_accumulator += target;
  if (context.stream.pacing_accumulator < source)
    return true;

  context.stream.pacing_accumulator -= source;
  return false;
}

static void record_decode_timing_sample(uint32_t decode_elapsed_us) {
  context.stream.decode_time_us = decode_elapsed_us;
  context.stream.decode_window_total_us += decode_elapsed_us;
  if (decode_elapsed_us > context.stream.decode_window_max_us)
    context.stream.decode_window_max_us = decode_elapsed_us;
  context.stream.decode_window_count++;
}

void update_scaling_settings(int width, int height) {
  // Initialize defaults - full screen
  image_scaling.texture_width = SCREEN_WIDTH;
  image_scaling.texture_height = SCREEN_HEIGHT;
  image_scaling.source_width = (unsigned int)width;
  image_scaling.source_height = (unsigned int)height;
  image_scaling.origin_x = 0;
  image_scaling.origin_y = 0;
  image_scaling.region_x1 = 0;
  image_scaling.region_y1 = 0;
  image_scaling.region_x2 = SCREEN_WIDTH;
  image_scaling.region_y2 = SCREEN_HEIGHT;

  // Clamp source region to texture bounds (defensive)
  if (image_scaling.source_width > image_scaling.texture_width)
    image_scaling.source_width = image_scaling.texture_width;
  if (image_scaling.source_height > image_scaling.texture_height)
    image_scaling.source_height = image_scaling.texture_height;

  // Fill Screen mode uses vita2d_draw_texture_scale in draw_streaming()
  // so we only need to calculate aspect-preserving layout here
  if (!context.config.stretch_video) {
    // Aspect-ratio preserving mode - fit video with letterboxing/pillarboxing
    float scale_w = (float)SCREEN_WIDTH / (float)width;
    float scale_h = (float)SCREEN_HEIGHT / (float)height;
    float scale = scale_w < scale_h ? scale_w : scale_h;
    if (scale > 1.0f)
      scale = 1.0f;

    image_scaling.region_x2 = image_scaling.source_width * scale;
    image_scaling.region_y2 = image_scaling.source_height * scale;
    image_scaling.origin_x = round((SCREEN_WIDTH - image_scaling.region_x2) / 2.0f);
    image_scaling.origin_y = round((SCREEN_HEIGHT - image_scaling.region_y2) / 2.0f);
  }

  LOGD("update_scaling_settings: src=%ux%u tex=%ux%u dst=%.0fx%.0f stretch=%s",
       image_scaling.source_width, image_scaling.source_height, image_scaling.texture_width,
       image_scaling.texture_height, image_scaling.region_x2, image_scaling.region_y2,
       context.config.stretch_video ? "true" : "false");
}

/* -----------------------------------------------------------------------
 * SPSC decode queue: producer = Takion recv thread, consumer = decode thread
 * (GH #188: decouple sceAvcdecDecode from the recv thread to fix jitter inflation)
 * ----------------------------------------------------------------------- */

/* 4 ring slots → 3 usable queued frames (full when (tail+1)%N == head) plus 1
 * in-flight slot held by the decode thread while sceAvcdecDecode runs. On a
 * healthy LAN the queue is almost always empty. */
#define DECODE_QUEUE_DEPTH 4
/* 256 KB per slot — headroom for worst-case IDR frames (~60-120 KB) plus the
 * 64-byte pad contract (CHIAKI_VIDEO_BUFFER_PADDING_SIZE, chiaki/video.h:24). */
#define DECODE_SLOT_CAPACITY (256 * 1024)
/* Trailing zero-pad required by sceAvcdecDecode (== CHIAKI_VIDEO_BUFFER_PADDING_SIZE). */
#define DECODE_SLOT_PAD 64

typedef struct {
  uint8_t *data; /* malloc'd once at start, DECODE_SLOT_CAPACITY bytes */
  size_t size;   /* valid compressed-bitstream bytes for this frame */
  bool frame_corrupt;
  /* Latency investigation: lib-side first-packet-arrival timestamp for this frame
   * (chiaki_time_now_monotonic_ms() clock -- see videoreceiver.c cur_frame_first_packet_ms),
   * carried through the queue so end-to-end latency can be measured at display time in
   * vita_video_render_latest_frame(). 0 if unavailable. */
  uint64_t frame_first_packet_ms;
} DecodeSlot;

/* Use a SEPARATE mutex from the existing decode `mtx` so the recv thread's
 * critical section (memcpy + index bump) never contends with the decode mutex
 * while a multi-ms sceAvcdecDecode is in flight. */
static DecodeSlot decode_queue[DECODE_QUEUE_DEPTH];
static size_t decode_q_head = 0;
static size_t decode_q_tail = 0;
static ChiakiMutex decode_q_mtx;
static ChiakiCond decode_q_cond;
static ChiakiThread decode_thread;
static volatile bool decode_thread_should_exit = false;
/* True only when chiaki_thread_create succeeded in vita_h264_start(). Guards the
 * chiaki_thread_join call in vita_h264_stop() so we never join a phantom thread. */
static bool decode_thread_started = false;
/* Frames dropped from the queue before decode (pre-decode drop breaks the DPB
 * reference chain; a post-decode overwrite counted in frame_overwrite_count
 * does not). Should be ~0 on a healthy LAN; exposed in PIPE/FPS for A/B. */
static uint32_t decode_queue_drops = 0;

/* Decode queue occupancy (latency investigation, item 2) — TIME-WEIGHTED, not a naive
 * average of samples taken only at push/pop. Code review (2026-08) caught the original
 * push/pop-sample design as structurally biased: a sample taken right after a push, or
 * right before a pop (before decode_q_head advances), is >=1 by construction every single
 * time, AND no sample is ever taken during the multi-ms sceAvcdecDecode window -- which is
 * precisely when a real backlog would sit. A healthy stream and one genuinely stuck at
 * depth 1 both reported "avg=1", which is the exact question this leading-hypothesis
 * metric exists to answer.
 *
 * Instead this tracks depth as a step function: every time depth actually changes (a push
 * increments it, a head-advance after decode decrements it — both already happen under
 * decode_q_mtx), we close out the interval the PREVIOUS depth was in effect for and add
 * depth*duration to a running area accumulator. avg = area / elapsed at window-close. This
 * correctly attributes the entire sceAvcdecDecode duration to whatever depth was in effect
 * when decode started (which does not change until the post-decode head-advance), instead
 * of missing it entirely. Uses sceKernelGetProcessTimeWide() to match every other
 * timestamp already taken at these call sites (decode_frame_now() timing, etc.) — see the
 * item-1 clock-domain comment in vita_video_render_latest_frame() for why that's the
 * correct choice on this file's side of the boundary. */
static uint64_t decode_q_occ_area_us = 0;          // Σ(depth × duration_us) since window start
static uint64_t decode_q_occ_window_start_us = 0;  // Window anchor for area -> avg normalization
static uint64_t decode_q_occ_last_change_us = 0;   // Timestamp of the last depth transition
static uint32_t decode_q_occ_last_depth = 0;       // Depth in effect since last_change_us
static uint32_t decode_q_occ_max_sample = 0;       // Peak depth observed since window start

/* Record a depth transition (push or post-decode head-advance). Must be called with
 * decode_q_mtx held. Closes out the interval the previous depth held (area += depth *
 * elapsed), then adopts new_depth as the depth in effect going forward. O(1), no
 * allocation, no I/O beyond the one timestamp syscall the caller already needed anyway. */
static void record_decode_queue_depth_locked(uint32_t new_depth, uint64_t now_us) {
  if (decode_q_occ_last_change_us != 0) {
    uint64_t elapsed_us = now_us - decode_q_occ_last_change_us;
    decode_q_occ_area_us += (uint64_t)decode_q_occ_last_depth * elapsed_us;
  }
  decode_q_occ_last_change_us = now_us;
  decode_q_occ_last_depth = new_depth;
  if (new_depth > decode_q_occ_max_sample)
    decode_q_occ_max_sample = new_depth;
}

/* Moved below the decode queue occupancy accumulators above (was originally declared near
 * the top of this file) so its window-close block can reference decode_q_mtx /
 * decode_q_occ_* without a forward-declaration. Only caller is decode_frame_now(),
 * further below, so this move is purely a declaration-order fix. */
static void record_incoming_frame_sample(void) {
  uint64_t now_us = sceKernelGetSystemTimeWide();
  if (context.stream.fps_window_start_us == 0)
    context.stream.fps_window_start_us = now_us;

  context.stream.fps_window_frame_count++;
  if (now_us - context.stream.fps_window_start_us >= 1000000) {
    context.stream.measured_incoming_fps = context.stream.fps_window_frame_count;
    if (context.config.show_latency) {
      uint32_t requested = context.stream.negotiated_fps;
      if (requested == 0)
        requested = 30;
      LOGD("Video FPS — incoming %u fps (requested %u)", context.stream.measured_incoming_fps,
           requested);
    }
    // D1: Publish decode timing window stats
    if (context.stream.decode_window_count > 0) {
      context.stream.decode_avg_us =
          context.stream.decode_window_total_us / context.stream.decode_window_count;
      context.stream.decode_max_us = context.stream.decode_window_max_us;
    } else {
      context.stream.decode_avg_us = 0;
      context.stream.decode_max_us = 0;
    }
    context.stream.decode_window_total_us = 0;
    context.stream.decode_window_max_us = 0;
    context.stream.decode_window_count = 0;

    // Latency investigation (item 2): publish TIME-WEIGHTED decode queue occupancy for
    // this window (see the decode_q_occ_area_us block above for why). Safe to lock
    // decode_q_mtx here: decode_thread_func() always unlocks it before calling
    // decode_frame_now() (which calls this function) and only re-acquires it afterwards
    // to advance decode_q_head -- so decode_q_mtx is guaranteed free at this point, no
    // nested-lock/deadlock risk.
    //
    // CLOCK DOMAIN: this function's own `now_us` above is sceKernelGetSystemTimeWide()
    // (pre-existing, used for the fps window) -- a DIFFERENT clock than the
    // sceKernelGetProcessTimeWide() used for decode_q_occ_last_change_us at the push/pop
    // sites. Mixing them would corrupt the elapsed-time math, so a fresh
    // sceKernelGetProcessTimeWide() read is taken here, scoped only to the queue-depth
    // interval close-out below.
    chiaki_mutex_lock(&decode_q_mtx);
    uint64_t proc_now_us = sceKernelGetProcessTimeWide();
    /* Close out the currently-open depth interval up to this window boundary, so a
     * long-running decode (which produces no push/pop event of its own) still gets
     * attributed to whichever window it overlaps at close time. */
    if (decode_q_occ_last_change_us != 0) {
      uint64_t elapsed_us = proc_now_us - decode_q_occ_last_change_us;
      decode_q_occ_area_us += (uint64_t)decode_q_occ_last_depth * elapsed_us;
      decode_q_occ_last_change_us = proc_now_us;
    }
    uint64_t area_us = decode_q_occ_area_us;
    uint64_t window_elapsed_us =
        decode_q_occ_window_start_us != 0 ? (proc_now_us - decode_q_occ_window_start_us) : 0;
    uint32_t occ_max = decode_q_occ_max_sample;
    decode_q_occ_area_us = 0;
    decode_q_occ_max_sample = decode_q_occ_last_depth;  // Next window starts from "now", not 0
    decode_q_occ_window_start_us = proc_now_us;
    chiaki_mutex_unlock(&decode_q_mtx);
    /* Code review fix: a 4-slot queue's whole interesting range is 0.00-4.00 frames, and
     * the hypothesis under test is a SUB-FRAME persistent depth shift -- plain integer
     * division would floor 1.2 and 1.9 to the same "1", hiding exactly the signal this PR
     * exists to find. Scale by OCC_AVG_FIXED_POINT_SCALE before dividing (multiply in
     * 64-bit: area_us is bounded by ~DECODE_QUEUE_DEPTH * 1e6 per window, so
     * area_us * 100 is comfortably inside uint64_t before the divide). decode_q_occ_avg is
     * therefore fixed-point (see stream_state.h) -- host_metrics.c formats it as X.XX. */
    context.stream.decode_q_occ_avg =
        window_elapsed_us > 0
            ? (uint32_t)((area_us * (uint64_t)OCC_AVG_FIXED_POINT_SCALE) / window_elapsed_us)
            : decode_q_occ_last_depth * OCC_AVG_FIXED_POINT_SCALE;
    context.stream.decode_q_occ_max = occ_max;

    context.stream.fps_window_frame_count = 0;
    context.stream.fps_window_start_us = now_us;
  }
}

typedef struct SceVideodecMemInfo {
  SceUInt32 memSize;

} SceVideodecMemInfo;

typedef struct SceVideodecCtrl {
  SceAvcdecBuf memBuf;
  SceUID memBufUid;

  SceUIntVAddr vaContext;
  SceUInt32 contextSize;
} SceVideodecCtrl;
extern SceInt32 sceVideodecQueryMemSize(SceUInt32 codecType,
                                        const SceVideodecQueryInitInfo *pInitInfo,
                                        SceVideodecMemInfo *pMemInfo);
/* Extended videodec + codec-engine API not yet in VitaSDK headers (GCC 15
 * hard-errors on implicit declarations; symbols exist in stub libs). */
extern SceInt32 sceVideodecInitLibraryWithUnmapMem(SceVideodecType codec, SceVideodecCtrl *libCtrl,
                                                   const SceVideodecQueryInitInfo *initInfo);
extern SceUID sceCodecEngineOpenUnmapMemBlock(void *base, SceUInt32 size);
extern SceInt32 sceCodecEngineCloseUnmapMemBlock(SceUID unmapUid);
extern SceUIntVAddr sceCodecEngineAllocMemoryFromUnmapMemBlock(SceUID unmapUid, SceUInt32 size,
                                                               SceUInt32 alignment);
extern SceInt32 sceCodecEngineFreeMemoryFromUnmapMemBlock(SceUID unmapUid, SceUIntVAddr vaCtx);
extern SceInt32 sceAvcdecDecodeAvailableSize(const SceAvcdecCtrl *decoder);

SceAvcdecAu au = {0};
SceAvcdecArrayPicture array_picture = {0};
struct SceAvcdecPicture picture = {0};
struct SceAvcdecPicture *pictures = {&picture};

static void video_cleanup_decoder(void) {
  if (video_status != INIT_AVC_DEC)
    return;
  sceAvcdecDeleteDecoder(decoder);
  video_status--;
}

static void video_cleanup_decoder_memblock(void) {
  if (video_status != INIT_DECODER_MEMBLOCK)
    return;

  if (decoderblock >= 0) {
    sceKernelFreeMemBlock(decoderblock);
    decoderblock = -1;
  }
  if (decoder != NULL) {
    free(decoder);
    decoder = NULL;
  }
  if (decoder_info != NULL) {
    free(decoder_info);
    decoder_info = NULL;
  }
  video_status--;
}

static void video_cleanup_avc_lib(void) {
  if (video_status != INIT_AVC_LIB)
    return;

  sceVideodecTermLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC);

  if (videodecContext != 0) {
    sceCodecEngineFreeMemoryFromUnmapMemBlock(videodecUnmap, videodecContext);
    videodecContext = 0;
  }

  if (videodecUnmap != -1) {
    sceCodecEngineCloseUnmapMemBlock(videodecUnmap);
    videodecUnmap = -1;
  }

  if (videodecblock != -1) {
    sceKernelFreeMemBlock(videodecblock);
    videodecblock = -1;
  }
  video_status--;
}

static void video_cleanup_framebuffer(void) {
  if (video_status != INIT_FRAMEBUFFER)
    return;
  if (frame_texture != NULL) {
    vita2d_free_texture(frame_texture);
    frame_texture = NULL;
  }
  if (last_good_texture != NULL) {
    vita2d_free_texture(last_good_texture);
    last_good_texture = NULL;
  }
  video_status--;
}

static int video_setup_framebuffer(int width, int height) {
  update_scaling_settings(width, height);
  picture.frame.framePitch = image_scaling.texture_width;
  picture.frame.frameWidth = image_scaling.texture_width;
  picture.frame.frameHeight = image_scaling.texture_height;

  frame_texture =
      vita2d_create_empty_texture_format(image_scaling.texture_width, image_scaling.texture_height,
                                         SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
  if (frame_texture == NULL) {
    LOGD("not enough memory4\n");
    return VITA_VIDEO_ERROR_NO_MEM;
  }
  /* Initial decode target, matching frame_texture's freshly-allocated value above.
   * decode_frame_now() re-reads frame_texture and re-sets this on every subsequent
   * decode (GH #245 ping-pong — frame_texture is repointed at runtime by
   * promote_decoded_frame_to_last_good()), so this assignment only matters for a
   * hypothetical decode that ran before the first ping-pong swap; kept for
   * defensive clarity rather than relying on that per-decode re-set alone. */
  picture.frame.pPicture[0] = vita2d_texture_get_datap(frame_texture);

  /* Allocate the twin "last good frame" texture. Same format and dimensions as
   * frame_texture. No copying happens between them (GH #245) -- clean frames are
   * promoted by swapping which of these two allocations frame_texture/
   * last_good_texture point to (see promote_decoded_frame_to_last_good()).
   * Failure is non-fatal: freeze suppression simply won't engage AND the
   * ping-pong never activates -- decode_frame_now() keeps targeting the sole
   * frame_texture allocation forever (last_good_texture stays NULL). */
  last_good_texture =
      vita2d_create_empty_texture_format(image_scaling.texture_width, image_scaling.texture_height,
                                         SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
  if (last_good_texture == NULL)
    LOGD("VIDEO: last_good_texture alloc failed — freeze suppression disabled\n");

  return VITA_VIDEO_INIT_OK;
}

static int video_setup_avc_lib(int width, int height, SceVideodecCtrl *libCtrl,
                               SceVideodecMemInfo *libMemInfo,
                               SceVideodecQueryInitInfo *initVideodec) {
  void *libMem;
  sceClibMemset(&initVideodec->hwAvc, 0, sizeof(SceVideodecQueryInitInfoHwAvcdec));

  initVideodec->hwAvc.size = sizeof(SceVideodecQueryInitInfoHwAvcdec);
  initVideodec->hwAvc.horizontal = VITA_DECODER_RESOLUTION(width);
  initVideodec->hwAvc.vertical = VITA_DECODER_RESOLUTION(height);
  initVideodec->hwAvc.numOfStreams = 1;
  initVideodec->hwAvc.numOfRefFrames = REF_FRAMES;

  int ret = sceVideodecQueryMemSize(SCE_VIDEODEC_TYPE_HW_AVCDEC, initVideodec, libMemInfo);
  if (ret < 0) {
    sceClibPrintf("sceVideodecQueryMemSize 0x%x\n", ret);
    return VITA_VIDEO_ERROR_INIT_LIB;
  }

  libMemInfo->memSize = ROUND_UP(libMemInfo->memSize, 256 * 1024);

  SceKernelAllocMemBlockOpt opt;
  sceClibMemset(&opt, 0, sizeof(SceKernelAllocMemBlockOpt));
  opt.size = sizeof(SceKernelAllocMemBlockOpt);
  opt.attr = 4;
  opt.alignment = 256 * 1024;

  videodecblock = sceKernelAllocMemBlock("videodec", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                                         libMemInfo->memSize, &opt);
  if (videodecblock < 0) {
    sceClibPrintf("videodecblock: 0x%08x\n", videodecblock);
    return VITA_VIDEO_ERROR_INIT_LIB;
  }

  ret = sceKernelGetMemBlockBase(videodecblock, &libMem);
  if (ret < 0) {
    sceClibPrintf("sceKernelGetMemBlockBase: 0x%x\n", ret);
    return VITA_VIDEO_ERROR_INIT_LIB;
  }

  videodecUnmap = sceCodecEngineOpenUnmapMemBlock(libMem, libMemInfo->memSize);
  if (videodecUnmap < 0) {
    sceClibPrintf("sceCodecEngineOpenUnmapMemBlock: 0x%x\n", videodecUnmap);
    return VITA_VIDEO_ERROR_INIT_LIB;
  }

  videodecContext =
      sceCodecEngineAllocMemoryFromUnmapMemBlock(videodecUnmap, libMemInfo->memSize, 256 * 1024);
  if (videodecContext < 0) {
    sceClibPrintf("sceCodecEngineAllocMemoryFromUnmapMemBlock: 0x%x\n", videodecContext);
    return VITA_VIDEO_ERROR_INIT_LIB;
  }

  sceClibMemset(libCtrl, 0, sizeof(SceVideodecCtrl));
  libCtrl->vaContext = videodecContext;
  libCtrl->contextSize = libMemInfo->memSize;

  ret = sceVideodecInitLibraryWithUnmapMem(SCE_VIDEODEC_TYPE_HW_AVCDEC, libCtrl, initVideodec);
  if (ret < 0) {
    LOGD("sceVideodecInitLibrary 0x%x\n", ret);
    return VITA_VIDEO_ERROR_INIT_LIB;
  }

  return VITA_VIDEO_INIT_OK;
}

static int video_setup_decoder_memblock(const SceVideodecQueryInitInfo *initVideodec) {
  if (decoder_info == NULL) {
    decoder_info = calloc(1, sizeof(SceAvcdecQueryDecoderInfo));
    if (decoder_info == NULL) {
      LOGD("not enough memory2\n");
      return VITA_VIDEO_ERROR_NO_MEM;
    }
  }
  decoder_info->horizontal = initVideodec->hwAvc.horizontal;
  decoder_info->vertical = initVideodec->hwAvc.vertical;
  decoder_info->numOfRefFrames = initVideodec->hwAvc.numOfRefFrames;

  SceAvcdecDecoderInfo decoder_info_out = (SceAvcdecDecoderInfo){0};
  int ret =
      sceAvcdecQueryDecoderMemSize(SCE_VIDEODEC_TYPE_HW_AVCDEC, decoder_info, &decoder_info_out);
  if (ret < 0) {
    LOGD("sceAvcdecQueryDecoderMemSize 0x%x size 0x%x\n", ret, decoder_info_out.frameMemSize);
    return VITA_VIDEO_ERROR_QUERY_DEC_MEMSIZE;
  }

  decoder = calloc(1, sizeof(SceAvcdecCtrl));
  if (decoder == NULL) {
    LOGD("not enough memory3\n");
    return VITA_VIDEO_ERROR_ALLOC_MEM;
  }

  decoder->frameBuf.size = decoder_info_out.frameMemSize;
  LOGD("allocating size 0x%x\n", decoder_info_out.frameMemSize);
  SceKernelAllocMemBlockOpt opt;
  sceClibMemset(&opt, 0, sizeof(SceKernelAllocMemBlockOpt));
  opt.size = sizeof(SceKernelAllocMemBlockOpt);
  opt.attr = 4;
  opt.alignment = 1024 * 1024;
  decoderblock = sceKernelAllocMemBlock("decoder", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                                        decoder_info_out.frameMemSize, &opt);
  if (decoderblock < 0) {
    LOGD("decoderblock: 0x%08x\n", decoderblock);
    return VITA_VIDEO_ERROR_ALLOC_MEM;
  }

  ret = sceKernelGetMemBlockBase(decoderblock, &decoder->frameBuf.pBuf);
  if (ret < 0) {
    LOGD("sceKernelGetMemBlockBase: 0x%x\n", ret);
    return VITA_VIDEO_ERROR_GET_MEMBASE;
  }

  return VITA_VIDEO_INIT_OK;
}

static int video_setup_decoder_instance(void) {
  LOGD("base: 0x%08x\n", decoder->frameBuf.pBuf);
  int ret = sceAvcdecCreateDecoder(SCE_VIDEODEC_TYPE_HW_AVCDEC, decoder, decoder_info);
  if (ret < 0) {
    LOGD("sceAvcdecCreateDecoder 0x%x\n", ret);
    return VITA_VIDEO_ERROR_CREATE_DEC;
  }
  return VITA_VIDEO_INIT_OK;
}

void vita_h264_cleanup() {
  video_cleanup_decoder();
  video_cleanup_decoder_memblock();
  video_cleanup_avc_lib();
  video_cleanup_framebuffer();

  if (video_status == INIT_GS) {
    // gs_sps_stop();
    video_status--;
  }
}

int vita_h264_setup(int width, int height) {
  int ret = VITA_VIDEO_INIT_OK;
  LOGD("vita video setup\n");
  SceVideodecCtrl libCtrl;
  SceVideodecMemInfo libMemInfo;
  SceVideodecQueryInitInfo initVideodec;

  array_picture.numOfElm = 1;
  array_picture.pPicture = &pictures;
  picture.size = sizeof(picture);
  picture.frame.pixelType = SCE_AVCDEC_PIXELFORMAT_RGBA8888;

  au.dts.lower = 0xFFFFFFFF;
  au.dts.upper = 0xFFFFFFFF;
  au.pts.lower = 0xFFFFFFFF;
  au.pts.upper = 0xFFFFFFFF;

  if (video_status == NOT_INIT) {
    // INIT_GS
    // gs_sps_init(width, height);
    video_status++;
  }

  if (video_status == INIT_GS) {
    ret = video_setup_framebuffer(width, height);
    if (ret != VITA_VIDEO_INIT_OK)
      goto cleanup;
    video_status++;
  }

  if (video_status == INIT_FRAMEBUFFER) {
    ret = video_setup_avc_lib(width, height, &libCtrl, &libMemInfo, &initVideodec);
    if (ret != VITA_VIDEO_INIT_OK)
      goto cleanup;
    video_status++;
  }

  if (video_status == INIT_AVC_LIB) {
    ret = video_setup_decoder_memblock(&initVideodec);
    if (ret != VITA_VIDEO_INIT_OK)
      goto cleanup;
    video_status++;
  }

  if (video_status == INIT_DECODER_MEMBLOCK) {
    ret = video_setup_decoder_instance();
    if (ret != VITA_VIDEO_INIT_OK)
      goto cleanup;
    video_status++;
  }

  return VITA_VIDEO_INIT_OK;

cleanup:
  vita_h264_cleanup();
  return ret;
}

/* Performs the actual sceAvcdecDecode synchronously. Called only on the
 * dedicated decode thread (GH #188). buf must be a stable DECODE_SLOT_CAPACITY
 * allocation (not the borrowed frame_buf pointer from videoreceiver). */
static int decode_frame_now(uint8_t *buf, size_t buf_size, bool frame_corrupt,
                            uint64_t frame_first_packet_ms) {
  chiaki_mutex_lock(&mtx);

  if (buf_size > (size_t)sceAvcdecDecodeAvailableSize(decoder)) {
    sceClibPrintf("Video decode buffer too small\n");
    chiaki_mutex_unlock(&mtx);
    return 1;
  }

  int ret = 0;
  au.es.pBuf = buf;
  au.es.size = buf_size;
  /* GH #245 ping-pong: re-read frame_texture and repoint the decoder's output
   * buffer at it on every decode, under `mtx`, immediately before the call that
   * writes into it. frame_texture may have been swapped by the UI thread
   * (promote_decoded_frame_to_last_good(), called from
   * vita_video_render_latest_frame()) since the previous decode -- see that
   * function's comment for the full state machine and why re-reading here
   * (rather than trusting a value cached from a previous call) is required for
   * correctness, and frame_texture's own declaration comment for why this
   * cross-thread read needs no additional synchronization beyond the volatile
   * qualifier already on that pointer. */
  picture.frame.pPicture[0] = vita2d_texture_get_datap(frame_texture);
  uint64_t decode_start_us = sceKernelGetProcessTimeWide();
  ret = sceAvcdecDecode(decoder, &au, &array_picture);
  uint64_t decode_end_us = sceKernelGetProcessTimeWide();
  uint32_t decode_elapsed_us = (uint32_t)(decode_end_us - decode_start_us);
  record_decode_timing_sample(decode_elapsed_us);
  if (context.stream.first_decode_frame_count < 30) {
    context.stream.first_decode_frame_count++;
    LOGD("PIPE/DECODE n=%u us=%u", context.stream.first_decode_frame_count, decode_elapsed_us);
  }
  if (ret < 0) {
    LOGD("sceAvcdecDecode (len=0x%x): 0x%x numOfOutput %d\n", (unsigned int)buf_size, ret,
         array_picture.numOfOutput);
    chiaki_mutex_unlock(&mtx);
    return 0;
  }

  if (array_picture.numOfOutput != 1) {
    LOGD("numOfOutput %d bufSize 0x%x\n", array_picture.numOfOutput, (unsigned int)buf_size);
    chiaki_mutex_unlock(&mtx);
    return 0;
  }
  // Signal the UI thread that a new frame is ready for display.
  // The UI thread owns all vita2d rendering, which decouples the GPU wait
  // from the Takion network receive path and eliminates ~15-20ms of blocking.
  if (active_video_thread) {
    record_incoming_frame_sample();
    /* Atomically tie the corruption flag to this decoded frame while we still
     * hold the mutex. This prevents the flag from mismatching the pixels under
     * frame-overwrite scenarios. The last-good promotion (GH #245: an O(1)
     * frame_texture/last_good_texture pointer swap, no copy) happens on the UI
     * thread in vita_video_render_latest_frame() so the decode thread is never
     * stalled by it. */
    incoming_frame_corrupt = frame_corrupt;
    incoming_frame_first_packet_ms = frame_first_packet_ms;
    // PIPE/DISPLAY: stamp decode-done for the pickup_us span (see field comment above).
    incoming_frame_decode_done_us = sceKernelGetProcessTimeWide();
    // D5: Count frames overwritten before display consumed them
    if (frame_ready_for_display)
      context.stream.frame_overwrite_count++;
    frame_ready_for_display = true;
  } else {
    LOGD("inactive video thread");
  }

  chiaki_mutex_unlock(&mtx);
  return 0;
}

int vita_h264_decode_frame(uint8_t *buf, size_t buf_size, bool frame_corrupt,
                           uint64_t frame_first_packet_ms) {
  /* Early validation — reject garbage before touching the queue. */
  if (buf == NULL || buf_size == 0) {
    LOGD("VIDEO: Invalid frame (NULL or zero size), skipping");
    return 1;
  }
  if (buf_size < 5) {
    LOGD("VIDEO: Frame too small (%zu bytes), possibly corrupted, skipping", buf_size);
    return 1;
  }
  /* Guard against oversized frames that would overflow the slot (should never
   * occur at 540p, but defensive). Reserve DECODE_SLOT_PAD bytes for sceAvcdecDecode. */
  if (buf_size > DECODE_SLOT_CAPACITY - DECODE_SLOT_PAD) {
    LOGD("VIDEO: Frame too large for slot (%zu > %d), dropping", buf_size,
         DECODE_SLOT_CAPACITY - DECODE_SLOT_PAD);
    decode_queue_drops++;
    return 0;
  }

  chiaki_mutex_lock(&decode_q_mtx);

  /* Backpressure: if the ring is full, wait briefly for the decode thread to
   * free a slot. Bounding the wait (<< one frame interval) prevents a stalled
   * decoder from wedging the recv thread indefinitely.
   *
   * IMPORTANT: dropping a compressed P-frame BEFORE decode breaks the HW DPB
   * reference chain (→ macroblocking until next IDR), which is worse than the
   * post-decode overwrite counted in frame_overwrite_count. So we prefer to
   * block briefly rather than immediately drop. */
  if (((decode_q_tail + 1) % DECODE_QUEUE_DEPTH) == decode_q_head) {
    /* Queue full — wait up to 10 ms for the consumer to free a slot. */
    chiaki_cond_timedwait(&decode_q_cond, &decode_q_mtx, 10);
  }

  if (((decode_q_tail + 1) % DECODE_QUEUE_DEPTH) == decode_q_head) {
    /* Still full after timeout — drop the oldest slot to make room. */
    LOGD("VIDEO: decode queue full, dropping oldest frame (drops=%u)", decode_queue_drops + 1);
    decode_queue_drops++;
    decode_q_head = (decode_q_head + 1) % DECODE_QUEUE_DEPTH;
  }

  /* Copy the bitstream into the queue slot. buf points into frame_buf (the
   * frame-processor's single reused allocation). After this callback returns,
   * frame_buf is overwritten or freed, so we MUST copy here. Also capture
   * frame_corrupt now: frames_lost is reset in videoreceiver.c right after
   * the callback returns. Zero the trailing DECODE_SLOT_PAD bytes required by
   * sceAvcdecDecode (== CHIAKI_VIDEO_BUFFER_PADDING_SIZE). */
  DecodeSlot *slot = &decode_queue[decode_q_tail];
  sceClibMemcpy(slot->data, buf, buf_size);
  sceClibMemset(slot->data + buf_size, 0, DECODE_SLOT_PAD);
  slot->size = buf_size;
  slot->frame_corrupt = frame_corrupt;
  slot->frame_first_packet_ms = frame_first_packet_ms;
  decode_q_tail = (decode_q_tail + 1) % DECODE_QUEUE_DEPTH;
  /* Latency investigation (item 2): depth just changed (increased) — record the
   * transition for the time-weighted occupancy average while still holding decode_q_mtx.
   * sceKernelGetProcessTimeWide() to match the clock domain used at the other transition
   * site (post-decode head-advance in decode_thread_func()) and the window-close reduce
   * in record_incoming_frame_sample(). */
  record_decode_queue_depth_locked(
      (uint32_t)((decode_q_tail + DECODE_QUEUE_DEPTH - decode_q_head) % DECODE_QUEUE_DEPTH),
      sceKernelGetProcessTimeWide());

  chiaki_cond_signal(&decode_q_cond);
  chiaki_mutex_unlock(&decode_q_mtx);
  return 0;
}

static void draw_streaming(vita2d_texture *tex) {
  // ui is still rendering in the background, clear the screen first
  vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, RGBA8(0, 0, 0, 255));

  float src_w = (float)image_scaling.source_width;
  float src_h = (float)image_scaling.source_height;
  if (src_w <= 0.0f || src_h <= 0.0f) {
    static uint64_t last_invalid_source_log_us = 0;
    uint64_t now_us = sceKernelGetProcessTimeWide();
    if (last_invalid_source_log_us == 0 || (now_us - last_invalid_source_log_us) >= 1000000ULL) {
      LOGD("draw_streaming skipped invalid source dimensions (w=%.1f h=%.1f)", src_w, src_h);
      last_invalid_source_log_us = now_us;
    }
    return;
  }

  if (context.config.stretch_video) {
    // Fill Screen: scale active decoded source region to full display
    float scale_x = (float)SCREEN_WIDTH / src_w;
    float scale_y = (float)SCREEN_HEIGHT / src_h;
    vita2d_draw_texture_part_scale(tex, 0.0f, 0.0f, 0.0f, 0.0f, src_w, src_h, scale_x, scale_y);
  } else {
    // Aspect-preserving: draw active source region centered with computed scale
    float scale_x = image_scaling.region_x2 / src_w;
    float scale_y = image_scaling.region_y2 / src_h;
    vita2d_draw_texture_part_scale(tex, image_scaling.origin_x, image_scaling.origin_y, 0.0f, 0.0f,
                                   src_w, src_h, scale_x, scale_y);
  }
}

/* PIPE/DISPLAY: record one stage-span sample (microseconds) into a display-pipeline ring,
 * overwrite-oldest on wrap. Same idiom as the PIPE/LATENCY ring below (see its comment at the
 * `latency_window_samples_ms` write site) -- dropping the newest sample instead would hide
 * exactly the late-window stall this instrumentation exists to catch. Factored into one helper
 * (rather than four inlined copies) since vita_video_render_latest_frame() below writes to four
 * structurally-identical rings per call. UI-thread-only caller, so no locking is needed -- see
 * the PIPE/DISPLAY field comment in stream_state.h. */
static void record_display_stage_sample(uint32_t *samples, uint32_t *sample_count,
                                        uint32_t *write_idx, uint32_t *dropped_count,
                                        uint64_t elapsed_us) {
  uint32_t clamped_us = (uint32_t)(elapsed_us > UINT32_MAX ? UINT32_MAX : elapsed_us);
  uint32_t idx = *write_idx;
  samples[idx] = clamped_us;
  *write_idx = (idx + 1) % LATENCY_WINDOW_SAMPLE_CAP;
  if (*sample_count < LATENCY_WINDOW_SAMPLE_CAP) {
    (*sample_count)++;
  } else {
    (*dropped_count)++;
  }
}

bool vita_video_render_latest_frame(void) {
  /* GH #245 code review round 2: mark entry before any early return, cleared before
   * every return below (mirrors the file's existing lock-then-unlock-before-each-return
   * style in decode_frame_now()). See ui_render_in_progress's declaration comment --
   * this is the flag vita_h264_stop() polls before destroying `mtx`. */
  ui_render_in_progress = true;
  /* GH #245 code review round 3: explicit, load-bearing teardown gate. This is the
   * ACTUAL thing that keeps this function from ever reaching `mtx` (via
   * promote_decoded_frame_to_last_good() below) once vita_h264_stop() has begun --
   * not an incidental side effect of the frame_ready_for_display check right below
   * it. vita_h264_stop() clears active_video_thread FIRST, before joining the decode
   * thread and before polling ui_render_in_progress, so this check is guaranteed to
   * see teardown-has-started by the time this function can next be entered after
   * that clear is visible. See active_video_thread's declaration comment and the
   * ordering invariant documented above chiaki_mutex_fini(&mtx) in vita_h264_stop(). */
  if (!active_video_thread) {
    ui_render_in_progress = false;
    return false;
  }
  if (!frame_ready_for_display) {
    ui_render_in_progress = false;
    return false;
  }

  frame_ready_for_display = false;
  /* Latency investigation (item 1): snapshot once, before either branch below, so the
   * paced-drop path (which never swaps buffers, and therefore never has a real display
   * timestamp) and the display path both see the same frame's timestamp consistently. */
  uint64_t frame_first_packet_ms_snapshot = incoming_frame_first_packet_ms;
  /* PIPE/DISPLAY: snapshot decode-done and read the render-entry clock together, before
   * should_drop_frame_for_pacing(), for the same reason as the snapshot above -- the
   * paced-drop path returns early and both this function's callers must see a consistent
   * "when did the UI thread pick this frame up" instant regardless of which path is taken. */
  uint64_t decode_done_us_snapshot = incoming_frame_decode_done_us;
  uint64_t render_entry_us = sceKernelGetProcessTimeWide();
  /* Cross-thread read (decode thread writes, UI thread reads) -- unlike the same-thread
   * sequential reads later in this function, this one needs the same defensive
   * clock-read-out-of-order guard used by feedback_sender_record_input_latency()
   * (lib/src/feedbacksender.c) rather than being assumed monotonic by construction.
   * 0 means no frame has ever been decoded this session; skip rather than compute a
   * nonsense multi-decade span against process start. */
  bool pickup_valid = decode_done_us_snapshot != 0 && render_entry_us >= decode_done_us_snapshot;
  if (pickup_valid) {
    record_display_stage_sample(
        context.stream.display_pickup_samples_us, &context.stream.display_pickup_sample_count,
        &context.stream.display_pickup_write_idx, &context.stream.display_pickup_dropped_count,
        render_entry_us - decode_done_us_snapshot);
  }

  bool drop_frame = should_drop_frame_for_pacing();
  if (drop_frame) {
    // Frame is paced out but still consumed — advance freeze state so the cap
    // counts all consumed frames, not just displayed ones.
    bool corrupt = incoming_frame_corrupt;
    if (corrupt && last_good_texture != NULL && frozen_frame_streak < FREEZE_MAX_STREAK) {
      frozen_frame_streak++;
      context.stream.freeze_engaged_count++;
    } else if (!corrupt) {
      // Clean paced-drop: still promote this frame to last-good (no draw happens on
      // this path, so the swap is unconditionally safe -- there is no concurrent GPU
      // read of either texture to race against here).
      if (frozen_frame_streak > 0) {
        LOGD("PIPE/FREEZE cleared streak=%d (paced)", frozen_frame_streak);
      }
      frozen_frame_streak = 0;
      (void)promote_decoded_frame_to_last_good();
    } else {
      /* corrupt + cap-release or no snapshot: mirror the non-paced cap-release path */
      if (frozen_frame_streak > 0)
        LOGD("PIPE/FREEZE cap-released streak=%d (paced)", frozen_frame_streak);
      frozen_frame_streak = 0;
    }
    // PIPE/DISPLAY: pickup_us was already recorded above (real regardless of pacing);
    // snapshot/draw/swap never happen on this path (no buffer swap occurs), so recording
    // zeros for them would corrupt those rings' percentiles with fake zero-latency
    // samples. Track this as a distinct counter instead.
    context.stream.display_paced_count++;
    ui_render_in_progress = false;
    return true;  // consumed the frame but skipped display
  }

  /* Determine which texture to present.
   *
   * If the incoming frame is flagged corrupt AND we have a clean snapshot AND
   * we haven't held the freeze beyond FREEZE_MAX_STREAK, show the last good
   * frame instead. At the cap, fall through to present whatever decoded — this
   * guarantees the picture always resumes even under sustained loss.
   *
   * The corruption flag is updated under mtx inside decode_frame_now() on the
   * decode thread, so it is always consistent with the pixels in frame_texture
   * when we read it here. The clean branch below promotes it to last-good via an
   * O(1) pointer swap (GH #245) -- see promote_decoded_frame_to_last_good(). */
  bool corrupt = incoming_frame_corrupt;
  vita2d_texture *present_texture = frame_texture;

  if (corrupt && last_good_texture != NULL && frozen_frame_streak < FREEZE_MAX_STREAK) {
    frozen_frame_streak++;
    context.stream.freeze_engaged_count++;
    if (frozen_frame_streak == 1)
      LOGD("PIPE/FREEZE engaged streak=%d", frozen_frame_streak);
    present_texture = last_good_texture;
  } else if (!corrupt) {
    /* Clean frame — promote it to last-good here on the UI thread, BEFORE drawing
     * (not after): the swap must land before draw_streaming() below submits the GPU
     * draw call, so that by the time the decode thread re-reads frame_texture (its
     * next write target) it already sees the OTHER, unrelated texture rather than
     * the one currently being drawn. See promote_decoded_frame_to_last_good() for
     * the full ordering argument. */
    if (frozen_frame_streak > 0) {
      LOGD("PIPE/FREEZE cleared streak=%d", frozen_frame_streak);
      frozen_frame_streak = 0;
    }
    present_texture = promote_decoded_frame_to_last_good();
  } else {
    /* corrupt && (last_good_texture == NULL || streak >= FREEZE_MAX_STREAK) */
    if (frozen_frame_streak >= FREEZE_MAX_STREAK)
      LOGD("PIPE/FREEZE cap-released streak=%d", frozen_frame_streak);
    frozen_frame_streak = 0;
    present_texture = frame_texture;
  }

  /* PIPE/DISPLAY: read here, right after the if/else chain above, NOT inside any one
   * branch -- the corrupt/freeze and cap-release branches skip the promotion swap
   * (so their snapshot_us is genuinely ~0, a real measurement worth keeping; the clean
   * branch's swap is also ~0 now that it's a pointer exchange rather than a ~2 MB copy
   * -- this stage should read near-zero across all three branches post-fix), but the
   * boundary itself must land at the same point in all three branches for draw_us below to
   * mean the same thing regardless of which branch was taken. */
  uint64_t after_snapshot_us = sceKernelGetProcessTimeWide();

  vita2d_start_drawing();

  draw_streaming(present_texture);
  vitavideo_overlay_render();

  vita2d_end_drawing();

  /* PIPE/DISPLAY: vita2d command submission (draw_streaming + overlay + end_drawing) is
   * done; wait_rendering_done()/swap_buffers() below is the GPU-wait + swap span. */
  uint64_t after_draw_us = sceKernelGetProcessTimeWide();

  vita2d_wait_rendering_done();
  vita2d_swap_buffers();

  // D7: Track actual frames rendered to screen per second
  uint64_t now_us = sceKernelGetProcessTimeWide();
  // PIPE/DISPLAY: now_us above is already "right after vita2d_swap_buffers()" -- reused
  // as the swap-span endpoint instead of taking a redundant fifth clock read.
  record_display_stage_sample(
      context.stream.display_snapshot_samples_us, &context.stream.display_snapshot_sample_count,
      &context.stream.display_snapshot_write_idx, &context.stream.display_snapshot_dropped_count,
      after_snapshot_us - render_entry_us);
  record_display_stage_sample(
      context.stream.display_draw_samples_us, &context.stream.display_draw_sample_count,
      &context.stream.display_draw_write_idx, &context.stream.display_draw_dropped_count,
      after_draw_us - after_snapshot_us);
  record_display_stage_sample(context.stream.display_swap_samples_us,
                              &context.stream.display_swap_sample_count,
                              &context.stream.display_swap_write_idx,
                              &context.stream.display_swap_dropped_count, now_us - after_draw_us);
  {
    if (context.stream.display_fps_window_start_us == 0)
      context.stream.display_fps_window_start_us = now_us;
    context.stream.display_frame_count++;
    if (now_us - context.stream.display_fps_window_start_us >= 1000000) {
      context.stream.display_fps = context.stream.display_frame_count;
      context.stream.display_frame_count = 0;
      context.stream.display_fps_window_start_us = now_us;
    }
  }

  // Latency investigation (item 1): true end-to-end frame latency, first-packet-arrival
  // to on-screen swap.
  //
  // CLOCK DOMAIN: cur_frame_first_packet_ms (lib/src/videoreceiver.c) is stamped via
  // chiaki_time_now_monotonic_ms(), which on Vita (lib/src/time.c) resolves to
  // sceKernelGetProcessTime() -- "process time" in microseconds, monotonic, since process
  // start. now_us above is read via sceKernelGetProcessTimeWide() -- per the VitaSDK, the
  // "Wide" entry point reads the exact same process-time clock, just returning a plain
  // SceUInt64 instead of the SceKernelSysClock the narrow API uses; every other file under
  // vita/src/ already standardizes on the Wide variant for this reason (see host_metrics.c,
  // host_feedback.c, ui.c, etc.), while lib/ (shared with non-Vita platforms) goes through
  // the portable chiaki_time_now_monotonic_us() wrapper around the narrow API. They are NOT
  // two different clocks needing an epoch/offset conversion -- they are the same underlying
  // monotonic counter reached via two different VitaSDK entry points. The only real
  // conversion needed is therefore a UNIT conversion (us -> ms, matching the ms granularity
  // cur_frame_first_packet_ms already uses), done explicitly right here at the boundary
  // where the timestamp crosses from "carried through the lib+decode-queue pipeline" to
  // "compared against a fresh vita-side read".
  if (frame_first_packet_ms_snapshot > 0) {
    uint64_t now_ms = now_us / 1000;
    if (now_ms >= frame_first_packet_ms_snapshot) {
      uint64_t latency_ms = now_ms - frame_first_packet_ms_snapshot;
      uint32_t clamped_ms = (uint32_t)(latency_ms > UINT32_MAX ? UINT32_MAX : latency_ms);
      /* Code review fix: this window's live view is a ring buffer, not a fill-once array.
       * The window boundary (refresh_rtt in host_metrics.c) is a "check once per UI pass"
       * gate, not a hard timer, so real windows commonly run past LATENCY_WINDOW_SAMPLE_CAP
       * worth of frames -- and dropping the NEWEST samples past the cap discards exactly
       * the late-window spike a post-event latency step would show up as. Always write;
       * once full, overwrite the oldest slot instead. latency_window_dropped_count now
       * counts overwrites (i.e. "this window ran long enough to wrap"), not "sample lost
       * with no record at all" -- host_metrics.c publishes it per-window (latency_dropped_n)
       * precisely so a truncated window is visible instead of silently reporting stale data. */
      uint32_t write_idx = context.stream.latency_window_write_idx;
      context.stream.latency_window_samples_ms[write_idx] = clamped_ms;
      context.stream.latency_window_write_idx = (write_idx + 1) % LATENCY_WINDOW_SAMPLE_CAP;
      if (context.stream.latency_window_sample_count < LATENCY_WINDOW_SAMPLE_CAP) {
        context.stream.latency_window_sample_count++;
      } else {
        context.stream.latency_window_dropped_count++;
      }
    }
  }

  ui_render_in_progress = false;
  return true;
}

/* Decode thread: pops compressed frames from the SPSC queue and calls
 * decode_frame_now(). Pinned to USER_1 so decode no longer competes with
 * the recv thread (USER_0) or audio (USER_2) for CPU time. */
static void *decode_thread_func(void *user) {
  (void)user;
#ifdef __PSVITA__
  /* Pin to USER_1 — recv is USER_0, audio is USER_2. */
  sceKernelChangeThreadPriority(SCE_KERNEL_THREAD_ID_SELF, VITA_DECODE_THREAD_PRIORITY);
  sceKernelChangeThreadCpuAffinityMask(SCE_KERNEL_THREAD_ID_SELF, SCE_KERNEL_CPU_MASK_USER_1);
#endif
  LOGD("VIDEO: decode thread started (USER_1)");

  for (;;) {
    chiaki_mutex_lock(&decode_q_mtx);
    /* Wait for work or exit signal. Drain the queue before honouring exit
     * so in-flight frames are decoded in order. */
    while (!decode_thread_should_exit && decode_q_head == decode_q_tail)
      chiaki_cond_wait(&decode_q_cond, &decode_q_mtx);

    if (decode_thread_should_exit && decode_q_head == decode_q_tail) {
      chiaki_mutex_unlock(&decode_q_mtx);
      break;
    }

    /* Pop: capture all fields needed, but do NOT advance head yet.
     * Keeping head unchanged reserves this slot until decode completes —
     * the producer's full-check naturally excludes it from reuse. */
    size_t popped_idx = decode_q_head;
    uint8_t *frame_data = decode_queue[popped_idx].data;
    size_t frame_size = decode_queue[popped_idx].size;
    bool corrupt = decode_queue[popped_idx].frame_corrupt;
    uint64_t frame_first_packet_ms = decode_queue[popped_idx].frame_first_packet_ms;
    /* Latency investigation (item 2): depth does NOT change here — head is deliberately
     * held back until decode completes (see comment above) — so there is nothing to
     * record at this point. The depth transition this slot's occupancy contributed
     * happened at push time and is recorded at the head-advance below once decode is
     * actually done; that is what makes the average time-weighted across the whole
     * sceAvcdecDecode duration instead of blind to it. */
    chiaki_mutex_unlock(&decode_q_mtx);

    /* Decode the frame. The slot buffer is exclusively ours until we advance
     * decode_q_head below — the producer will block or drop-oldest on the
     * preceding slots rather than overwriting this one. */
    decode_frame_now(frame_data, frame_size, corrupt, frame_first_packet_ms);

    /* Release the slot now that decode is done. Signal any blocked producer. */
    chiaki_mutex_lock(&decode_q_mtx);
    decode_q_head = (decode_q_head + 1) % DECODE_QUEUE_DEPTH;
    /* Latency investigation (item 2): depth just changed (decreased) — record the
     * transition. This is the point that finally attributes the full decode duration to
     * the depth that was in effect while sceAvcdecDecode ran, closing the blind spot the
     * old push/pop-only sampling had. */
    record_decode_queue_depth_locked(
        (uint32_t)((decode_q_tail + DECODE_QUEUE_DEPTH - decode_q_head) % DECODE_QUEUE_DEPTH),
        sceKernelGetProcessTimeWide());
    chiaki_cond_signal(&decode_q_cond);
    chiaki_mutex_unlock(&decode_q_mtx);
  }

  LOGD("VIDEO: decode thread exiting");
  return NULL;
}

void vita_h264_start() {
  active_video_thread = true;
  chiaki_mutex_init(&mtx, false);
  vita2d_set_vblank_wait(false);
  frame_ready_for_display = false;
  incoming_frame_corrupt = false;
  incoming_frame_first_packet_ms = 0;
  incoming_frame_decode_done_us = 0;
  frozen_frame_streak = 0;
  /* GH #245 code review round 2: no render call is in flight at session start --
   * matches the same "reset to the idle value" treatment as frame_ready_for_display
   * above. See the declaration comment for the cross-thread invariant this flag
   * establishes with vita_h264_stop(). */
  ui_render_in_progress = false;
  /* frame_texture/last_good_texture (GH #245 ping-pong roles) are deliberately NOT
   * reset here. vita_h264_setup() -> video_setup_framebuffer() always runs
   * immediately before this function (see host.c) and unconditionally reassigns
   * both to freshly-allocated textures, which already establishes the canonical
   * starting roles for a new session. There is no code path today where
   * vita_h264_start() runs without a preceding vita_h264_setup() call, so no
   * separate reset is needed here -- but if that pairing ever changes, this
   * comment is the tripwire to come back and add one. */
  context.stream.display_fps = 0;
  context.stream.display_frame_count = 0;
  context.stream.display_fps_window_start_us = 0;

  /* --- Decode queue init (GH #188) --- */
  chiaki_mutex_init(&decode_q_mtx, false);
  chiaki_cond_init(&decode_q_cond, &decode_q_mtx);
  decode_q_head = 0;
  decode_q_tail = 0;
  decode_thread_should_exit = false;
  decode_thread_started = false;
  decode_queue_drops = 0;
  decode_q_occ_area_us = 0;
  decode_q_occ_window_start_us = 0;
  decode_q_occ_last_change_us = 0;
  decode_q_occ_last_depth = 0;
  decode_q_occ_max_sample = 0;

  /* Allocate all slot buffers up front. On the first allocation failure free
   * any already-allocated slots and skip thread creation entirely — a NULL
   * slot->data would cause a NULL-deref in the producer on the first frame. */
  bool slots_ok = true;
  for (int i = 0; i < DECODE_QUEUE_DEPTH; i++) {
    decode_queue[i].data = malloc(DECODE_SLOT_CAPACITY);
    decode_queue[i].size = 0;
    decode_queue[i].frame_corrupt = false;
    decode_queue[i].frame_first_packet_ms = 0;
    if (decode_queue[i].data == NULL) {
      LOGE("VIDEO: failed to allocate decode slot %d — decode thread disabled", i);
      for (int j = 0; j < i; j++) {
        free(decode_queue[j].data);
        decode_queue[j].data = NULL;
      }
      slots_ok = false;
      break;
    }
  }

  if (slots_ok) {
    ChiakiErrorCode thread_err = chiaki_thread_create(&decode_thread, decode_thread_func, NULL);
    if (thread_err != CHIAKI_ERR_SUCCESS) {
      LOGE("VIDEO: failed to create decode thread: %d — freeing slots", thread_err);
      for (int i = 0; i < DECODE_QUEUE_DEPTH; i++) {
        free(decode_queue[i].data);
        decode_queue[i].data = NULL;
      }
    } else {
      chiaki_thread_set_name(&decode_thread, "VitaDecode");
      decode_thread_started = true;
    }
  }

  vitavideo_overlay_on_stream_start();
}

/* Bound on how long vita_h264_stop() will poll ui_render_in_progress before giving up
 * and destroying `mtx` anyway. See the poll loop's comment in vita_h264_stop() for the
 * reasoning behind this specific value. NOT an order of magnitude of margin in the
 * worst case: vita_h264_stop() calls vita2d_set_vblank_wait(true) before clearing
 * active_video_thread, so a UI thread already past the active_video_thread gate when
 * teardown begins can have its vita2d_swap_buffers() block for a full vsync
 * (~16.6ms) instead of the low-single-digit-ms common case -- call it ~3x margin
 * under this bound, not ~10x. Still comfortably under 50ms either way. */
#define UI_RENDER_QUIESCENCE_TIMEOUT_US (50 * 1000)
/* Poll granularity for the wait above -- matches the existing 1ms busy-spin-avoidance
 * sleep already used for this same "nothing to do yet" situation in ui.c's main loop. */
#define UI_RENDER_QUIESCENCE_POLL_US 1000

void vita_h264_stop() {
  vita2d_set_vblank_wait(true);
  active_video_thread = false;
  frame_ready_for_display = false;
  incoming_frame_corrupt = false;
  incoming_frame_first_packet_ms = 0;
  incoming_frame_decode_done_us = 0;
  frozen_frame_streak = 0;

  /* --- Decode thread shutdown (GH #188) ---
   * Signal and JOIN the decode thread BEFORE destroying the decode mutex
   * and BEFORE vita_h264_cleanup() frees decoder/frame_texture. The join
   * guarantees no sceAvcdecDecode is running on a freed decoder.
   * Guard on decode_thread_started so we never join a thread that was never
   * created (e.g. slot alloc failure or chiaki_thread_create failure). */
  if (decode_thread_started) {
    chiaki_mutex_lock(&decode_q_mtx);
    decode_thread_should_exit = true;
    chiaki_cond_signal(&decode_q_cond);
    chiaki_mutex_unlock(&decode_q_mtx);
    chiaki_thread_join(&decode_thread, NULL);
    decode_thread_started = false;
  }

  /* Free slot buffers. Non-NULL slots were allocated in vita_h264_start();
   * failed/skipped slots are already NULL so free() is a safe no-op. */
  for (int i = 0; i < DECODE_QUEUE_DEPTH; i++) {
    free(decode_queue[i].data);
    decode_queue[i].data = NULL;
  }
  chiaki_cond_fini(&decode_q_cond);
  chiaki_mutex_fini(&decode_q_mtx);

  /* GH #245 code review round 2 (BLOCKING finding, now fixed) + round 3 (comment
   * corrected to state the ACTUAL invariant, not an emergent one):
   *
   * The join above only proves the DECODE thread no longer holds `mtx` -- it says
   * nothing about the UI thread, which is a separate, third thread that also takes
   * `mtx` (inside vita_video_render_latest_frame() ->
   * promote_decoded_frame_to_last_good(), see that function's concurrency comment).
   * This function itself runs synchronously on the Chiaki SESSION thread (via
   * host_shutdown_media_pipeline(), invoked from the CHIAKI_EVENT_QUIT handler) --
   * a third thread again, distinct from both decode and UI. Destroying a mutex a
   * live thread currently owns is kernel UB (sceKernelDeleteMutex), not a benign
   * stale-pixel race, so getting this right matters.
   *
   * What actually keeps the UI thread out of `mtx` once we get here: the
   * active_video_thread == false check at the top of
   * vita_video_render_latest_frame() (see that check's comment). active_video_thread
   * is cleared a few lines above, in THIS function, BEFORE the decode-thread join
   * and BEFORE the poll below -- so by the time we reach chiaki_mutex_fini(&mtx),
   * every subsequent entry to vita_video_render_latest_frame() bails before it can
   * reach promote_decoded_frame_to_last_good(). This ordering --
   * active_video_thread = false, THEN join, THEN this poll, THEN
   * chiaki_mutex_fini(&mtx) -- is load-bearing and must be preserved; do not reorder
   * the active_video_thread clear to later in this function without re-auditing this
   * comment. (`context.stream.is_streaming = false`, set by the caller just before
   * this function runs, is NOT what provides this guarantee -- it only takes effect
   * on the UI thread's *next* main-loop check in ui.c and was the round-2 reviewer's
   * original, mistaken theory for why this was safe.)
   *
   * The poll on ui_render_in_progress below is a SEPARATE, defense-in-depth
   * safety net, not the primary mechanism: even with the ordering above,
   * plain `volatile` alone only guarantees eventual cross-thread visibility on this
   * hardware, not a bounded one, and vita_video_render_latest_frame() is already
   * mid-call by the time active_video_thread's clear becomes visible to it in the
   * worst case (see the timeout constant's comment for how long that call's tail --
   * draw + GPU wait + swap -- can run). Polling ui_render_in_progress (set/cleared
   * only by the UI thread, see its declaration comment) with a bounded timeout
   * lets us wait out that in-flight call instead of racing it, while still making
   * forward progress if the flag is somehow never cleared (e.g. a future bug that
   * adds a return path in vita_video_render_latest_frame() without clearing it).
   * If the timeout DOES fire, that is logged loudly (not swallowed) and we still
   * proceed with the teardown below, since refusing to ever finalize shutdown would
   * just trade a rare kernel-UB risk for a guaranteed hang.
   *
   * Why plain `volatile` is enough here, and why that would NOT generalize: the
   * margin this correctness argument relies on is "an entire chiaki_thread_join()
   * call's worth of time" (the decode-thread join above, which the poll below adds
   * further margin on top of) -- not a nanosecond-scale propagation window. That is
   * an enormous margin on any real hardware, which is why a plain volatile bool
   * (no memory barrier, no atomic, no mutex around the flag itself) suffices for
   * THIS specific handshake. It would NOT be sufficient for a general Dekker-style
   * mutual-exclusion handshake between two threads racing at full speed with no
   * such margin -- a future maintainer must not point to this code as precedent for
   * that. */
  if (ui_render_in_progress) {
    uint64_t quiescence_wait_start_us = sceKernelGetProcessTimeWide();
    while (ui_render_in_progress) {
      if (sceKernelGetProcessTimeWide() - quiescence_wait_start_us >=
          UI_RENDER_QUIESCENCE_TIMEOUT_US) {
        LOGE(
            "VIDEO: UI thread still in render critical region after %dus -- proceeding "
            "with mtx teardown anyway (possible kernel UB if it is still mid-swap)",
            UI_RENDER_QUIESCENCE_TIMEOUT_US);
        break;
      }
      sceKernelDelayThread(UI_RENDER_QUIESCENCE_POLL_US);
    }
  }

  chiaki_mutex_fini(&mtx);
  vitavideo_overlay_on_stream_stop();
}

void vitavideo_show_poor_net_indicator() {
  vitavideo_overlay_show_poor_net_indicator();
}

void vitavideo_hide_poor_net_indicator() {
  vitavideo_overlay_hide_poor_net_indicator();
}

uint32_t vita_video_decode_queue_drops(void) {
  return decode_queue_drops;
}

int vitavideo_initialized() {
  return video_status != NOT_INIT;
}
