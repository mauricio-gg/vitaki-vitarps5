/*
 * token_crypto.h — AES-256-GCM encryption for persisted PSN OAuth tokens.
 *
 * Key derivation: SHA-256(salt || OpenPsID || kind) → 32-byte AES-256 key.
 * The key is derived on demand and never stored.
 *
 * Wire format (base64-encoded):
 *   version(1) || nonce(12) || ciphertext(N) || tag(16)
 *
 * The "kind" string ("access" / "refresh") is bound as GCM AAD alongside a
 * version byte, preventing cross-field blob swapping.
 *
 * GitHub issue #81.
 */
#pragma once

#include <stdint.h>

/*
 * token_crypto_encrypt — Encrypt a plaintext PSN token for disk storage.
 *
 * @param plaintext  NUL-terminated token string (e.g. the access token).
 * @param kind       Token kind label used as AAD ("access" or "refresh").
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
