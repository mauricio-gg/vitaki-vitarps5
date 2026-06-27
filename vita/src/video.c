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

vita2d_texture *frame_texture = NULL;
enum VideoStatus video_status = NOT_INIT;

SceAvcdecCtrl *decoder = NULL;
SceUID decoderblock = -1;
SceUID videodecblock = -1;
SceUID videodecUnmap = -1;
SceUIntVAddr videodecContext = 0;
SceAvcdecQueryDecoderInfo *decoder_info = NULL;

static bool active_video_thread = true;
static volatile bool frame_ready_for_display = false;

/* --- Freeze-on-corrupt: last-good frame texture and presentation state --- */

/* Twin texture holding the last clean decoded frame. Allocated in video_setup_framebuffer(),
 * freed in video_cleanup_framebuffer(), NULL until first clean frame arrives. */
static vita2d_texture *last_good_texture = NULL;

/* Set inside decode_frame_now() on the dedicated decode thread, under `mtx`,
 * after a successful sceAvcdecDecode. Read by vita_video_render_latest_frame() on
 * the UI thread. Single-writer/single-reader on Vita Cortex-A9 — volatile sufficient. */
static volatile bool incoming_frame_corrupt = false;

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

/* Snapshot the current decoded frame as the last-good frame. Runs on the UI
 * thread only — keeps the ~2 MB copy off the Takion receive/decode hot path. */
static void snapshot_last_good_frame(void) {
  if (last_good_texture == NULL)
    return;
  uint32_t copy_size = image_scaling.texture_height * vita2d_texture_get_stride(frame_texture);
  sceClibMemcpy(vita2d_texture_get_datap(last_good_texture),
                vita2d_texture_get_datap(frame_texture), copy_size);
}

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

    context.stream.fps_window_frame_count = 0;
    context.stream.fps_window_start_us = now_us;
  }
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

ChiakiMutex mtx;

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
  picture.frame.pPicture[0] = vita2d_texture_get_datap(frame_texture);

  /* Allocate the twin "last good frame" texture. Same format and dimensions as
   * frame_texture so we can memcpy between them. Failure is non-fatal: freeze
   * suppression simply won't engage (last_good_texture stays NULL). */
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
static int decode_frame_now(uint8_t *buf, size_t buf_size, bool frame_corrupt) {
  chiaki_mutex_lock(&mtx);

  if (buf_size > (size_t)sceAvcdecDecodeAvailableSize(decoder)) {
    sceClibPrintf("Video decode buffer too small\n");
    chiaki_mutex_unlock(&mtx);
    return 1;
  }

  int ret = 0;
  au.es.pBuf = buf;
  au.es.size = buf_size;
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
     * frame-overwrite scenarios. The last-good snapshot (the expensive ~2 MB
     * memcpy) is now taken on the UI thread in vita_video_render_latest_frame()
     * so the decode thread is never stalled by it. */
    incoming_frame_corrupt = frame_corrupt;
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

int vita_h264_decode_frame(uint8_t *buf, size_t buf_size, bool frame_corrupt) {
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
  decode_q_tail = (decode_q_tail + 1) % DECODE_QUEUE_DEPTH;

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

bool vita_video_render_latest_frame(void) {
  if (!frame_ready_for_display)
    return false;

  frame_ready_for_display = false;

  bool drop_frame = should_drop_frame_for_pacing();
  if (drop_frame) {
    // Frame is paced out but still consumed — advance freeze state so the cap
    // counts all consumed frames, not just displayed ones.
    bool corrupt = incoming_frame_corrupt;
    if (corrupt && last_good_texture != NULL && frozen_frame_streak < FREEZE_MAX_STREAK) {
      frozen_frame_streak++;
      context.stream.freeze_engaged_count++;
    } else if (!corrupt) {
      // Clean paced-drop: still update the last-good snapshot.
      if (frozen_frame_streak > 0) {
        LOGD("PIPE/FREEZE cleared streak=%d (paced)", frozen_frame_streak);
      }
      frozen_frame_streak = 0;
      snapshot_last_good_frame();
    } else {
      /* corrupt + cap-release or no snapshot: mirror the non-paced cap-release path */
      if (frozen_frame_streak > 0)
        LOGD("PIPE/FREEZE cap-released streak=%d (paced)", frozen_frame_streak);
      frozen_frame_streak = 0;
    }
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
   * when we read it here. The last-good snapshot is taken below on this thread. */
  bool corrupt = incoming_frame_corrupt;
  vita2d_texture *present_texture = frame_texture;

  if (corrupt && last_good_texture != NULL && frozen_frame_streak < FREEZE_MAX_STREAK) {
    frozen_frame_streak++;
    context.stream.freeze_engaged_count++;
    if (frozen_frame_streak == 1)
      LOGD("PIPE/FREEZE engaged streak=%d", frozen_frame_streak);
    present_texture = last_good_texture;
  } else if (!corrupt) {
    /* Clean frame — take the last-good snapshot here on the UI thread so the
     * Takion receive/decode thread is never stalled by the ~2 MB copy. */
    if (frozen_frame_streak > 0) {
      LOGD("PIPE/FREEZE cleared streak=%d", frozen_frame_streak);
      frozen_frame_streak = 0;
    }
    present_texture = frame_texture;
    snapshot_last_good_frame();
  } else {
    /* corrupt && (last_good_texture == NULL || streak >= FREEZE_MAX_STREAK) */
    if (frozen_frame_streak >= FREEZE_MAX_STREAK)
      LOGD("PIPE/FREEZE cap-released streak=%d", frozen_frame_streak);
    frozen_frame_streak = 0;
    present_texture = frame_texture;
  }

  vita2d_start_drawing();

  draw_streaming(present_texture);
  vitavideo_overlay_render();

  vita2d_end_drawing();

  vita2d_wait_rendering_done();
  vita2d_swap_buffers();

  // D7: Track actual frames rendered to screen per second
  {
    uint64_t now_us = sceKernelGetProcessTimeWide();
    if (context.stream.display_fps_window_start_us == 0)
      context.stream.display_fps_window_start_us = now_us;
    context.stream.display_frame_count++;
    if (now_us - context.stream.display_fps_window_start_us >= 1000000) {
      context.stream.display_fps = context.stream.display_frame_count;
      context.stream.display_frame_count = 0;
      context.stream.display_fps_window_start_us = now_us;
    }
  }

  return true;
}

/* Decode thread: pops compressed frames from the SPSC queue and calls
 * decode_frame_now(). Pinned to USER_1 so decode no longer competes with
 * the recv thread (USER_0) or audio (USER_2) for CPU time. */
static void *decode_thread_func(void *user) {
  (void)user;
#ifdef __PSVITA__
  /* Pin to USER_1 — recv is USER_0, audio is USER_2. */
  sceKernelChangeThreadPriority(SCE_KERNEL_THREAD_ID_SELF, 64);
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
    chiaki_mutex_unlock(&decode_q_mtx);

    /* Decode the frame. The slot buffer is exclusively ours until we advance
     * decode_q_head below — the producer will block or drop-oldest on the
     * preceding slots rather than overwriting this one. */
    decode_frame_now(frame_data, frame_size, corrupt);

    /* Release the slot now that decode is done. Signal any blocked producer. */
    chiaki_mutex_lock(&decode_q_mtx);
    decode_q_head = (decode_q_head + 1) % DECODE_QUEUE_DEPTH;
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
  frozen_frame_streak = 0;
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

  /* Allocate all slot buffers up front. On the first allocation failure free
   * any already-allocated slots and skip thread creation entirely — a NULL
   * slot->data would cause a NULL-deref in the producer on the first frame. */
  bool slots_ok = true;
  for (int i = 0; i < DECODE_QUEUE_DEPTH; i++) {
    decode_queue[i].data = malloc(DECODE_SLOT_CAPACITY);
    decode_queue[i].size = 0;
    decode_queue[i].frame_corrupt = false;
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

void vita_h264_stop() {
  vita2d_set_vblank_wait(true);
  active_video_thread = false;
  frame_ready_for_display = false;
  incoming_frame_corrupt = false;
  frozen_frame_streak = 0;

  /* --- Decode thread shutdown (GH #188) ---
   * Signal and JOIN the decode thread BEFORE destroying the decode mutex
   * and BEFORE vita_h264_cleanup() frees decoder/frame_texture. The join
   * guarantees no sceAvcdecDecode is running on a freed decoder and that
   * no decode thread holds mtx when chiaki_mutex_fini(&mtx) runs below.
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
