/**
 * @file ui_qr_panel.c
 * @brief C18 QrPanel (SPEC.md C18)
 */

#include "ui/ui_qr_panel.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <vita2d.h>

#include "context.h"
#include "ui/ui_qr.h"
#include "ui/ui_shapes.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"

static const char HIDDEN_TEXT[] = "QR hidden";

/** The modules, white where the code is dark, tinted UI_QR_INK at draw time. UI_QR_ART square. */
static vita2d_texture *s_art = NULL;
static char s_url[UI_QR_PANEL_URL_MAX];
static UIQrCode s_qr;
static bool s_ready = false;
/** Width of HIDDEN_TEXT, measured on the first hidden draw. */
static int s_hidden_w = -1;

/** Create the art texture the first time it is needed. @return true when it exists */
static bool ensure_art(void) {
  if (s_art)
    return true;
  s_art = vita2d_create_empty_texture(UI_QR_ART, UI_QR_ART);
  if (!s_art) {
    LOGE("QR panel: could not allocate the %dx%d art texture", UI_QR_ART, UI_QR_ART);
    return false;
  }
  vita2d_texture_set_filters(s_art, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
  return true;
}

/** Fill the art texture with the modules of s_qr, @module_px pixels each, centred. */
static void render_modules(int module_px) {
  const int stride_px = (int)vita2d_texture_get_stride(s_art) / (int)sizeof(uint32_t);
  uint32_t *pixels = (uint32_t *)vita2d_texture_get_datap(s_art);
  const int origin = (UI_QR_ART - s_qr.size * module_px) / 2;
  const uint32_t on = RGBA8(255, 255, 255, 255);

  for (int y = 0; y < UI_QR_ART; y++)
    memset(&pixels[y * stride_px], 0, UI_QR_ART * sizeof(uint32_t));
  for (int row = 0; row < s_qr.size; row++) {
    for (int col = 0; col < s_qr.size; col++) {
      if (!qrcodegen_getModule(s_qr.qrcode, col, row))
        continue;
      for (int dy = 0; dy < module_px; dy++) {
        for (int dx = 0; dx < module_px; dx++)
          pixels[(origin + row * module_px + dy) * stride_px + origin + col * module_px + dx] = on;
      }
    }
  }
}

/** Encode s_url and draw it into the art texture; sets s_ready and logs how it went. */
static void build(void) {
  s_ready = false;
  if (!s_url[0])
    return;
  const int url_len = (int)strlen(s_url);
  if (!ui_qr_encode_text(s_url, &s_qr)) {
    LOGE("QR panel: could not encode a %d character URL", url_len);
    return;
  }
  const int module_px = ui_qr_module_px(s_qr.size, UI_QR_ART);
  if (module_px == 0) {
    LOGE("QR panel: a %dx%d code for a %d character URL does not fit %d px", s_qr.size, s_qr.size,
         url_len, UI_QR_ART);
    return;
  }
  if (!ensure_art())
    return;
  render_modules(module_px);
  s_ready = true;
  LOGD("QR panel: %dx%d modules at %d px each (%d px of %d), URL %d characters", s_qr.size,
       s_qr.size, module_px, s_qr.size * module_px, UI_QR_ART, url_len);
}

bool ui_qr_panel_set_url(const char *url) {
  if (!url)
    url = "";
  if (strncmp(s_url, url, sizeof(s_url)) == 0)
    return false;
  snprintf(s_url, sizeof(s_url), "%s", url);
  build();
  return true;
}

bool ui_qr_panel_ready(void) {
  return s_ready;
}

void ui_qr_panel_draw(int x, int y, bool show) {
  const UiRect box = {x, y, UI_QR_BOX, UI_QR_BOX};
  if (show && s_ready) {
    ui_shape9_draw(UI_SHAPE9_SM, box, UI_QR_PLATE);
    vita2d_draw_texture_tint(s_art, (float)(x + UI_QR_QUIET), (float)(y + UI_QR_QUIET), UI_QR_INK);
    return;
  }
  if (s_hidden_w < 0)
    s_hidden_w = ui_text_face_width(UI_FACE_T16, HIDDEN_TEXT);
  ui_shape9_draw(UI_SHAPE9_SM, box, UI_QR_HIDDEN);
  ui_text_draw_face_centered_v(UI_FACE_T16, x + (UI_QR_BOX - s_hidden_w) / 2, y, UI_QR_BOX,
                               UI_TEXT_3, HIDDEN_TEXT);
}
