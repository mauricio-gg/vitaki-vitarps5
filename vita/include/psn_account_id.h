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
 * Concerns the Vita's own ID only (config.toml psn_account_id); the manual ID (#201) lives in
 * its own field and is never passed here. A valid registry ID always wins. Without one, a valid
 * stored ID is kept untouched so a Vita that is signed out does not lose it to zeros; an invalid
 * stored ID (for example zeros saved by older versions) is ignored, not cleared.
 */
PsnAccountIdChoice psn_account_id_startup_choice(const char *registry_b64, const char *stored_b64);

/** psn_account_id_fingerprint() - The last few base64 characters, safe to log ("" if too short). */
const char *psn_account_id_fingerprint(const char *id_b64);

/** Which account ID the user chose to use (config.toml psn_account_id_source). */
typedef enum psn_account_id_source_t {
  PSN_ACCOUNT_ID_SOURCE_VITA = 0,  ///< the ID read from the Vita's system profile (default)
  PSN_ACCOUNT_ID_SOURCE_MANUAL,    ///< the ID the user typed in
} PsnAccountIdSource;

/** config.toml spellings of PsnAccountIdSource; anything else reads as Vita. */
#define PSN_ACCOUNT_ID_SOURCE_VITA_KEY "vita"
#define PSN_ACCOUNT_ID_SOURCE_MANUAL_KEY "manual"

/**
 * psn_account_id_in_use() - Which account ID pairing and internet connect must use.
 * @chosen:     What the user chose.
 * @vita_b64:   The Vita's own ID (config.toml psn_account_id); may be NULL.
 * @manual_b64: The typed ID (config.toml psn_account_id_manual); may be NULL.
 * @used:       Out, may be NULL: the source actually returned (differs from @chosen when the
 *              manual choice falls back).
 *
 * Vita chosen: @vita_b64 exactly as stored (callers validate it, as before #201). Manual chosen
 * and @manual_b64 valid: @manual_b64. Manual chosen without a valid manual ID (a hand-edited
 * file): @vita_b64. Returns NULL when that is NULL. The string is borrowed, not copied.
 */
const char *psn_account_id_in_use(PsnAccountIdSource chosen, const char *vita_b64,
                                  const char *manual_b64, PsnAccountIdSource *used);

/** Result of psn_account_id_parse(); the UI shows a different message for each failure. */
typedef enum psn_account_id_parse_t {
  PSN_ACCOUNT_ID_PARSE_OK,
  PSN_ACCOUNT_ID_PARSE_BAD_FORMAT,  ///< neither 12-character base64 ending in '=' nor a number
  PSN_ACCOUNT_ID_PARSE_ALL_ZERO,    ///< well formed but decodes to eight zero bytes
} PsnAccountIdParse;

/** Characters in the base64 form of an account ID (8 bytes, one '=' pad). Not get_base64_size():
 * util.c pulls in the SDK and this file must build natively. */
#define PSN_ACCOUNT_ID_B64_CHARS 12

/** Buffer size psn_account_id_parse() needs for its output: the base64 characters plus NUL. */
#define PSN_ACCOUNT_ID_B64_BUF_SIZE (PSN_ACCOUNT_ID_B64_CHARS + 1)

/**
 * psn_account_id_parse() - Turn the text a user typed into the base64 ID to store.
 * @text:    The typed text; may be NULL.
 * @out_b64: Out: the base64 form, NUL terminated. Only written on PSN_ACCOUNT_ID_PARSE_OK.
 * @out_size: Size of @out_b64; must be at least PSN_ACCOUNT_ID_B64_BUF_SIZE.
 *
 * Leading and trailing whitespace (space, tab, CR, LF) is ignored. What remains must be either
 *  - base64 of exactly 8 bytes: 12 characters ending in '=', or
 *  - a decimal number: digits only, no sign, no inner spaces, at most 2^64 - 1.
 * A decimal number is the PSN numeric account ID; its 8 bytes are little-endian, the same as
 * Chiaki-NG and psn-account-id.py. The 8 bytes must not be all zero.
 */
PsnAccountIdParse psn_account_id_parse(const char *text, char *out_b64, size_t out_size);
