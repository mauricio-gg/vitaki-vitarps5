// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#ifndef VITARPS5_VIDEO_GEOMETRY_H
#define VITARPS5_VIDEO_GEOMETRY_H

/* Stream sizing: how the decoder is set up for a stream size, the size of its output texture, and
 * where the visible stream area is drawn. Pure math, no Vita calls. */

#include "video.h"

#define VIDEO_GEOMETRY_SCREEN_W 960u
#define VIDEO_GEOMETRY_SCREEN_H 544u

typedef struct {
  unsigned int texture_width;  /* what sceAvcdec writes into: frameWidth/framePitch */
  unsigned int texture_height; /* frameHeight */
  unsigned int source_width;   /* visible stream area inside the texture */
  unsigned int source_height;
  float origin_x; /* where on screen the area is drawn (aspect-fit mode) */
  float origin_y;
  float dest_width; /* size on screen (aspect-fit mode) */
  float dest_height;
} VideoGeometry;

/* Reference-frame counts the hardware accepted at each size (docs/design/streaming.md section 5):
 * 540p keeps REF_FRAMES; the decoder refuses more than 5 at 1280x720; above that the Internal
 * entry points take what the H.264 level allows, 32768 / macroblocks (4 at 1920x1088). */
#define VIDEO_GEOMETRY_REFS_720P 5
#define VIDEO_GEOMETRY_LEVEL_MB_BUDGET 32768
/* Macroblocks of 1280x720: the largest size the public decoder entry points handle. */
#define VIDEO_GEOMETRY_PUBLIC_MAX_MBS 3600u

typedef struct {
  int use_internal; /* 1: the firmware's Internal entry points; 0: the public ones */
  int ref_frames;   /* numOfRefFrames the decoder is set up with */
} VideoDecoderPlan;

/* Which decoder entry points and how many reference frames a stream size gets. The decoder size is
 * the stream size rounded to 16 (1080 becomes 1088). */
static inline VideoDecoderPlan video_geometry_decoder_plan(int width, int height) {
  unsigned int mbs = ((unsigned int)VITA_DECODER_RESOLUTION(width) / 16u) *
                     ((unsigned int)VITA_DECODER_RESOLUTION(height) / 16u);
  VideoDecoderPlan plan;
  if (mbs > VIDEO_GEOMETRY_PUBLIC_MAX_MBS) {
    plan.use_internal = 1;
    plan.ref_frames = VIDEO_GEOMETRY_LEVEL_MB_BUDGET / (int)mbs;
  } else if (width > (int)VIDEO_GEOMETRY_SCREEN_W) {
    plan.use_internal = 0;
    plan.ref_frames = VIDEO_GEOMETRY_REFS_720P;
  } else {
    plan.use_internal = 0;
    plan.ref_frames = REF_FRAMES;
  }
  return plan;
}

/* The texture is the decoder's frame (stream size rounded to 16, so 1080 becomes 1088: the
 * decoder is created with that size and writes whole macroblock rows), but never smaller than the
 * screen. So 960x540 stays 960x544 as before; 1280x720 is 1280x720; 1920x1080 is 1920x1088. */
static inline VideoGeometry video_geometry_compute(int width, int height) {
  VideoGeometry g;
  unsigned int dec_w = (unsigned int)VITA_DECODER_RESOLUTION(width);
  unsigned int dec_h = (unsigned int)VITA_DECODER_RESOLUTION(height);
  g.texture_width = dec_w > VIDEO_GEOMETRY_SCREEN_W ? dec_w : VIDEO_GEOMETRY_SCREEN_W;
  g.texture_height = dec_h > VIDEO_GEOMETRY_SCREEN_H ? dec_h : VIDEO_GEOMETRY_SCREEN_H;
  g.source_width = (unsigned int)width;
  g.source_height = (unsigned int)height;
  if (g.source_width > g.texture_width)
    g.source_width = g.texture_width;
  if (g.source_height > g.texture_height)
    g.source_height = g.texture_height;

  /* Aspect-fit: shrink to the screen, never enlarge. */
  float scale_w = (float)VIDEO_GEOMETRY_SCREEN_W / (float)width;
  float scale_h = (float)VIDEO_GEOMETRY_SCREEN_H / (float)height;
  float scale = scale_w < scale_h ? scale_w : scale_h;
  if (scale > 1.0f)
    scale = 1.0f;
  g.dest_width = g.source_width * scale;
  g.dest_height = g.source_height * scale;
  g.origin_x = (float)round((VIDEO_GEOMETRY_SCREEN_W - g.dest_width) / 2.0f);
  g.origin_y = (float)round((VIDEO_GEOMETRY_SCREEN_H - g.dest_height) / 2.0f);
  return g;
}

#endif
