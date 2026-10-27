/*
 * token_crypto.h — AES-256-GCM encryption for persisted secrets: PSN OAuth tokens and
 * console pairing keys.
 *
 * Key derivation: SHA-256(salt || OpenPsID || kind) → 32-byte AES-256 key.
 * The key is derived on demand and never stored.
 *
 * Wire format (base64-encoded):
 *   version(1) || nonce(12) || ciphertext(N) || tag(16)
 *
 * The "kind" string ("access" / "refresh" for PSN tokens, TOKEN_CRYPTO_KIND_RP_KEY /
 * TOKEN_CRYPTO_KIND_RP_REGIST_KEY for console pairing keys) feeds the key derivation and is
 * bound as GCM AAD alongside a version byte, preventing cross-field blob swapping.
 *
 * GitHub issues #81, #464.
 */
#pragma once

#include <stdint.h>

/* Kind labels for the console pairing keys stored in chiaki.toml (GH #464). */
#define TOKEN_CRYPTO_KIND_RP_KEY "rp_key"
#define TOKEN_CRYPTO_KIND_RP_REGIST_KEY "rp_regist_key"

/*
 * TokenLoadResult — three-way outcome of loading one encrypted value from the config.
 *
 * TOKEN_LOAD_ABSENT  : the _enc key is not present in the TOML table.
 *                      The caller may fall back to a legacy plaintext key.
 * TOKEN_LOAD_OK      : the _enc key was present and decryption succeeded.
 *                      The output field has been set; no fallback needed.
 * TOKEN_LOAD_FAILED  : the _enc key was present but decryption failed
 *                      (wrong device / tampering).  The output field has
 *                      been cleared.  The caller MUST NOT fall back to
 *                      the legacy plaintext key.
 */
typedef enum { TOKEN_LOAD_ABSENT = 0, TOKEN_LOAD_OK = 1, TOKEN_LOAD_FAILED = 2 } TokenLoadResult;

/*
 * token_crypto_encrypt — Encrypt a plaintext PSN token for disk storage.
 *
 * @param plaintext  NUL-terminated token string (e.g. the access token).
 * @param kind       Kind label used as AAD ("access", "refresh" or a TOKEN_CRYPTO_KIND_*).
 *
 * Returns a heap-allocated, NUL-terminated base64 string on success.
 * The caller is responsible for free()ing the returned pointer.
 * Returns NULL on failure (key derivation error, cipher error, OOM).
 *
 * Never logs token plaintext or derived key bytes.
 */
char *token_crypto_encrypt(const char *plaintext, const char *kind);

/*
 * token_crypto_decrypt — Decrypt a PSN token blob loaded from disk.
 *
 * @param b64_blob   NUL-terminated base64 string produced by token_crypto_encrypt().
 * @param kind       Token kind label ("access" or "refresh") — must match the
 *                   value used during encryption; mismatches cause authentication
 *                   failure and NULL is returned.
 *
 * Returns a heap-allocated, NUL-terminated plaintext string on success.
 * The caller is responsible for free()ing the returned pointer.
 * Returns NULL on failure (wrong device, tampered blob, malformed base64,
 * truncated data, or wrong kind).
 *
 * On failure, the caller should clear in-memory token fields and require
 * the user to re-authenticate.
 */
char *token_crypto_decrypt(const char *b64_blob, const char *kind);

#ifdef VITARPS5_TEST_BUILD
/*
 * token_crypto_set_test_device_id — Override the hardware OpenPsID with a
 * fixed 16-byte value for host-side unit tests.
 *
 * This function is compiled only when VITARPS5_TEST_BUILD is defined.
 * It MUST be called before any token_crypto_encrypt / token_crypto_decrypt
 * calls in the test runner.
 *
 * @param id  Pointer to exactly 16 bytes used as the synthetic device ID.
 */
void token_crypto_set_test_device_id(const uint8_t id[16]);
#endif
