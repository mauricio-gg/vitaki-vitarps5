#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "qrcodegen/qrcodegen.h"

typedef struct ui_qr_code_t {
    uint8_t qrcode[qrcodegen_BUFFER_LEN_MAX];
    int size;
    bool valid;
} UIQrCode;

bool ui_qr_encode_text(const char *text, UIQrCode *out_qr);
void ui_qr_draw(const UIQrCode *qr, int x, int y, int module_px, int quiet_zone,
                uint32_t fg_color, uint32_t bg_color);
