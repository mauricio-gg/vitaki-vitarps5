/**
 * @file psn_account_id.c
 * @brief Pure rules for the PSN account ID (issue #270)
 */

#include "psn_account_id.h"

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

PsnAccountIdChoice psn_account_id_startup_choice(const char *registry_b64, const char *stored_b64) {
  const bool has_stored = stored_b64 && stored_b64[0];
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
