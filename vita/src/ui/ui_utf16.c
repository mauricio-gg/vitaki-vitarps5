/**
 * @file ui_utf16.c
 * @brief UTF-16 to UTF-8 for the text the system keyboard returns (see ui_utf16.h)
 */

#include "ui/ui_utf16.h"

/** Bytes UTF-8 needs for the BMP unit @unit. */
static size_t utf8_length(uint16_t unit) {
  if (unit < 0x80)
    return 1;
  return unit < 0x800 ? 2 : 3;
}

/** Write @unit as UTF-8 at @out, which has room for utf8_length(@unit) bytes. */
static void utf8_write(uint16_t unit, char *out) {
  if (unit < 0x80) {
    out[0] = (char)unit;
  } else if (unit < 0x800) {
    out[0] = (char)(0xC0 | (unit >> 6));
    out[1] = (char)(0x80 | (unit & 0x3F));
  } else {
    out[0] = (char)(0xE0 | (unit >> 12));
    out[1] = (char)(0x80 | ((unit >> 6) & 0x3F));
    out[2] = (char)(0x80 | (unit & 0x3F));
  }
}

void ui_utf16_to_utf8(const uint16_t *src, size_t src_max, char *dst, size_t dst_size) {
  if (!dst || dst_size == 0)
    return;
  size_t out = 0;
  for (size_t i = 0; src && i < src_max && src[i]; i++) {
    const size_t need = utf8_length(src[i]);
    if (out + need >= dst_size)
      break;
    utf8_write(src[i], &dst[out]);
    out += need;
  }
  dst[out] = '\0';
}
