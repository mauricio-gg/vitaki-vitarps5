/**
 * @file psn_account_id.h
 * @brief Rules for the PSN account ID the Vita sends when pairing (issue #270)
 *
 * No SDK or lib dependency, so the rules can be checked natively.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Size of a PSN account ID in bytes. host_registration.c asserts it equals lib's value. */
#define PSN_ACCOUNT_ID_BYTES 8

/** Vita registry location of the signed-in PSN account ID. */
#define PSN_ACCOUNT_ID_REGISTRY_PATH "/CONFIG/NP/"
#define PSN_ACCOUNT_ID_REGISTRY_KEY "account_id"

/** How many trailing base64 characters identify an ID in logs without revealing it. */
#define PSN_ACCOUNT_ID_FINGERPRINT_CHARS 4

/** Where the account ID used for this launch comes from. */
typedef enum psn_account_id_choice_t {
  PSN_ACCOUNT_ID_USE_REGISTRY_NEW,   ///< registry is valid and differs: replace the stored ID
  PSN_ACCOUNT_ID_USE_REGISTRY_SAME,  ///< registry is valid and equals the stored ID
  PSN_ACCOUNT_ID_USE_STORED,         ///< registry has no valid ID: keep what config.toml holds
  PSN_ACCOUNT_ID_USE_NONE,           ///< neither has an ID
} PsnAccountIdChoice;

/**
 * psn_account_id_is_valid() - Whether the bytes are a usable account ID.
 * @id:  The decoded ID bytes; may be NULL.
 * @len: Number of bytes in @id.
 *
 * Valid means exactly PSN_ACCOUNT_ID_BYTES long and not all zero. A Vita that is not signed in
 * to PSN has an all-zero ID, which the console rejects with CE-110032-7.
 */
bool psn_account_id_is_valid(const uint8_t *id, size_t len);

/**
 * psn_account_id_b64_is_valid() - Whether a base64 string decodes to a usable account ID.
 * @id_b64: The ID as stored in config.toml; may be NULL.
 *
 * Same rule as psn_account_id_is_valid(), applied after decoding. Undecodable, wrong-length
 * and all-zero values are not valid.
 */
bool psn_account_id_b64_is_valid(const char *id_b64);

/**
 * psn_account_id_startup_choice() - Decide which account ID a launch uses.
 * @registry_b64: Base64 of the registry ID, or NULL when it was all zero or unreadable.
 * @stored_b64:   The ID in config.toml; NULL, empty or not a valid ID counts as none.
 *
 * A valid registry ID always wins. Without one, a valid stored ID is kept untouched so a
 * hand-entered ID is never overwritten by zeros (#201); an invalid stored ID (for example zeros
 * saved by older versions) is ignored, not cleared.
 */
PsnAccountIdChoice psn_account_id_startup_choice(const char *registry_b64, const char *stored_b64);

/** psn_account_id_fingerprint() - The last few base64 characters, safe to log ("" if too short). */
const char *psn_account_id_fingerprint(const char *id_b64);
