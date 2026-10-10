/**
 * @file ui_freeze.c
 * @brief The popup background freeze (see ui_freeze.h)
 *
 * Why the capture reads the finished frame instead of rendering the screen a second time:
 * the display runs 4x MSAA, and a render target is not MSAA, so drawing the screen into a target
 * would need an MSAA-none version of every program the screen uses (textured, tinted, text) and a
 * draw-only path through every component. Averaging the frame the GPU has just finished needs
 * none of that and cannot drift from what the screen really looked like.
 *
 * vita2d_get_current_fb() returns the buffer that was last handed to the display queue, which
 * after vita2d_swap_buffers() is the frame just drawn. Its rows are the display pitch, 960 pixels
 * (vita2d's display_callback passes 960 to sceDisplaySetFrameBuf), format 0xAABBGGRR like every
 * vita2d texture. The buffer is CDRAM, where single reads from the CPU are slow, so each pair of
 * rows is copied with memcpy into a small static buffer and averaged from there.
 */

#include "ui/ui_freeze.h"

#include <stdint.h>
#include <string.h>

#include <vita2d.h>
#include <psp2/kernel/processmgr.h>

#include "context.h"
#include "ui/ui_constants.h"
#include "ui/ui_downsample.h"
#include "ui/ui_theme.h"

/** Pixels per row of vita2d's display buffers (see the file comment). */
#define FB_PITCH_PX VITA_WIDTH
/** Source rows averaged into one copy row. */
#define SRC_ROWS_PER_ROW 2
/** Alpha bits forced on the copy: the screen behind is opaque. */
#define OPAQUE_MASK 0xFF000000u
/** Draw the half-resolution copy at twice its size. */
#define UPSCALE 2.0f

typedef enum freeze_state_t {
  FREEZE_IDLE = 0,
  FREEZE_REQUESTED,   /**< the frame being drawn will be captured */
  FREEZE_READY,       /**< the copy stands in for the screen behind the popup */
  FREEZE_UNAVAILABLE, /**< no copy could be made; the live screen stays behind the popup */
} FreezeState;

static FreezeState s_state = FREEZE_IDLE;
static bool s_release_pending = false;
static vita2d_texture *s_copy = NULL;
/** The two source rows being averaged; static so a capture allocates nothing. */
static uint32_t s_rows[SRC_ROWS_PER_ROW][VITA_WIDTH];

void ui_freeze_request(void) {
  s_release_pending = false;
  if (s_state == FREEZE_IDLE)
    s_state = FREEZE_REQUESTED;
}

void ui_freeze_release(void) {
  if (s_state == FREEZE_REQUESTED)
    s_state = FREEZE_IDLE;
  else if (s_state != FREEZE_IDLE)
    s_release_pending = true;
}

bool ui_freeze_is_capturing(void) {
  return s_state == FREEZE_REQUESTED;
}

bool ui_freeze_is_ready(void) {
  return s_state == FREEZE_READY;
}

void ui_freeze_draw(void) {
  if (s_state != FREEZE_READY || !s_copy)
    return;
  vita2d_draw_texture_scale(s_copy, 0.0f, 0.0f, UPSCALE, UPSCALE);
}

/** Create the copy texture on first use. Returns false (after logging) when it cannot be made. */
static bool ensure_copy_texture(void) {
  if (s_copy)
    return true;
  s_copy = vita2d_create_empty_texture(UI_FREEZE_W, UI_FREEZE_H);
  if (!s_copy) {
    LOGE("UI/POPUP_FREEZE could not allocate the %dx%d copy, popups go over the live screen",
         UI_FREEZE_W, UI_FREEZE_H);
    return false;
  }
  vita2d_texture_set_filters(s_copy, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
  return true;
}

/**
 * Average the finished frame in the display buffer into the copy texture.
 * Returns false when the frame cannot be read.
 */
static bool copy_frame(void) {
  const uint32_t *frame = (const uint32_t *)vita2d_get_current_fb();
  if (!frame) {
    LOGE("UI/POPUP_FREEZE no display buffer to read, popups go over the live screen");
    return false;
  }
  uint32_t *dst = (uint32_t *)vita2d_texture_get_datap(s_copy);
  const int dst_stride_px = vita2d_texture_get_stride(s_copy) / (int)sizeof(uint32_t);

  for (int y = 0; y < UI_FREEZE_H; y++) {
    for (int r = 0; r < SRC_ROWS_PER_ROW; r++) {
      memcpy(s_rows[r], frame + (size_t)(y * SRC_ROWS_PER_ROW + r) * FB_PITCH_PX,
             sizeof(s_rows[r]));
    }
    ui_downsample_row_pair(s_rows[0], s_rows[1], UI_FREEZE_W, dst + (size_t)y * dst_stride_px,
                           OPAQUE_MASK);
  }
  return true;
}

/** Wait for the GPU to finish the frame, copy it, and log what that cost. */
static void capture(void) {
  const uint64_t start_us = sceKernelGetProcessTimeWide();
  if (!ensure_copy_texture()) {
    s_state = FREEZE_UNAVAILABLE;
    return;
  }

  vita2d_wait_rendering_done();
  const uint64_t waited_us = sceKernelGetProcessTimeWide();
  const bool copied = copy_frame();
  const uint64_t done_us = sceKernelGetProcessTimeWide();

  s_state = copied ? FREEZE_READY : FREEZE_UNAVAILABLE;
  LOGD("UI/POPUP_FREEZE capture ok=%d wait_us=%llu copy_us=%llu total_us=%llu", copied ? 1 : 0,
       (unsigned long long)(waited_us - start_us), (unsigned long long)(done_us - waited_us),
       (unsigned long long)(done_us - start_us));
}

void ui_freeze_frame_end(void) {
  if (s_state == FREEZE_REQUESTED) {
    capture();
  } else if (s_release_pending) {
    s_state = FREEZE_IDLE;
    s_release_pending = false;
  }
}
