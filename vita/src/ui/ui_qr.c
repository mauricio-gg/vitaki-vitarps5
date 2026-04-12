#include <string.h>

#include <vita2d.h>

#include "ui/ui_qr.h"

bool ui_qr_encode_text(const char *text, UIQrCode *out_qr) {
  if (!text || !text[0] || !out_qr)
    return false;

  uint8_t temp[qrcodegen_BUFFER_LEN_MAX];
  memset(out_qr, 0, sizeof(*out_qr));

  if (!qrcodegen_encodeText(text, temp, out_qr->qrcode, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                            qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
    return false;
  }

  out_qr->size = qrcodegen_getSize(out_qr->qrcode);
  out_qr->valid = out_qr->size > 0;
  return out_qr->valid;
}

void ui_qr_draw(const UIQrCode *qr, int x, int y, int module_px, int quiet_zone, uint32_t fg_color,
                uint32_t bg_color) {
  if (!qr || !qr->valid || qr->size <= 0 || module_px <= 0)
    return;

  int span = (qr->size + quiet_zone * 2) * module_px;
  vita2d_draw_rectangle(x, y, span, span, bg_color);

  for (int row = 0; row < qr->size; row++) {
    for (int col = 0; col < qr->size; col++) {
      if (!qrcodegen_getModule(qr->qrcode, col, row))
        continue;
      int dx = x + (quiet_zone + col) * module_px;
      int dy = y + (quiet_zone + row) * module_px;
      vita2d_draw_rectangle(dx, dy, module_px, module_px, fg_color);
    }
  }
}
