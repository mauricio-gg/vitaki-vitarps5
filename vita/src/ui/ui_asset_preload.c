/**
 * @file ui_asset_preload.c
 * @brief Start-up PNG preload (see ui_asset_preload.h)
 *
 * Hand-off: the worker owns a slot until it sets the slot's `ready` flag under the mutex and
 * broadcasts; the main thread looks at a slot's pixels only after it has seen `ready` under the
 * same mutex, so it never reads a half-written slot. Nothing else is shared: the use counters and
 * log flags belong to the main thread, and the worker only writes (and logs nothing; the message
 * log is not thread safe, so failures are logged by the main thread when it meets them).
 */

#include "ui/ui_asset_preload.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chiaki/thread.h>
#include <png.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "context.h"
#include "ui/ui_theme.h"

#define BYTES_PER_PIXEL 4
#define FILLER_OPAQUE 0xFF
#define PRELOAD_THREAD_NAME "VitaUiPreload"
#define PRELOAD_ERR_LEN 96
#define PRELOAD_NO_FLAGS 0
#define PRELOAD_NO_CORE_MASK 0

#define ASSETS "app0:/assets/"
#define ICONS ASSETS "icons/"
#define GLYPHS ASSETS "glyphs/"

/** One PNG loaded during start-up. */
typedef struct {
  const char *path;
  int uses; /**< How many times start-up loads this path; its pixels are kept for all of them. */
} PreloadEntry;

/*
 * Every PNG loaded while start-up runs, in order of first use, the logo first. The order is the
 * STARTUP_STEPS order in ui.c, and inside a step the order its init function loads in. A path
 * missing here still loads (directly, with a "not preloaded" warning); a path listed here that
 * nobody loads is logged at the end of start-up. Keep this in step with the loaders.
 */
static const PreloadEntry PRELOAD_LIST[] = {
    {UI_ASSET_LOGO_PATH, 1},
    /* step textures */
    {ASSETS "ps4.png", 1},
    {ASSETS "symbol_triangle.png", 1},
    {ASSETS "symbol_circle.png", 1},
    {ASSETS "symbol_ex.png", 1},
    {ASSETS "symbol_square.png", 1},
    {ASSETS "icon_play.png", 1},
    {ASSETS "icon_settings.png", 1},
    {ASSETS "PS5_logo.png", 1},
    /* step room_icons: each room at the row, grid and ring size */
    {ICONS "room_tv_38.png", 1},
    {ICONS "room_tv_48.png", 1},
    {ICONS "room_tv_64.png", 1},
    {ICONS "room_sofa_38.png", 1},
    {ICONS "room_sofa_48.png", 1},
    {ICONS "room_sofa_64.png", 1},
    {ICONS "room_bed_38.png", 1},
    {ICONS "room_bed_48.png", 1},
    {ICONS "room_bed_64.png", 1},
    {ICONS "room_bunk_38.png", 1},
    {ICONS "room_bunk_48.png", 1},
    {ICONS "room_bunk_64.png", 1},
    {ICONS "room_desk_38.png", 1},
    {ICONS "room_desk_48.png", 1},
    {ICONS "room_desk_64.png", 1},
    {ICONS "room_house_38.png", 1},
    {ICONS "room_house_48.png", 1},
    {ICONS "room_house_64.png", 1},
    /* step result_popup (also loaded by toast and list_popup: check 3 times, warn twice) */
    {ICONS "popup_check.png", 3},
    {ICONS "popup_warn.png", 2},
    /* step home: item icons (search is also the Pair popup's icon), category icons, top bar and
     * hint row */
    {ICONS "video.png", 1},
    {ICONS "network.png", 1},
    {ICONS "display.png", 1},
    {ICONS "controls.png", 1},
    {ICONS "advanced.png", 1},
    {ICONS "account.png", 1},
    {ICONS "connection.png", 1},
    {ICONS "psn.png", 1},
    {ICONS "slot1.png", 1},
    {ICONS "slot2.png", 1},
    {ICONS "slot3.png", 1},
    {ICONS "search.png", 2},
    {ICONS "plus.png", 1},
    {ICONS "controller.png", 1},
    {ICONS "profile.png", 1},
    {ICONS "wifi.png", 1},
    {ICONS "battery.png", 1},
    {GLYPHS "hint_l.png", 1},
    {GLYPHS "hint_r.png", 1},
    {GLYPHS "hint_start.png", 1},
    /* step connecting: the page frame icons */
    {ICONS "page_lan.png", 1},
    {ICONS "page_globe.png", 1},
    {ICONS "page_moon.png", 1},
    {ICONS "page_wifi.png", 1},
    {ICONS "page_gear.png", 1},
    {ICONS "page_lock.png", 1},
    {ICONS "page_profile.png", 1},
    {ICONS "page_controller.png", 1},
};

#define PRELOAD_COUNT (sizeof(PRELOAD_LIST) / sizeof(PRELOAD_LIST[0]))

/** The result for one list entry. */
typedef struct {
  UiAssetPixels px;
  char err[PRELOAD_ERR_LEN]; /**< Why the read or decode failed; empty when it did not. */
  bool ready;                /**< Worker done with this slot. Guarded by s_mutex. */
  int uses_left;             /**< Main thread only. */
  bool err_logged;           /**< Main thread only. */
} Slot;

static struct {
  bool active;
  bool thread_started;
  SceUID thread;
  ChiakiMutex mutex;
  ChiakiCond cond;
  UiAssetFrameHook hook;
  Slot slots[PRELOAD_COUNT];
} s_pre;

/* ============================================================================
 * Worker: read a file, decode it (no vita2d, no logging)
 * ============================================================================ */

/** Reads a whole file in one read. Returns the malloc'd bytes and sets @size, or NULL with @err. */
static uint8_t *read_file(const char *path, size_t *size, char *err) {
  SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
  if (fd < 0) {
    snprintf(err, PRELOAD_ERR_LEN, "sceIoOpen failed 0x%08x", (unsigned int)fd);
    return NULL;
  }
  SceOff end = sceIoLseek(fd, 0, SCE_SEEK_END);
  if (end <= 0 || sceIoLseek(fd, 0, SCE_SEEK_SET) < 0) {
    snprintf(err, PRELOAD_ERR_LEN, "sceIoLseek found no file size (%d)", (int)end);
    sceIoClose(fd);
    return NULL;
  }
  uint8_t *bytes = malloc((size_t)end);
  if (!bytes) {
    snprintf(err, PRELOAD_ERR_LEN, "out of memory for %u file bytes", (unsigned int)end);
    sceIoClose(fd);
    return NULL;
  }
  int got = sceIoRead(fd, bytes, (SceSize)end);
  sceIoClose(fd);
  if (got != (int)end) {
    snprintf(err, PRELOAD_ERR_LEN, "sceIoRead returned %d of %u bytes", got, (unsigned int)end);
    free(bytes);
    return NULL;
  }
  *size = (size_t)end;
  return bytes;
}

/** State of one decode, reachable from libpng's callbacks. */
typedef struct {
  const uint8_t *data;
  size_t size;
  size_t pos;
  char *err; /**< PRELOAD_ERR_LEN bytes; set by the error callback. */
  png_structp png;
  png_infop info;
  uint8_t *rgba;
  png_bytep *rows;
} Decode;

/** libpng read callback: copy from the file bytes in RAM. */
static void png_read_memory(png_structp png, png_bytep out, png_size_t count) {
  Decode *d = png_get_io_ptr(png);
  if (count > d->size - d->pos)
    png_error(png, "unexpected end of file");
  memcpy(out, d->data + d->pos, count);
  d->pos += count;
}

/** libpng error callback: keep the message, then unwind to the setjmp in decode_png(). */
static void png_fail(png_structp png, png_const_charp message) {
  Decode *d = png_get_error_ptr(png);
  snprintf(d->err, PRELOAD_ERR_LEN, "libpng: %s", message);
  png_longjmp(png, 1);
}

/** libpng warning callback: ignored. Harmless warnings (an sRGB profile libpng dislikes) would
 * otherwise flood the log, one per icon. */
static void png_ignore_warning(png_structp png, png_const_charp message) {
  (void)png;
  (void)message;
}

/** Frees what a decode allocated, then the state itself. The pixels survive when @keep_pixels. */
static void decode_finish(Decode *d, bool keep_pixels) {
  png_destroy_read_struct(&d->png, &d->info, NULL);
  free(d->rows);
  if (!keep_pixels)
    free(d->rgba);
  free(d);
}

/**
 * Decodes PNG @bytes into tightly packed RGBA8 in @px, the way vita2d_load_PNG_file() does
 * (palette, gray, 16-bit and transparency chunks all become 8-bit RGBA; interlace is handled).
 * On failure @err holds the reason and @px->rgba stays NULL.
 *
 * The state is on the heap, not the stack: libpng errors longjmp back to the setjmp below, and a
 * local changed after setjmp would have an undefined value there.
 */
static void decode_png(const uint8_t *bytes, size_t size, UiAssetPixels *px, char *err) {
  Decode *d = calloc(1, sizeof(*d));
  if (!d) {
    snprintf(err, PRELOAD_ERR_LEN, "out of memory for the decode state");
    return;
  }
  d->data = bytes;
  d->size = size;
  d->err = err;
  d->png = png_create_read_struct(PNG_LIBPNG_VER_STRING, d, png_fail, png_ignore_warning);
  d->info = d->png ? png_create_info_struct(d->png) : NULL;
  if (!d->png || !d->info) {
    snprintf(err, PRELOAD_ERR_LEN, "libpng could not allocate its read state");
    decode_finish(d, false);
    return;
  }
  if (setjmp(png_jmpbuf(d->png))) {
    decode_finish(d, false); /* png_fail() wrote the reason */
    return;
  }

  png_set_read_fn(d->png, d, png_read_memory);
  png_read_info(d->png, d->info);
  png_uint_32 width, height;
  int depth, color, interlace;
  png_get_IHDR(d->png, d->info, &width, &height, &depth, &color, &interlace, NULL, NULL);
  if (width == 0 || height == 0 || width > UI_PRELOAD_MAX_SIDE || height > UI_PRELOAD_MAX_SIDE)
    png_error(d->png, "image size out of range");

  const bool has_trns = png_get_valid(d->png, d->info, PNG_INFO_tRNS) != 0;
  if (depth == 16)
    png_set_strip_16(d->png);
  if (color == PNG_COLOR_TYPE_PALETTE)
    png_set_palette_to_rgb(d->png);
  if (color == PNG_COLOR_TYPE_GRAY && depth < 8)
    png_set_expand_gray_1_2_4_to_8(d->png);
  if (has_trns)
    png_set_tRNS_to_alpha(d->png);
  if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb(d->png);
  if (!(color & PNG_COLOR_MASK_ALPHA) && !has_trns)
    png_set_filler(d->png, FILLER_OPAQUE, PNG_FILLER_AFTER);
  png_set_interlace_handling(d->png);
  png_read_update_info(d->png, d->info);

  const size_t stride = (size_t)width * BYTES_PER_PIXEL;
  if (png_get_rowbytes(d->png, d->info) != stride)
    png_error(d->png, "unexpected pixel layout after conversion");
  d->rgba = malloc(stride * height);
  d->rows = malloc(height * sizeof(png_bytep));
  if (!d->rgba || !d->rows)
    png_error(d->png, "out of memory for the pixels");
  for (png_uint_32 y = 0; y < height; y++)
    d->rows[y] = d->rgba + y * stride;
  png_read_image(d->png, d->rows);
  png_read_end(d->png, NULL);

  px->rgba = d->rgba;
  px->width = (int)width;
  px->height = (int)height;
  px->stride_bytes = stride;
  decode_finish(d, true);
}

/** Reads and decodes list entry @index into its slot, with timings. Worker (or fallback) only. */
static void load_slot(size_t index) {
  Slot *slot = &s_pre.slots[index];
  const uint64_t t0 = sceKernelGetProcessTimeWide();
  size_t size = 0;
  uint8_t *bytes = read_file(PRELOAD_LIST[index].path, &size, slot->err);
  const uint64_t t1 = sceKernelGetProcessTimeWide();
  slot->px.read_us = t1 - t0;
  if (bytes) {
    decode_png(bytes, size, &slot->px, slot->err);
    free(bytes);
    slot->px.decode_us = sceKernelGetProcessTimeWide() - t1;
  }
}

/** Decodes the whole list in order, publishing each slot as it is done. */
static void load_all(void) {
  for (size_t i = 0; i < PRELOAD_COUNT; i++) {
    load_slot(i);
    chiaki_mutex_lock(&s_pre.mutex);
    s_pre.slots[i].ready = true;
    chiaki_cond_broadcast(&s_pre.cond);
    chiaki_mutex_unlock(&s_pre.mutex);
  }
}

/** Thread entry. */
static int worker_main(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  load_all();
  return 0;
}

/* ============================================================================
 * Main thread
 * ============================================================================ */

void ui_asset_preload_start(void) {
  memset(&s_pre, 0, sizeof(s_pre));
  for (size_t i = 0; i < PRELOAD_COUNT; i++)
    s_pre.slots[i].uses_left = PRELOAD_LIST[i].uses;
  if (chiaki_mutex_init(&s_pre.mutex, false) != CHIAKI_ERR_SUCCESS ||
      chiaki_cond_init(&s_pre.cond, &s_pre.mutex) != CHIAKI_ERR_SUCCESS) {
    LOGE("UI/PRELOAD could not create the hand-off lock, loading PNGs directly");
    return;
  }
  s_pre.active = true;

  int priority = sceKernelGetThreadCurrentPriority() + UI_PRELOAD_THREAD_PRIO_STEP;
  if (priority > UI_PRELOAD_THREAD_PRIO_LOWEST)
    priority = UI_PRELOAD_THREAD_PRIO_LOWEST;
  s_pre.thread =
      sceKernelCreateThread(PRELOAD_THREAD_NAME, worker_main, priority, UI_PRELOAD_THREAD_STACK,
                            PRELOAD_NO_FLAGS, PRELOAD_NO_CORE_MASK, NULL);
  if (s_pre.thread >= 0 && sceKernelStartThread(s_pre.thread, 0, NULL) >= 0) {
    s_pre.thread_started = true;
    return;
  }
  LOGE("UI/PRELOAD could not start the worker thread (0x%08x), decoding on the main thread",
       (unsigned int)s_pre.thread);
  if (s_pre.thread >= 0)
    sceKernelDeleteThread(s_pre.thread);
  load_all();
}

void ui_asset_preload_set_frame_hook(UiAssetFrameHook hook) {
  s_pre.hook = hook;
}

void ui_asset_preload_pump(void) {
  if (s_pre.hook)
    s_pre.hook();
}

/** Index of the first slot for @path that still has a use left, or PRELOAD_COUNT. */
static size_t find_slot(const char *path) {
  for (size_t i = 0; i < PRELOAD_COUNT; i++)
    if (s_pre.slots[i].uses_left > 0 && strcmp(PRELOAD_LIST[i].path, path) == 0)
      return i;
  return PRELOAD_COUNT;
}

/** True once the worker has published slot @index. */
static bool slot_ready(size_t index) {
  chiaki_mutex_lock(&s_pre.mutex);
  const bool ready = s_pre.slots[index].ready;
  chiaki_mutex_unlock(&s_pre.mutex);
  return ready;
}

/** Waits until slot @index is ready, giving the frame hook a turn every UI_PRELOAD_WAIT_MS. */
static void wait_ready(size_t index) {
  while (!slot_ready(index)) {
    ui_asset_preload_pump();
    chiaki_mutex_lock(&s_pre.mutex);
    if (!s_pre.slots[index].ready)
      chiaki_cond_timedwait(&s_pre.cond, &s_pre.mutex, UI_PRELOAD_WAIT_MS);
    chiaki_mutex_unlock(&s_pre.mutex);
  }
}

/** Logs a slot's failure once. */
static void log_failure(size_t index) {
  Slot *slot = &s_pre.slots[index];
  if (slot->px.rgba || slot->err_logged)
    return;
  slot->err_logged = true;
  LOGE("UI/PRELOAD failed to load '%s': %s", PRELOAD_LIST[index].path, slot->err);
}

bool ui_asset_preload_wait(const char *path, UiAssetPixels *out) {
  *out = (UiAssetPixels){0};
  if (!s_pre.active)
    return false;
  const size_t index = find_slot(path);
  if (index == PRELOAD_COUNT)
    return false;

  wait_ready(index);
  Slot *slot = &s_pre.slots[index];
  log_failure(index);
  slot->uses_left--;
  if (!slot->px.rgba)
    return true;

  if (slot->uses_left > 0) {
    /* Later users need the pixels too: hand out a copy. */
    const size_t bytes = slot->px.stride_bytes * (size_t)slot->px.height;
    out->rgba = malloc(bytes);
    if (!out->rgba) {
      LOGE("UI/PRELOAD out of memory copying %u bytes for '%s'", (unsigned int)bytes, path);
      return true;
    }
    memcpy(out->rgba, slot->px.rgba, bytes);
    out->width = slot->px.width;
    out->height = slot->px.height;
    out->stride_bytes = slot->px.stride_bytes;
    out->read_us = slot->px.read_us;
    out->decode_us = slot->px.decode_us;
    return true;
  }
  *out = slot->px;
  slot->px = (UiAssetPixels){0};
  return true;
}

vita2d_texture *ui_asset_preload_upload(const UiAssetPixels *px, const char *path) {
  const uint64_t t0 = sceKernelGetProcessTimeWide();
  vita2d_texture *tex =
      vita2d_create_empty_texture((unsigned int)px->width, (unsigned int)px->height);
  if (!tex) {
    LOGE("UI/PRELOAD could not allocate a %dx%d texture for '%s'", px->width, px->height, path);
    return NULL;
  }
  const size_t stride = vita2d_texture_get_stride(tex);
  uint8_t *dst = vita2d_texture_get_datap(tex);
  for (int y = 0; y < px->height; y++)
    memcpy(dst + (size_t)y * stride, px->rgba + (size_t)y * px->stride_bytes,
           (size_t)px->width * BYTES_PER_PIXEL);
  vita2d_texture_set_filters(tex, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
  LOGD("PIPE/ASSET path=%s read_us=%llu decode_us=%llu upload_us=%llu w=%d h=%d", path,
       (unsigned long long)px->read_us, (unsigned long long)px->decode_us,
       (unsigned long long)(sceKernelGetProcessTimeWide() - t0), px->width, px->height);
  ui_asset_preload_pump();
  return tex;
}

void ui_asset_pixels_free(UiAssetPixels *px) {
  free(px->rgba);
  *px = (UiAssetPixels){0};
}

void ui_asset_preload_finish(void) {
  if (!s_pre.active)
    return;
  s_pre.hook = NULL;
  if (s_pre.thread_started) {
    sceKernelWaitThreadEnd(s_pre.thread, NULL, NULL);
    sceKernelDeleteThread(s_pre.thread);
  }
  for (size_t i = 0; i < PRELOAD_COUNT; i++) {
    Slot *slot = &s_pre.slots[i];
    log_failure(i);
    if (slot->uses_left > 0)
      LOGW("UI/PRELOAD '%s' was decoded but %d of %d loads never asked for it",
           PRELOAD_LIST[i].path, slot->uses_left, PRELOAD_LIST[i].uses);
    ui_asset_pixels_free(&slot->px);
  }
  chiaki_cond_fini(&s_pre.cond);
  chiaki_mutex_fini(&s_pre.mutex);
  s_pre.active = false;
}

vita2d_texture *ui_load_png_linear(const char *path) {
  UiAssetPixels px;
  if (ui_asset_preload_wait(path, &px)) {
    vita2d_texture *tex = px.rgba ? ui_asset_preload_upload(&px, path) : NULL;
    ui_asset_pixels_free(&px);
    return tex;
  }
  if (s_pre.active)
    LOGW("UI/PRELOAD '%s' not preloaded, loading it directly", path);
  vita2d_texture *tex = vita2d_load_PNG_file(path);
  if (!tex) {
    LOGE("ui_load_png_linear: failed to load '%s'", path);
    return NULL;
  }
  vita2d_texture_set_filters(tex, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
  return tex;
}
