#include "ui/ui_draw_stats.h"

#if VITARPS5_DEBUG_TOOLS

#include <psp2/gxm.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "context.h"

/* Log one line per second, in microseconds. */
#define DRAW_STATS_LOG_INTERVAL_US 1000000ULL

/* Provided by the linker for -Wl,--wrap=sceGxmDraw (vita/CMakeLists.txt). */
int __real_sceGxmDraw(SceGxmContext *context, SceGxmPrimitiveType primType,
                      SceGxmIndexFormat indexType, const void *indexData, unsigned int indexCount);

typedef struct {
  uint32_t raw;
  uint32_t runs;
  uint32_t glyphs;
} FrameCounts;

/* Only the UI thread draws, so plain counters are enough. */
static uint32_t s_raw_total = 0;
static FrameCounts s_frame;
static FrameCounts s_peak;
static const char *s_peak_screen = "";
static uint32_t s_peak_frames = 0;
static uint64_t s_window_start_us = 0;

/* Frame time and named work (ticket #353). Written only by the UI thread: other threads are
 * rejected by the thread-id check before they touch anything. */
static SceUID s_ui_thread_id = 0;
static uint64_t s_last_frame_end_us = 0;
static uint64_t s_worst_interval_us = 0;
static const char *s_work_name = NULL;
static uint64_t s_work_us = 0;

int __wrap_sceGxmDraw(SceGxmContext *context, SceGxmPrimitiveType primType,
                      SceGxmIndexFormat indexType, const void *indexData, unsigned int indexCount) {
  s_raw_total++;
  return __real_sceGxmDraw(context, primType, indexType, indexData, indexCount);
}

uint32_t ui_draw_stats_raw(void) {
  return s_raw_total;
}

void ui_draw_stats_text_done(uint32_t raw_before) {
  s_frame.runs++;
  s_frame.glyphs += s_raw_total - raw_before;
}

void ui_draw_stats_frame_begin(void) {
  s_frame.raw = s_raw_total;
  s_frame.runs = 0;
  s_frame.glyphs = 0;
}

/** Logical draws of a frame: raw draws, with each text run's glyph quads counted as one draw. */
static uint32_t logical_of(const FrameCounts *f) {
  return f->raw - f->glyphs + f->runs;
}

uint64_t ui_draw_stats_now_us(void) {
  return sceKernelGetProcessTimeWide();
}

void ui_draw_stats_work_note(const char *name, uint64_t us) {
  if (s_ui_thread_id == 0 || sceKernelGetThreadId() != s_ui_thread_id)
    return;
  if (us > s_work_us) {
    s_work_name = name;
    s_work_us = us;
  }
}

/**
 * Measures the interval since the previous frame end, tracks the window's worst one and logs a
 * UI/SLOW_FRAME line when it is over UI_SLOW_FRAME_US. Clears the work note either way, so a note
 * belongs to the interval that ends at the next frame end.
 */
static void track_frame_interval(const char *screen) {
  const uint64_t now_us = sceKernelGetProcessTimeWide();
  if (s_ui_thread_id == 0)
    s_ui_thread_id = sceKernelGetThreadId();
  else {
    const uint64_t interval_us = now_us - s_last_frame_end_us;
    if (interval_us > s_worst_interval_us)
      s_worst_interval_us = interval_us;
    if (interval_us > UI_SLOW_FRAME_US) {
      if (s_work_name)
        LOGD("UI/SLOW_FRAME us=%llu screen=%s work=%s:%llu", (unsigned long long)interval_us,
             screen ? screen : "other", s_work_name, (unsigned long long)s_work_us);
      else
        LOGD("UI/SLOW_FRAME us=%llu screen=%s work=none", (unsigned long long)interval_us,
             screen ? screen : "other");
    }
  }
  s_last_frame_end_us = now_us;
  s_work_name = NULL;
  s_work_us = 0;
}

void ui_draw_stats_frame_end(const char *screen) {
  track_frame_interval(screen);
  if (!screen)
    return;

  s_frame.raw = s_raw_total - s_frame.raw;
  s_peak_frames++;
  if (logical_of(&s_frame) >= logical_of(&s_peak)) {
    s_peak = s_frame;
    s_peak_screen = screen;
  }

  uint64_t now_us = sceKernelGetProcessTimeWide();
  if (now_us - s_window_start_us < DRAW_STATS_LOG_INTERVAL_US)
    return;

  LOGD("UI/DRAWS screen=%s raw=%u logical=%u runs=%u glyphs=%u frames=%u worst_us=%llu",
       s_peak_screen, s_peak.raw, logical_of(&s_peak), s_peak.runs, s_peak.glyphs, s_peak_frames,
       (unsigned long long)s_worst_interval_us);
  s_worst_interval_us = 0;
  s_window_start_us = now_us;
  s_peak_frames = 0;
  s_peak = (FrameCounts){0, 0, 0};
}

#endif /* VITARPS5_DEBUG_TOOLS */
