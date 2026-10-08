/**
 * @file ui_draw_stats.h
 * @brief Draw-call counter for the testing build (ticket #300)
 *
 * Counts the real GPU draws of each frame so the draw budget in FEASIBILITY.md section 4 can be
 * checked on hardware. Every vita2d primitive ends in exactly one sceGxmDraw, so a link-time
 * wrap of sceGxmDraw (vita/CMakeLists.txt, only when VITARPS5_DEBUG_TOOLS is set) sees them all.
 *
 * Besides the raw count it reports a logical count that matches the paper method: vita2d draws
 * every glyph as its own quad, the paper counts one text run as one draw, so
 * logical = raw - glyph quads + text runs.
 *
 * Everything here exists only when VITARPS5_DEBUG_TOOLS is 1. In a release build the macros
 * expand to nothing and the module is not compiled or linked.
 */

#pragma once

#include "debug_tools.h"

#if VITARPS5_DEBUG_TOOLS

#include <stdint.h>

/** Raw sceGxmDraw count since the process started. */
uint32_t ui_draw_stats_raw(void);

/** Marks the end of one text run that started when the raw count was @p raw_before. */
void ui_draw_stats_text_done(uint32_t raw_before);

/** Starts a frame's counters. Call right after the screen is cleared. */
void ui_draw_stats_frame_begin(void);

/**
 * Ends a frame. When @p screen is not NULL the frame is a candidate for the once-a-second log
 * line (the heaviest candidate frame of the second, with its screen name and the number of
 * frames seen).
 */
void ui_draw_stats_frame_end(const char *screen);

#define UI_DRAW_STATS_FRAME_BEGIN() ui_draw_stats_frame_begin()
#define UI_DRAW_STATS_FRAME_END(screen) ui_draw_stats_frame_end(screen)

/** Runs one text-drawing call and counts it as one text run with its glyph quads. */
#define UI_DRAW_STATS_TEXT(draw_call)                    \
  do {                                                   \
    uint32_t ui_draw_stats_before = ui_draw_stats_raw(); \
    draw_call;                                           \
    ui_draw_stats_text_done(ui_draw_stats_before);       \
  } while (0)

#else

#define UI_DRAW_STATS_FRAME_BEGIN() ((void)0)
#define UI_DRAW_STATS_FRAME_END(screen) ((void)0)
#define UI_DRAW_STATS_TEXT(draw_call) draw_call

#endif /* VITARPS5_DEBUG_TOOLS */
