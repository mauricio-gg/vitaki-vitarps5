/**
 * @file psn_account_id.c
 * @brief Pure rules for the PSN account ID (issue #270)
 */

#include "psn_account_id.h"

#include <chiaki/base64.h>

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
