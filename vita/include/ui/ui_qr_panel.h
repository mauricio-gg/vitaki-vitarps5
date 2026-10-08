/**
 * @file ui_qr_panel.h
 * @brief C18 QrPanel: a QR code on a light plate, or the "QR hidden" box (SPEC.md C18)
 *
 * Display-only helper; the screen owns the tap rect. The modules are encoded by ui_qr and drawn
 * once into one texture when the URL changes, so a frame costs one quad however long the URL is.
 * The texture is allocated at most once for the life of the app and reused.
 * Paper cost: plate 9 + QR 1 when shown; box 9 + text 1 when hidden.
 */

#pragma once

#include <stdbool.h>

/** Bytes the stored URL can hold, terminator included: the size of psn_auth's authorize URL. */
#define UI_QR_PANEL_URL_MAX 1536

/**
 * ui_qr_module_px() - The largest whole number of pixels per module that keeps a QR code of
 * @qr_size modules within @art_px pixels.
 * @return pixels per module, or 0 when the code does not fit even at 1 px per module (it must be
 *         refused, never drawn clipped)
 */
static inline int ui_qr_module_px(int qr_size, int art_px) {
  if (qr_size <= 0 || art_px <= 0)
    return 0;
  return art_px / qr_size;
}

/**
 * ui_qr_panel_set_url() - Make @url the text the panel shows as a QR code.
 * @url: The text to encode; NULL or empty clears the panel.
 *
 * Does nothing when @url is the one already set, so it may be called every frame. A new URL is
 * encoded and drawn into the texture; when it cannot be (encoding failed, it does not fit
 * UI_QR_ART at 1 px per module, no texture) the failure is logged and ui_qr_panel_ready() is false.
 * @return true when @url differs from the previous call's (so a failure can be reported once)
 */
bool ui_qr_panel_set_url(const char *url);

/** ui_qr_panel_ready() - True when a QR code for the current URL is ready to draw. */
bool ui_qr_panel_ready(void);

/**
 * ui_qr_panel_draw() - Draw the panel with its top-left corner at (@x, @y), UI_QR_BOX square.
 * @show: Draw the QR code; otherwise (or when it is not ready) the "QR hidden" box.
 */
void ui_qr_panel_draw(int x, int y, bool show);
