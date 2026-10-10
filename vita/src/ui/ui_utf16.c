/**
 * @file ui_utf16.c
 * @brief UTF-8 and UTF-16 conversion for the system keyboard text (see ui_utf16.h)
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

void ui_utf8_to_utf16(const char *src, uint16_t *dst, size_t dst_len) {
  if (!dst || dst_len == 0)
    return;
  size_t out = 0;
  const unsigned char *p = (const unsigned char *)src;
  while (p && *p && out + 1 < dst_len) {
    if (*p < 0x80) {
      dst[out++] = *p++;
    } else if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
      dst[out++] = (uint16_t)(((p[0] & 0x1F) << 6) | (p[1] & 0x3F));
      p += 2;
    } else if ((*p & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
      dst[out++] = (uint16_t)(((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F));
      p += 3;
    } else {
      break;
    }
  }
  dst[out] = 0;
}
