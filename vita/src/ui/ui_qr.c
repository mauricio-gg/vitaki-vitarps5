#include <string.h>

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
