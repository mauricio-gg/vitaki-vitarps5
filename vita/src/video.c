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
#include "context.h"

#include <h264-bitstream/h264_stream.h>

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

#include <stdarg.h>

void draw_streaming(vita2d_texture *frame_texture);
void draw_fps();
void draw_indicators();

enum {
  VITA_VIDEO_INIT_OK                    = 0,
  VITA_VIDEO_ERROR_NO_MEM               = 0x80010001,
  VITA_VIDEO_ERROR_INIT_LIB             = 0x80010002,
  VITA_VIDEO_ERROR_QUERY_DEC_MEMSIZE    = 0x80010003,
  VITA_VIDEO_ERROR_ALLOC_MEM            = 0x80010004,
  VITA_VIDEO_ERROR_GET_MEMBASE          = 0x80010005,
  VITA_VIDEO_ERROR_CREATE_DEC           = 0x80010006,
  VITA_VIDEO_ERROR_CREATE_PACER_THREAD  = 0x80010007,
};

// #define DECODER_BUFFER_SIZE (92 * 1024)
#define AU_BUF_SIZE(STREAM_WIDTH, STREAM_HEIGHT) (STREAM_WIDTH*STREAM_HEIGHT*3/2/2)
#define DECODE_AU_ALIGNMENT (0x100)

static char* decoder_buffer = NULL;
static char* header_buf = NULL;
static size_t header_buf_size;

enum {
  SCREEN_WIDTH = 960,
  SCREEN_HEIGHT = 544,
  LINE_SIZE = 960,
  FRAMEBUFFER_SIZE = 2 * 1024 * 1024,
  FRAMEBUFFER_ALIGNMENT = 256 * 1024
};

enum VideoStatus {
  NOT_INIT,
  INIT_GS,
  INIT_FRAMEBUFFER,
  INIT_AVC_LIB,
  INIT_DECODER_MEMBLOCK,
  INIT_AVC_DEC,
  INIT_FRAME_PACER_THREAD,
};

vita2d_texture *frame_texture = NULL;
enum VideoStatus video_status = NOT_INIT;

SceAvcdecCtrl *decoder = NULL;
SceUID displayblock = -1;
SceUID decoderblock = -1;
SceUID videodecblock = -1;
SceUID videodecUnmap = -1;
SceUIntVAddr videodecContext = 0;
SceUID pacer_thread = -1;
SceAvcdecQueryDecoderInfo *decoder_info = NULL;

typedef struct {
  bool activated;
  uint8_t alpha;
  bool plus;
} indicator_status;

static unsigned numframes;
static bool active_video_thread = true;
static bool active_pacer_thread = false;
static indicator_status poor_net_indicator = {0};

uint32_t frame_count = 0;
uint32_t need_drop = 0;
uint32_t curr_fps[2] = {0, 0};
float carry = 0;

typedef struct {
  unsigned int texture_width;
  unsigned int texture_height;
  float origin_x;
  float origin_y;
  float region_x1;
  float region_y1;
  float region_x2;
  float region_y2;
} image_scaling_settings;

static image_scaling_settings image_scaling = {0};

void update_scaling_settings(int width, int height) {
  image_scaling.texture_width = SCREEN_WIDTH;
  image_scaling.texture_height = SCREEN_HEIGHT;
  image_scaling.origin_x = 0;
  image_scaling.origin_y = 0;
  image_scaling.region_x1 = 0;
  image_scaling.region_y1 = 0;
  image_scaling.region_x2 = SCREEN_WIDTH;
  image_scaling.region_y2 = SCREEN_HEIGHT;

  double scaled_width = (double) SCREEN_HEIGHT * width / height;
  double scaled_height = (double) SCREEN_WIDTH * height / width;

  if (context.config.stretch_video) {
    if (SCREEN_WIDTH * height == SCREEN_HEIGHT * width) {
      // matches aspect; already fills screen
      image_scaling.region_x2 = SCREEN_WIDTH;
      image_scaling.region_y2 = SCREEN_HEIGHT;
    } else if (SCREEN_WIDTH * height > SCREEN_HEIGHT * width) {
      image_scaling.texture_width = VITA_DECODER_RESOLUTION(scaled_width);
      image_scaling.region_x2 = VITA_DECODER_RESOLUTION(scaled_width);
      image_scaling.origin_x = round((double) (SCREEN_WIDTH - image_scaling.texture_width) / 2);
      image_scaling.region_y2 = SCREEN_HEIGHT;
    } else {
      image_scaling.texture_height = VITA_DECODER_RESOLUTION(scaled_height);
      image_scaling.region_y2 = VITA_DECODER_RESOLUTION(scaled_height);
      image_scaling.origin_y = round((double) (SCREEN_HEIGHT - image_scaling.texture_height) / 2);
      image_scaling.region_x2 = SCREEN_WIDTH;
    }
  } else {
    float scale = 1.0f;
    float scale_w = (float)SCREEN_WIDTH / (float)width;
    float scale_h = (float)SCREEN_HEIGHT / (float)height;
    scale = scale_w < scale_h ? scale_w : scale_h;
    if (scale > 1.0f)
      scale = 1.0f;

    image_scaling.region_x2 = width * scale;
    image_scaling.region_y2 = height * scale;
    image_scaling.origin_x = round((SCREEN_WIDTH - image_scaling.region_x2) / 2.0f);
    image_scaling.origin_y = round((SCREEN_HEIGHT - image_scaling.region_y2) / 2.0f);
  }

  LOGD("update_scaling_settings: width = %u\n", width);
  LOGD("update_scaling_settings: height = %u\n", height);
  LOGD("update_scaling_settings: scaled_width = %f\n", scaled_width);
  LOGD("update_scaling_settings: scaled_height = %f\n", scaled_height);
  LOGD("update_scaling_settings: image_scaling.texture_width = %u\n", image_scaling.texture_width);
  LOGD("update_scaling_settings: image_scaling.texture_height = %u\n", image_scaling.texture_height);
  LOGD("update_scaling_settings: image_scaling.origin_x = %f\n", image_scaling.origin_x);
  LOGD("update_scaling_settings: image_scaling.origin_y = %f\n", image_scaling.origin_y);
  LOGD("update_scaling_settings: image_scaling.region_x1 = %f\n", image_scaling.region_x1);
  LOGD("update_scaling_settings: image_scaling.region_y1 = %f\n", image_scaling.region_y1);
  LOGD("update_scaling_settings: image_scaling.region_x2 = %f\n", image_scaling.region_x2);
  LOGD("update_scaling_settings: image_scaling.region_y2 = %f\n", image_scaling.region_y2);
}

static int vita_pacer_thread_main(SceSize args, void *argp) {
  // 1s
  int wait = 1000000;
  //float max_fps = 0;
  //sceDisplayGetRefreshRate(&max_fps);
  //if (config.stream.fps == 30) {
  //  max_fps /= 2;
  //}
  int max_fps = context.stream.fps; // bug?
  uint64_t last_vblank_count = sceDisplayGetVcount();
  uint64_t last_check_time = sceKernelGetSystemTimeWide();
  //float carry = 0;
  need_drop = 0;
  frame_count = 0;
  while (active_pacer_thread) {
    uint64_t curr_vblank_count = sceDisplayGetVcount();
    uint32_t vblank_fps = curr_vblank_count - last_vblank_count;
    uint32_t curr_frame_count = frame_count;
    frame_count = 0;

    if (!active_video_thread) {
    //  carry = 0;
    LOGD("thread inactive");
    } else {
      if (/*config.enable_frame_pacer*/false && curr_frame_count > max_fps) {
        //carry += curr_frame_count - max_fps;
        //if (carry > 1) {
        //  need_drop += (int)carry;
        //  carry -= (int)carry;
        //}
        need_drop += curr_frame_count - max_fps;
      }
      LOGD("fps0/fps1/carry/need_drop: %u/%u/%f/%u\n",
                    curr_frame_count, vblank_fps, carry, need_drop);
    }

    curr_fps[0] = curr_frame_count;
    curr_fps[1] = vblank_fps;

    last_vblank_count = curr_vblank_count;
    uint64_t curr_check_time = sceKernelGetSystemTimeWide();
    uint32_t lapse = curr_check_time - last_check_time;
    last_check_time = curr_check_time;
    if (lapse > wait && (lapse - wait) < wait) {
      LOGD("sleep: %d wait: %d lapse: %d", wait * 2 - lapse, wait, lapse);
      sceKernelDelayThread(wait * 2 - lapse);
    } else {
      sceKernelDelayThread(wait);
    }
  }
  return 0;
}

ChiakiMutex mtx;

bool threadSetupComplete = false;

void vita_h264_cleanup() {
	if (video_status == INIT_FRAME_PACER_THREAD) {
		// active_pacer_thread = false;
		// // wait 10sec
		// SceUInt timeout = 10000000;
		// int ret;
		// sceKernelWaitThreadEnd(pacer_thread, &ret, &timeout);
		// sceKernelDeleteThread(pacer_thread);
		video_status--;
	}

	if (video_status == INIT_AVC_DEC) {
		sceAvcdecDeleteDecoder(decoder);
		video_status--;
	}

	if (video_status == INIT_DECODER_MEMBLOCK) {
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

	if (video_status == INIT_AVC_LIB) {

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

	if (video_status == INIT_FRAMEBUFFER) {
		if (frame_texture != NULL) {
			vita2d_free_texture(frame_texture);
			frame_texture = NULL;
		}

		if (decoder_buffer != NULL) {
			free(decoder_buffer);
			decoder_buffer = NULL;
		}
		video_status--;
	}

	if (video_status == INIT_GS) {
		// gs_sps_stop();
    threadSetupComplete = false;
		video_status--;
	}

}

typedef struct SceVideodecMemInfo {
    SceUInt32    memSize;

} SceVideodecMemInfo;

typedef struct SceVideodecCtrl {
    SceAvcdecBuf    memBuf;
    SceUID            memBufUid;

    SceUIntVAddr    vaContext;
    SceUInt32        contextSize;
} SceVideodecCtrl;
extern SceInt32 sceVideodecQueryMemSize(SceUInt32 codecType, const SceVideodecQueryInitInfo *pInitInfo, SceVideodecMemInfo *pMemInfo);

SceAvcdecAu au = {0};
SceAvcdecArrayPicture array_picture = {0};
struct SceAvcdecPicture picture = {0};
struct SceAvcdecPicture *pictures = { &picture };


bool first_frame = false;
int vita_h264_setup(int width, int height) {
  int ret;
  LOGD("vita video setup\n");
	SceVideodecCtrl libCtrl;
	SceVideodecMemInfo libMemInfo;
  SceVideodecQueryInitInfo initVideodec;
	void *libMem;
  first_frame = true;

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
    // INIT_FRAMEBUFFER
    // update_scaling_settings(SCREEN_WIDTH, SCREEN_HEIGHT);
    update_scaling_settings(width, height);
    picture.frame.framePitch = image_scaling.texture_width;
    picture.frame.frameWidth = image_scaling.texture_width;
    picture.frame.frameHeight = image_scaling.texture_height;

		// decoder_buffer = memalign(DECODE_AU_ALIGNMENT, AU_BUF_SIZE(SCREEN_WIDTH, SCREEN_HEIGHT));
    // // decoder_buffer = malloc(DECODER_BUFFER_SIZE);
    // if (decoder_buffer == NULL) {
    //   LOGD("not enough memory\n");
    //   ret = VITA_VIDEO_ERROR_NO_MEM;
    //   goto cleanup;
    // }

    // au.es.pBuf = decoder_buffer;

    frame_texture = vita2d_create_empty_texture_format(image_scaling.texture_width, image_scaling.texture_height, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
    if (frame_texture == NULL) {
      LOGD("not enough memory4\n");
      ret = VITA_VIDEO_ERROR_NO_MEM;
      goto cleanup;
    }

    picture.frame.pPicture[0] = vita2d_texture_get_datap(frame_texture);

    video_status++;
  }

  if (video_status == INIT_FRAMEBUFFER) {
    // INIT_AVC_LIB
    // if (initVideodec == NULL) {
          // if (init == NULL) {
        // LOGD("not enough memory1\n");
        // ret = VITA_VIDEO_ERROR_NO_MEM;
        // goto cleanup;
      // }
    // }
    sceClibMemset(&initVideodec.hwAvc, 0, sizeof(SceVideodecQueryInitInfoHwAvcdec));

		initVideodec.hwAvc.size = sizeof(SceVideodecQueryInitInfoHwAvcdec);
		initVideodec.hwAvc.horizontal = VITA_DECODER_RESOLUTION(width);
		initVideodec.hwAvc.vertical = VITA_DECODER_RESOLUTION(height);
		initVideodec.hwAvc.numOfStreams = 1;
		initVideodec.hwAvc.numOfRefFrames = REF_FRAMES;

		ret = sceVideodecQueryMemSize(SCE_VIDEODEC_TYPE_HW_AVCDEC, &initVideodec, &libMemInfo);
		if (ret < 0) {
			sceClibPrintf("sceVideodecQueryMemSize 0x%x\n", ret);
			ret = VITA_VIDEO_ERROR_INIT_LIB;
			goto cleanup;
		}

		libMemInfo.memSize = ROUND_UP(libMemInfo.memSize, 256 * 1024);

		SceKernelAllocMemBlockOpt   opt;
    sceClibMemset(&opt, 0, sizeof(SceKernelAllocMemBlockOpt));
		opt.size = sizeof(SceKernelAllocMemBlockOpt);
		opt.attr = 4;
		opt.alignment = 256 * 1024;

		videodecblock = sceKernelAllocMemBlock("videodec", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, libMemInfo.memSize, &opt);
		if (videodecblock < 0) {
			sceClibPrintf("videodecblock: 0x%08x\n", videodecblock);
			ret = VITA_VIDEO_ERROR_INIT_LIB;
			goto cleanup;
		}

		ret = sceKernelGetMemBlockBase(videodecblock, &libMem);
		if (ret < 0) {
			sceClibPrintf("sceKernelGetMemBlockBase: 0x%x\n", ret);
			ret = VITA_VIDEO_ERROR_INIT_LIB;
			goto cleanup;
		}

		videodecUnmap = sceCodecEngineOpenUnmapMemBlock(libMem, libMemInfo.memSize);
		if (videodecUnmap < 0) {
			sceClibPrintf("sceCodecEngineOpenUnmapMemBlock: 0x%x\n", videodecUnmap);
			ret = VITA_VIDEO_ERROR_INIT_LIB;
			goto cleanup;
		}

		videodecContext = sceCodecEngineAllocMemoryFromUnmapMemBlock(videodecUnmap, libMemInfo.memSize, 256 * 1024);
		if (videodecContext < 0) {
			sceClibPrintf("sceCodecEngineAllocMemoryFromUnmapMemBlock: 0x%x\n", videodecContext);
			ret = VITA_VIDEO_ERROR_INIT_LIB;
			goto cleanup;
		}

    sceClibMemset(&libCtrl, 0, sizeof(SceVideodecCtrl));
		libCtrl.vaContext = videodecContext;
		libCtrl.contextSize = libMemInfo.memSize;

    ret = sceVideodecInitLibraryWithUnmapMem(SCE_VIDEODEC_TYPE_HW_AVCDEC, &libCtrl, &initVideodec);
    if (ret < 0) {
      LOGD("sceVideodecInitLibrary 0x%x\n", ret);
      ret = VITA_VIDEO_ERROR_INIT_LIB;
      goto cleanup;
    }
    video_status++;
  }

  if (video_status == INIT_AVC_LIB) {
    // INIT_DECODER_MEMBLOCK
    if (decoder_info == NULL) {
      decoder_info = calloc(1, sizeof(SceAvcdecQueryDecoderInfo));
      if (decoder_info == NULL) {
        LOGD("not enough memory2\n");
        ret = VITA_VIDEO_ERROR_NO_MEM;
        goto cleanup;
      }
    }
		decoder_info->horizontal = initVideodec.hwAvc.horizontal;
		decoder_info->vertical = initVideodec.hwAvc.vertical;
		decoder_info->numOfRefFrames = initVideodec.hwAvc.numOfRefFrames;

    SceAvcdecDecoderInfo decoder_info_out = {0};

    ret = sceAvcdecQueryDecoderMemSize(SCE_VIDEODEC_TYPE_HW_AVCDEC, decoder_info, &decoder_info_out);
    if (ret < 0) {
      LOGD("sceAvcdecQueryDecoderMemSize 0x%x size 0x%x\n", ret, decoder_info_out.frameMemSize);
      ret = VITA_VIDEO_ERROR_QUERY_DEC_MEMSIZE;
      goto cleanup;
    }

    decoder = calloc(1, sizeof(SceAvcdecCtrl));
    if (decoder == NULL) {
      LOGD("not enough memory3\n");
      ret = VITA_VIDEO_ERROR_ALLOC_MEM;
      goto cleanup;
    }

    decoder->frameBuf.size = decoder_info_out.frameMemSize;
    LOGD("allocating size 0x%x\n", decoder_info_out.frameMemSize);
		SceKernelAllocMemBlockOpt   opt;
    sceClibMemset(&opt, 0, sizeof(SceKernelAllocMemBlockOpt));
		opt.size = sizeof(SceKernelAllocMemBlockOpt);
		opt.attr = 4;
		opt.alignment = 1024 * 1024;
    decoderblock = sceKernelAllocMemBlock("decoder", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, decoder_info_out.frameMemSize, &opt);
    // decoderblock = sceKernelAllocMemBlock("decoder", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, sz, NULL);
    if (decoderblock < 0) {
      LOGD("decoderblock: 0x%08x\n", decoderblock);
      ret = VITA_VIDEO_ERROR_ALLOC_MEM;
      goto cleanup;
    }

    ret = sceKernelGetMemBlockBase(decoderblock, &decoder->frameBuf.pBuf);
    if (ret < 0) {
      LOGD("sceKernelGetMemBlockBase: 0x%x\n", ret);
      ret = VITA_VIDEO_ERROR_GET_MEMBASE;
      goto cleanup;
    }
    video_status++;
  }

  if (video_status == INIT_DECODER_MEMBLOCK) {
    // INIT_AVC_DEC
    LOGD("base: 0x%08x\n", decoder->frameBuf.pBuf);

    ret = sceAvcdecCreateDecoder(SCE_VIDEODEC_TYPE_HW_AVCDEC, decoder, decoder_info);
    if (ret < 0) {
      LOGD("sceAvcdecCreateDecoder 0x%x\n", ret);
      ret = VITA_VIDEO_ERROR_CREATE_DEC;
      goto cleanup;
    }
    video_status++;
  }

  if (video_status == INIT_AVC_DEC) {
    // INIT_FRAME_PACER_THREAD
    // ret = sceKernelCreateThread("frame_pacer", vita_pacer_thread_main, 0, 4 * 1024, 0, 0, NULL);
    // if (ret < 0) {
    //   LOGD("sceKernelCreateThread 0x%x\n", ret);
    //   ret = VITA_VIDEO_ERROR_CREATE_PACER_THREAD;
    //   goto cleanup;
    // }
    // pacer_thread = ret;
    // active_pacer_thread = true;
    // sceKernelStartThread(pacer_thread, 0, NULL);
    video_status++;
  }

  return VITA_VIDEO_INIT_OK;

cleanup:
  vita_h264_cleanup();
  return ret;
}

#ifndef HEXDUMP_COLS
#define HEXDUMP_COLS 16
#endif
 
void hexdump(void *mem, unsigned int len)
{
        unsigned int i, j;
        
        for(i = 0; i < len + ((len % HEXDUMP_COLS) ? (HEXDUMP_COLS - len % HEXDUMP_COLS) : 0); i++)
        {
                /* print offset */
                if(i % HEXDUMP_COLS == 0)
                {
                        sceClibPrintf("0x%06x: ", i);
                }
 
                /* print hex data */
                if(i < len)
                {
                        sceClibPrintf("%02x ", 0xFF & ((char*)mem)[i]);
                }
                else /* end of block, just aligning for ASCII dump */
                {
                        // printf("   ");
                        sceClibPrintf("\n");
                }
                
                // /* print ASCII dump */
                // if(i % HEXDUMP_COLS == (HEXDUMP_COLS - 1))
                // {
                //         for(j = i - (HEXDUMP_COLS - 1); j <= i; j++)
                //         {
                //                 if(j >= len) /* end of block, not really printing */
                //                 {
                //                         putchar(' ');
                //                 }
                //                 else if(isprint(((char*)mem)[j])) /* printable char */
                //                 {
                //                         putchar(0xFF & ((char*)mem)[j]);        
                //                 }
                //                 else /* other char */
                //                 {
                //                         putchar('.');
                //                 }
                //         }
                //         putchar('\n');
                // }
        }
}

// #define GS_SPS_BITSTREAM_FIXUP 0x01
// #define GS_SPS_BASELINE_HACK 0x02

// https://github.dev/intel-linyonghui/RPiPlay/blob/4a65c7b84ff09acd7d6d494bb9037bbf106153a8/renderers/video_renderer_rpi.c?q=NAL_UNIT_TYPE_SPS & https://github.dev/Stary2001/moonlight-embedded/blob/04a7c1d8b24571f2bc7807894c2db8b8096b0c33/libgamestream/sps.c
bool vita_h264_process_header(uint8_t *data, size_t data_len) {
  // uint8_t *modified_data;
  // This reduces the Raspberry Pi H264 decode pipeline delay from about 11 to 6 frames for RPiPlay.
  // Described at https://www.raspberrypi.org/forums/viewtopic.php?t=41053
  int sps_start, sps_end;
  int sps_size = find_nal_unit(data, data_len, &sps_start, &sps_end);
  if (sps_size > 0) {
      LOGD("replacing SPS");
      // const int sps_wiggle_room = 12;
      // const unsigned char nal_marker[] = { 0x0, 0x0, 0x0, 0x1 };
      // int modified_data_len = *data_len + sps_wiggle_room + sizeof(nal_marker);
      // uint8_t* modified_data = malloc(data_len);

      h264_stream_t *h = h264_new();
      int actual_sps_size = read_nal_unit(h, &data[sps_start], sps_size);
      if (actual_sps_size < 0) {
        LOGD("Reading NAL error %d", actual_sps_size);
        return false;
      }
      // h->nal->nal_unit_type = NAL_UNIT_TYPE_SPS;
      h->sps->num_ref_frames = REF_FRAMES;
      // h->sps->level_idc = 32; // Max 5 buffered frames at 1280x720x60
      // h->sps->vui.bitstream_restriction_flag = 1;
      // h->sps->vui.max_bits_per_mb_denom = 1;
      // h->sps->vui.log2_max_mv_length_horizontal = 16;
      // h->sps->vui.log2_max_mv_length_vertical = 16;
      // h->sps->vui.num_reorder_frames = 0;
      // h->sps->profile_idc = H264_PROFILE_BASELINE;

      // h->sps->vui.motion_vectors_over_pic_boundaries_flag = 1;
      // h->sps->vui.matrix_coefficients = 0;

      // h->sps->log2_max_frame_num_minus4 = 4;
      // h->sps->vui.aspect_ratio_idc = 255;

      // h->sps->frame_cropping_flag = 0;
      // h->sps->frame_crop_bottom_offset = 0;
      // h->sps->level_idc = 31;
      // h->sps->profile_idc = 100;
      // h->sps->constraint_set1_flag = 0;
      // h->sps->vui.log2_max_mv_length_horizontal = 1;
      // h->sps->vui.log2_max_mv_length_vertical = 1;

      // h->sps->vui.num_units_in_tick = 1000;
      // h->sps->vui.time_scale = 60000;


      // GFE 2.5.11 changed the SPS to add additional extensions
      // Some devices don't like these so we remove them here.
      // h->sps->vui.video_signal_type_present_flag = 0;
      // h->sps->vui.chroma_loc_info_present_flag = 0;

      // // Some devices throw errors if max_dec_frame_buffering < num_ref_frames
      h->sps->vui.max_dec_frame_buffering = REF_FRAMES;

      // // These values are the default for the fields, but they are more aggressive
      // // than what GFE sends in 2.5.11, but it doesn't seem to cause picture problems.
      // h->sps->vui.max_bytes_per_pic_denom = 2;
      // h->sps->vui.max_bits_per_mb_denom = 1;

      LOGD("sps real type %d type %d starts at 0x%x ends at 0x%x length 0x%x buf size 0x%x sps size 0x%x actual size 0x%x", (*(data + sps_start) & 0x1F), h->nal->nal_unit_type, sps_start, sps_end, data_len - sps_start, data_len, sps_size, actual_sps_size);
      int new_sps_size = write_nal_unit(h, data + sps_start, data_len); // i dont know why we need data_len instead of sps_size but it works and doesnt destroy it lel
      LOGD("new size 0x%x or %d", new_sps_size, new_sps_size);
      // bool ret;
      // if (new_sps_size > 0 && new_sps_size <= sps_wiggle_room) {
      //     memcpy(modified_data, *data, sps_start);
      //     memcpy(modified_data + sps_start + new_sps_size, nal_marker, sizeof(nal_marker));
      //     memcpy(modified_data + sps_start + new_sps_size + sizeof(nal_marker), *data + sps_start, *data_len - sps_start);
      //     *data = modified_data;
      //     *data_len = *data_len + new_sps_size + sizeof(nal_marker);
      //     ret = true;
      // } else {
      //     free(modified_data);
      //     modified_data = NULL;
      //     ret = false;
      // }
      h264_free(h);
      return true;
  } else {
    LOGD("cant find SPS");
    return false;
  }
}

// uint8_t *lbuf;
// bool infirst_frame = false;
int vita_h264_decode_frame(uint8_t *buf, size_t buf_size) {
  // Early validation to detect corrupted frames before decoding
  if (buf == NULL || buf_size == 0) {
    LOGD("VIDEO: Invalid frame (NULL or zero size), skipping");
    return 1;
  }

  // Validate minimum H.264 NAL unit size (at least 5 bytes for NAL header)
  if (buf_size < 5) {
    LOGD("VIDEO: Frame too small (%zu bytes), possibly corrupted, skipping", buf_size);
    return 1;
  }

  // free(lbuf);
  // lbuf = buf;
  chiaki_mutex_lock(&mtx);
  if(!threadSetupComplete) {
		sceKernelChangeThreadPriority(SCE_KERNEL_THREAD_ID_SELF, 64);
		sceKernelChangeThreadCpuAffinityMask(SCE_KERNEL_THREAD_ID_SELF, SCE_KERNEL_CPU_MASK_USER_0);
		threadSetupComplete = true;
	}
  // if (first_frame) {
  //   first_frame = false;
  //   // infirst_frame = true;
  //   // header_buf = memalign(DECODE_AU_ALIGNMENT, buf_size);
  //   // if (header_buf == NULL) {
  //   //   LOGD("not enough memory for header buf\n");
  //   //   return 1;
  //   // }

  //   // header_buf_size = buf_size;
  //   // sceClibMemcpy(header_buf, buf, buf_size);

  //   vita_h264_process_header(buf, buf_size);
  //   // header_buf = buf;
  //   // header_buf_size = buf_size;
  //   // hexdump(buf, buf_size);

  //   // chiaki_mutex_unlock(&mtx);
  //   // return 0;
  // }



  // hexdump(buf, buf_size);



  //frame->time = decodeUnit->receiveTimeMs;



  if (buf_size > sceAvcdecDecodeAvailableSize(decoder)) {
    sceClibPrintf("Video decode buffer too small\n");
    chiaki_mutex_unlock(&mtx);
    return 1;
  }


  int ret = 0;
  // if (frame_count != 0 && frame_count % 6 == 0) {
  //   LOGD("flushing decoder");
  //   sceAvcdecDecodeFlush(decoder);
  //   au.es.pBuf = header_buf;
  //   au.es.size = header_buf_size;

  //   ret = sceAvcdecDecode(decoder, &au, &array_picture);
  //   if (ret < 0) {
  //     LOGD("sceAvcdecDecodeHeaderBuf (len=0x%x): 0x%x numOfOutput %d\n", buf_size, ret, array_picture.numOfOutput);
  //     chiaki_mutex_unlock(&mtx);
  //     return 1; // decoder screwed up for some reason, just ignore this frame
  //   }
  //   return 1;
  // }

  // PLENTRY entry = decodeUnit->bufferList;
  // uint32_t length = 0;
  // while (entry != NULL) {
    // if (entry->bufferType == BUFFER_TYPE_SPS) {
      // gs_sps_fix(entry, GS_SPS_BITSTREAM_FIXUP, decoder_buffer, &length);
    // } else {
      // sceClibMemset(decoder_buffer, 0, AU_BUF_SIZE(SCREEN_WIDTH, SCREEN_HEIGHT));
      // memset(buf, buf_size);
      // free(buf);
      // decoder_buffer = buf;
      // length += entry->length;
    // }
    // entry = entry->next;
  // }

  // au.es.size = buf_size;
  // au.dts.lower = 0xFFFFFFFF;
  // au.dts.upper = 0xFFFFFFFF;
  // au.pts.lower = 0xFFFFFFFF;
  // au.pts.upper = 0xFFFFFFFF;
  // sceClibMemcpy(decoder_buffer, buf, buf_size);

  // sceClibMemcpy(decoder_buffer, buf, buf_size);

  // au.es.pBuf = decoder_buffer;
  au.es.pBuf = buf;
  au.es.size = buf_size;
  ret = sceAvcdecDecode(decoder, &au, &array_picture);
  if (ret < 0) {
    LOGD("sceAvcdecDecode (len=0x%x): 0x%x numOfOutput %d\n", buf_size, ret, array_picture.numOfOutput);
    // if (isEdited) free(buf);
    chiaki_mutex_unlock(&mtx);
    return 0;
    // goto fix;
  }

  if (array_picture.numOfOutput != 1) {
    LOGD("numOfOutput %d bufSize 0x%x\n", array_picture.numOfOutput, buf_size);
    // if (infirst_frame) {
    //   infirst_frame = false;
    //   // if (isEdited) free(buf);
    //   chiaki_mutex_unlock(&mtx);
    //   return 0;
    // } else {
      // goto fix;
      // if (isEdited) free(buf);
      chiaki_mutex_unlock(&mtx);
      return 0;
      // chiaki_mutex_unlock(&mtx);
      // return 1;
    // }
    // if (first_frame) {
    //   first_frame = false;
    //   chiaki_mutex_unlock(&mtx);
    //   return 0;
    // }
    // goto fix;
  }
  // display:
  if (active_video_thread) {
    if (need_drop > 0) {
      LOGD("remain frameskip: %d\n", need_drop);
      // skip
      need_drop--;
    } else {
      vita2d_start_drawing();

      draw_streaming(frame_texture);
      // draw_fps();
      // draw_indicators();

      vita2d_end_drawing();

      vita2d_wait_rendering_done();
      vita2d_swap_buffers();

      frame_count++;
      // LOGD("frc: %d", frame_count);
    }
  } else {
    LOGD("inactive video thread");
  }

  // if (numframes++ % 6 == 0)
  //   return DR_NEED_IDR;
  chiaki_mutex_unlock(&mtx);
  return 0;
}

void draw_streaming(vita2d_texture *frame_texture) {
  // ui is still rendering in the background, clear the screen first
  vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, RGBA8(0, 0, 0, 255));
  vita2d_draw_texture_part(frame_texture,
                           image_scaling.origin_x,
                           image_scaling.origin_y,
                           image_scaling.region_x1,
                           image_scaling.region_y1,
                           image_scaling.region_x2,
                           image_scaling.region_y2);
}

extern vita2d_font* font;

void draw_fps() {
  // if (config.show_fps) {
    vita2d_font_draw_textf(font, 40, 20, RGBA8(0xFF, 0xFF, 0xFF, 0xFF), 16, "fps: %u / %u", curr_fps[0], curr_fps[1]);
  // }
}

void draw_indicators() {
//   if (poor_net_indicator.activated) {
//     vita2d_font_draw_text(font, 40, 500, RGBA8(0xFF, 0xFF, 0xFF, poor_net_indicator.alpha), 64, ICON_NETWORK);
//     poor_net_indicator.alpha += (0x4 * (poor_net_indicator.plus ? 1 : -1));
//     if (poor_net_indicator.alpha == 0) {
//       poor_net_indicator.plus = !poor_net_indicator.plus;
//       poor_net_indicator.alpha += (0x4 * (poor_net_indicator.plus ? 1 : -1));
//     }
//   }
}

void vita_h264_start() {
  active_video_thread = true;
	chiaki_mutex_init(&mtx, false);
  vita2d_set_vblank_wait(false);
}

void vita_h264_stop() {
  vita2d_set_vblank_wait(true);
  active_video_thread = false;
	chiaki_mutex_fini(&mtx);
}

void vitavideo_show_poor_net_indicator() {
  poor_net_indicator.activated = true;
}

void vitavideo_hide_poor_net_indicator() {
  //poor_net_indicator.activated = false;
  memset(&poor_net_indicator, 0, sizeof(indicator_status));
}

int vitavideo_initialized() {
  return video_status != NOT_INIT;
}
