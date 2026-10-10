/**
 * @file ui_asset_preload.h
 * @brief Start-up PNG preload: a worker thread reads and decodes the PNGs, the main thread makes
 *        the textures (ticket #366)
 *
 * Reading a tiny file and decoding it costs far more than it should on the main thread, and the
 * splash cannot draw while it does. init_ui() starts the worker first. The worker decodes every
 * path in the list in ui_asset_preload.c (in order of first use, the logo first) into ordinary
 * RAM; it never touches vita2d. While start-up runs, ui_load_png_linear() serves a listed path
 * from those pixels: it waits if the decode is not ready, creates the texture on the calling (main)
 * thread and frees the pixels. A path not in the list is loaded directly, with a warning.
 *
 * Everything here except the worker is main thread only.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <vita2d.h>

/** The splash logo: first in the preload list, and the one image init_ui() takes itself. */
#define UI_ASSET_LOGO_PATH "app0:/assets/Vita_RPS5_Logo.png"

/** The two Roboto TTFs. The worker reads them whole (no decode) for vita2d_load_font_mem(). */
#define UI_ASSET_FONT_REGULAR_PATH "app0:/assets/fonts/Roboto-Regular.ttf"
#define UI_ASSET_FONT_LIGHT_PATH "app0:/assets/fonts/Roboto-Light.ttf"

/** A decoded image: RGBA, 4 bytes per pixel in the order R, G, B, A, rows packed. */
typedef struct {
  uint8_t *rgba; /**< Owned by the holder; free with ui_asset_pixels_free(). NULL if empty. */
  int width;
  int height;
  size_t stride_bytes; /**< Bytes from one row to the next. */
  uint64_t read_us;    /**< Worker time spent reading the file. */
  uint64_t decode_us;  /**< Worker time spent decoding it. */
} UiAssetPixels;

/** A function called while the main thread is busy loading, to let the splash draw a frame. */
typedef void (*UiAssetFrameHook)(void);

/**
 * ui_asset_preload_start() - Start the worker. Call first in init_ui().
 *
 * If the thread cannot be created it is logged as an error and the list is decoded on the calling
 * thread instead, so every later call still works (slower).
 */
void ui_asset_preload_start(void);

/**
 * ui_asset_preload_finish() - Join the worker, clear the frame hook and end the preload.
 *
 * Decoded images nobody asked for are freed and logged as warnings with their path. After this
 * ui_load_png_linear() loads directly, as it did before the preload existed.
 */
void ui_asset_preload_finish(void);

/**
 * ui_asset_preload_set_frame_hook() - Set (or, with NULL, clear) the function
 * ui_asset_preload_pump() calls. ui.c sets it after the first splash frame.
 */
void ui_asset_preload_set_frame_hook(UiAssetFrameHook hook);

/** ui_asset_preload_pump() - Call the frame hook, if one is set. Called after each texture
 * creation, while waiting for a decode, and after each baked texture (ui_bake_white()). */
void ui_asset_preload_pump(void);

/**
 * ui_asset_preload_wait() - Take the decoded pixels of @path, waiting for the worker if needed.
 * @path: A path in the preload list.
 * @out:  Receives the pixels; the caller frees them with ui_asset_pixels_free(). rgba is NULL
 *        when the read or decode failed (already logged with the path and the reason).
 *
 * Pumps the frame hook while it waits.
 *
 * @return true if @path is in the list and had an unclaimed use left, false otherwise (@out is
 *         left empty and nothing was logged)
 */
bool ui_asset_preload_wait(const char *path, UiAssetPixels *out);

/**
 * ui_asset_preload_take_font() - Take the bytes of a font file the worker read, waiting if needed.
 * @path:  UI_ASSET_FONT_REGULAR_PATH or UI_ASSET_FONT_LIGHT_PATH.
 * @bytes: Receives the malloc'd file contents; ownership passes to the caller, who must keep them
 *         for as long as a font opened from them lives (FreeType reads them on demand).
 * @size:  Receives the byte count.
 *
 * Pumps the frame hook while it waits. Logs the PIPE/ASSET line, or the failure with its path.
 *
 * @return true with @bytes set; false (@bytes NULL) when the preload is not running, the path is
 *         not a listed font, it was already taken, or the read failed. The caller then falls back
 *         to opening the file directly.
 */
bool ui_asset_preload_take_font(const char *path, uint8_t **bytes, unsigned int *size);

/**
 * ui_asset_preload_upload() - Create the vita2d texture for decoded pixels (main thread).
 * @px:   Pixels from ui_asset_preload_wait().
 * @path: For the PIPE/ASSET log line.
 *
 * The texture is RGBA8 (SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR) with linear filters, the same as
 * vita2d_load_PNG_file() plus ui_load_png_linear() give. Does not free @px. Logs the PIPE/ASSET
 * line and then pumps the frame hook.
 *
 * @return the texture, or NULL when it could not be allocated (logged)
 */
vita2d_texture *ui_asset_preload_upload(const UiAssetPixels *px, const char *path);

/** ui_asset_pixels_free() - Free @px->rgba and empty @px. Safe on an empty one. */
void ui_asset_pixels_free(UiAssetPixels *px);
