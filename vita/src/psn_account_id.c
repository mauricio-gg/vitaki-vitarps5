/**
 * @file psn_account_id.c
 * @brief Pure rules for the PSN account ID (issue #270)
 */

#include "psn_account_id.h"

#include <chiaki/base64.h>

#include <ctype.h>
#include <string.h>

bool psn_account_id_is_valid(const uint8_t *id, size_t len) {
  if (!id || len != PSN_ACCOUNT_ID_BYTES)
    return false;
  for (size_t i = 0; i < len; i++) {
    if (id[i] != 0)
      return true;
  }
  return false;
}

bool psn_account_id_b64_is_valid(const char *id_b64) {
  if (!id_b64 || !id_b64[0])
    return false;
  uint8_t id[PSN_ACCOUNT_ID_BYTES];
  size_t id_size = sizeof(id);
  if (chiaki_base64_decode(id_b64, strlen(id_b64), id, &id_size) != CHIAKI_ERR_SUCCESS)
    return false;
  return psn_account_id_is_valid(id, id_size);
}

PsnAccountIdChoice psn_account_id_startup_choice(const char *registry_b64, const char *stored_b64) {
  const bool has_stored = psn_account_id_b64_is_valid(stored_b64);
  if (registry_b64 && registry_b64[0]) {
    return (has_stored && strcmp(registry_b64, stored_b64) == 0) ? PSN_ACCOUNT_ID_USE_REGISTRY_SAME
                                                                 : PSN_ACCOUNT_ID_USE_REGISTRY_NEW;
  }
  return has_stored ? PSN_ACCOUNT_ID_USE_STORED : PSN_ACCOUNT_ID_USE_NONE;
}

const char *psn_account_id_fingerprint(const char *id_b64) {
  if (!id_b64)
    return "";
  size_t len = strlen(id_b64);
  return len < PSN_ACCOUNT_ID_FINGERPRINT_CHARS ? ""
                                                : id_b64 + len - PSN_ACCOUNT_ID_FINGERPRINT_CHARS;
}

const char *psn_account_id_in_use(PsnAccountIdSource chosen, const char *vita_b64,
                                  const char *manual_b64, PsnAccountIdSource *used) {
  if (chosen == PSN_ACCOUNT_ID_SOURCE_MANUAL && psn_account_id_b64_is_valid(manual_b64)) {
    if (used)
      *used = PSN_ACCOUNT_ID_SOURCE_MANUAL;
    return manual_b64;
  }
  if (used)
    *used = PSN_ACCOUNT_ID_SOURCE_VITA;
  return vita_b64;
}

/** True for the whitespace psn_account_id_parse() trims. */
static bool is_trim_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/** True when the @len characters at @s are all ASCII digits (and @len > 0). */
static bool is_all_digits(const char *s, size_t len) {
  if (len == 0)
    return false;
  for (size_t i = 0; i < len; i++) {
    if (!isdigit((unsigned char)s[i]))
      return false;
  }
  return true;
}

/** Parse @len decimal digits into 8 little-endian bytes; false when the value exceeds 2^64 - 1. */
static bool decimal_to_le_bytes(const char *s, size_t len, uint8_t out[PSN_ACCOUNT_ID_BYTES]) {
  uint64_t value = 0;
  for (size_t i = 0; i < len; i++) {
    uint64_t digit = (uint64_t)(s[i] - '0');
    if (value > (UINT64_MAX - digit) / 10)
      return false;
    value = value * 10 + digit;
  }
  for (size_t i = 0; i < PSN_ACCOUNT_ID_BYTES; i++)
    out[i] = (uint8_t)(value >> (8 * i));
  return true;
}

PsnAccountIdParse psn_account_id_parse(const char *text, char *out_b64, size_t out_size) {
  if (!text || !out_b64 || out_size < PSN_ACCOUNT_ID_B64_BUF_SIZE)
    return PSN_ACCOUNT_ID_PARSE_BAD_FORMAT;
  while (is_trim_space(*text))
    text++;
  size_t len = strlen(text);
  while (len > 0 && is_trim_space(text[len - 1]))
    len--;

  uint8_t id[PSN_ACCOUNT_ID_BYTES];
  if (is_all_digits(text, len)) {
    if (!decimal_to_le_bytes(text, len, id))
      return PSN_ACCOUNT_ID_PARSE_BAD_FORMAT;
  } else {
    size_t id_size = sizeof(id);
    if (len != PSN_ACCOUNT_ID_B64_CHARS || text[len - 1] != '=' ||
        chiaki_base64_decode(text, len, id, &id_size) != CHIAKI_ERR_SUCCESS ||
        id_size != PSN_ACCOUNT_ID_BYTES)
      return PSN_ACCOUNT_ID_PARSE_BAD_FORMAT;
  }
  if (!psn_account_id_is_valid(id, sizeof(id)))
    return PSN_ACCOUNT_ID_PARSE_ALL_ZERO;
  if (chiaki_base64_encode(id, sizeof(id), out_b64, out_size) != CHIAKI_ERR_SUCCESS)
    return PSN_ACCOUNT_ID_PARSE_BAD_FORMAT;
  return PSN_ACCOUNT_ID_PARSE_OK;
}
