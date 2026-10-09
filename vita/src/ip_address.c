// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "ip_address.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/// Longest a single number may be written ("255"; leading zeros count toward this).
#define IP_ADDRESS_MAX_DIGITS 3
#define IP_ADDRESS_MAX_VALUE 255

static bool is_trim_char(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool ip_address_parse(const char *text, uint8_t octets[IP_ADDRESS_OCTET_COUNT]) {
  if (!text)
    return false;

  while (is_trim_char(*text))
    text++;
  const char *end = text + strlen(text);
  while (end > text && is_trim_char(end[-1]))
    end--;

  uint8_t parsed[IP_ADDRESS_OCTET_COUNT];
  const char *cursor = text;
  for (int part = 0; part < IP_ADDRESS_OCTET_COUNT; part++) {
    int digits = 0;
    int value = 0;
    while (cursor < end && isdigit((unsigned char)*cursor)) {
      value = value * 10 + (*cursor - '0');
      digits++;
      cursor++;
      if (digits > IP_ADDRESS_MAX_DIGITS)
        return false;
    }
    if (digits == 0 || value > IP_ADDRESS_MAX_VALUE)
      return false;
    parsed[part] = (uint8_t)value;

    bool last = (part == IP_ADDRESS_OCTET_COUNT - 1);
    if (last)
      break;
    if (cursor >= end || *cursor != '.')
      return false;
    cursor++;
  }
  if (cursor != end)
    return false;

  if (octets)
    memcpy(octets, parsed, sizeof(parsed));
  return true;
}

bool ip_address_format(const uint8_t octets[IP_ADDRESS_OCTET_COUNT], char *out, size_t out_size) {
  if (!out || out_size == 0)
    return false;
  out[0] = '\0';
  if (!octets)
    return false;
  int written = snprintf(out, out_size, "%u.%u.%u.%u", (unsigned)octets[0], (unsigned)octets[1],
                         (unsigned)octets[2], (unsigned)octets[3]);
  if (written < 0 || (size_t)written >= out_size) {
    out[0] = '\0';
    return false;
  }
  return true;
}
