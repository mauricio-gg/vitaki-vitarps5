/*
 * token_crypto.c — AES-256-GCM encryption for persisted PSN OAuth tokens.
 *
 * Wire format (binary, before base64 encoding):
 *   [0]       version byte  (TOKEN_CRYPTO_VERSION = 0x01)
 *   [1..12]   12-byte GCM nonce (random per encryption)
 *   [13..N+12] ciphertext (same length as plaintext, no padding in GCM)
 *   [N+13..N+28] 16-byte GCM authentication tag
 *
 * AAD passed to GCM for binding: version_byte || kind_string
 *
 * Key derivation: SHA-256(TOKEN_CRYPTO_SALT || OpenPsID[16] || kind_string)
 * The derived key is stack-local and zeroed before return.
 *
 * Dependencies: OpenSSL EVP API (provided by VitaSDK ssl+crypto or host OpenSSL).
 *
 * GitHub issue #81.
 */

#include "token_crypto.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <chiaki/base64.h>

#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* Vita hardware OpenPsID is only available on device. */
#ifndef VITARPS5_TEST_BUILD
#include <psp2/kernel/openpsid.h>
#endif

/* Version tag embedded in every blob — bump if the format changes. */
#define TOKEN_CRYPTO_VERSION ((uint8_t)0x01)

/* Nonce size for AES-256-GCM. */
#define TOKEN_CRYPTO_NONCE_LEN 12

/* GCM authentication tag size (maximum 16 bytes). */
#define TOKEN_CRYPTO_TAG_LEN 16

/* AES-256 key size. */
#define TOKEN_CRYPTO_KEY_LEN 32

/* Maximum supported plaintext length (tokens are well under this). */
#define TOKEN_CRYPTO_MAX_PLAINTEXT 8192

/*
 * App-specific salt mixed into the key derivation hash.  This makes the
 * derived key unique to this application even if the OpenPsID were ever
 * used by another app with a similar scheme.  Not a secret.
 */
static const char TOKEN_CRYPTO_SALT[] = "VitaRPS5-PSNToken-v1-salt-8fd29e1a";

/* --------------------------------------------------------------------- */
/* Device ID — hardware or test override                                  */
/* --------------------------------------------------------------------- */

#ifdef VITARPS5_TEST_BUILD
static uint8_t g_test_device_id[16];
static int g_test_device_id_set = 0;

void token_crypto_set_test_device_id(const uint8_t id[16]) {
  memcpy(g_test_device_id, id, 16);
  g_test_device_id_set = 1;
}

/*
 * get_device_id — Fill buf[16] with the synthetic test device ID.
 * Returns 1 on success, 0 if not yet configured.
 */
static int get_device_id(uint8_t buf[16]) {
  if (!g_test_device_id_set)
    return 0;
  memcpy(buf, g_test_device_id, 16);
  return 1;
}
#else
/*
 * get_device_id — Fill buf[16] with the hardware OpenPsID.
 * Returns 1 on success, 0 on failure (should never fail on real hardware).
 */
static int get_device_id(uint8_t buf[16]) {
  SceKernelOpenPsId psid;
  memset(&psid, 0, sizeof(psid));
  int rc = sceKernelGetOpenPsId(&psid);
  if (rc < 0)
    return 0;
  memcpy(buf, psid.id, 16);
  return 1;
}
#endif /* VITARPS5_TEST_BUILD */

/* --------------------------------------------------------------------- */
/* Key derivation                                                         */
/* --------------------------------------------------------------------- */

/*
 * derive_key — Derive a 32-byte AES-256 key from the device ID and token kind.
 *
 * Uses SHA-256(salt || device_id[16] || kind) as a simple, dependency-free
 * KDF.  The resulting key is unique per device and per token kind.
 *
 * Single-pass SHA-256 is sufficient here because device_id is a 128-bit
 * hardware identifier with high entropy (not a user passphrase), so no
 * PBKDF2-style stretching is needed.
 *
 * @param kind      NUL-terminated token kind string ("access" or "refresh").
 * @param key_out   Output buffer of exactly TOKEN_CRYPTO_KEY_LEN bytes.
 *
 * Returns 1 on success, 0 on failure (device ID unavailable or SHA error).
 */
static int derive_key(const char *kind, uint8_t key_out[TOKEN_CRYPTO_KEY_LEN]) {
  uint8_t device_id[16];
  if (!get_device_id(device_id)) {
    OPENSSL_cleanse(device_id, sizeof(device_id));
    return 0;
  }

  /*
   * Use EVP_MD_CTX_create / EVP_MD_CTX_destroy (OpenSSL 1.0.x naming) for
   * compatibility with the VitaSDK mbedtls-backed OpenSSL shim, which does
   * not expose the 1.1.x EVP_MD_CTX_new / EVP_MD_CTX_free aliases.
   */
  EVP_MD_CTX *md_ctx = EVP_MD_CTX_create();
  if (!md_ctx)
    return 0;

  int ok = 0;
  unsigned int digest_len = 0;

  if (EVP_DigestInit_ex(md_ctx, EVP_sha256(), NULL) != 1)
    goto cleanup;
  if (EVP_DigestUpdate(md_ctx, TOKEN_CRYPTO_SALT, strlen(TOKEN_CRYPTO_SALT)) != 1)
    goto cleanup;
  if (EVP_DigestUpdate(md_ctx, device_id, sizeof(device_id)) != 1)
    goto cleanup;
  if (EVP_DigestUpdate(md_ctx, kind, strlen(kind)) != 1)
    goto cleanup;
  if (EVP_DigestFinal_ex(md_ctx, key_out, &digest_len) != 1)
    goto cleanup;
  /* SHA-256 always outputs exactly 32 bytes — equal to KEY_LEN. */
  ok = (digest_len == TOKEN_CRYPTO_KEY_LEN);

cleanup:
  EVP_MD_CTX_destroy(md_ctx);
  /* Scrub device ID from stack. */
  OPENSSL_cleanse(device_id, sizeof(device_id));
  return ok;
}

/* --------------------------------------------------------------------- */
/* Base64 helpers                                                         */
/* --------------------------------------------------------------------- */

/*
 * b64_encode_alloc — Encode raw bytes to a NUL-terminated base64 string.
 * Returns a heap-allocated string on success, NULL on OOM or encode error.
 */
static char *b64_encode_alloc(const uint8_t *data, size_t len) {
  /* Base64 output size: ceil(len / 3) * 4 + NUL */
  size_t out_size = ((len + 2) / 3) * 4 + 1;
  char *out = malloc(out_size);
  if (!out)
    return NULL;
  ChiakiErrorCode rc = chiaki_base64_encode(data, len, out, out_size);
  if (rc != CHIAKI_ERR_SUCCESS) {
    free(out);
    return NULL;
  }
  return out;
}

/*
 * b64_decode_alloc — Decode a base64 string to raw bytes.
 * Returns a heap-allocated byte buffer on success, NULL on OOM or decode error.
 * Sets *out_len to the number of decoded bytes.
 */
static uint8_t *b64_decode_alloc(const char *b64, size_t *out_len) {
  size_t in_len = strlen(b64);
  /* Maximum decoded size: (in_len / 4) * 3 */
  size_t max_out = (in_len / 4) * 3 + 3;
  uint8_t *out = malloc(max_out);
  if (!out)
    return NULL;
  *out_len = max_out;
  ChiakiErrorCode rc = chiaki_base64_decode(b64, in_len, out, out_len);
  if (rc != CHIAKI_ERR_SUCCESS) {
    free(out);
    return NULL;
  }
  return out;
}

/* --------------------------------------------------------------------- */
/* Public API                                                             */
/* --------------------------------------------------------------------- */

/*
 * token_crypto_encrypt — Encrypt a plaintext token with AES-256-GCM.
 *
 * Produces a base64-encoded blob: version(1) || nonce(12) || ct(N) || tag(16).
 * The GCM AAD is: version_byte || kind_string.
 */
char *token_crypto_encrypt(const char *plaintext, const char *kind) {
  if (!plaintext || !kind)
    return NULL;

  size_t pt_len = strlen(plaintext);
  if (pt_len == 0 || pt_len > TOKEN_CRYPTO_MAX_PLAINTEXT)
    return NULL;

  /* Derive the encryption key. */
  uint8_t key[TOKEN_CRYPTO_KEY_LEN];
  if (!derive_key(kind, key))
    return NULL;

  /* Generate a random 12-byte nonce. */
  uint8_t nonce[TOKEN_CRYPTO_NONCE_LEN];
  if (RAND_bytes(nonce, TOKEN_CRYPTO_NONCE_LEN) != 1) {
    OPENSSL_cleanse(key, sizeof(key));
    return NULL;
  }

  /*
   * Binary blob: version(1) || nonce(12) || ciphertext(pt_len) || tag(16)
   * GCM produces ciphertext of exactly the same length as plaintext.
   */
  size_t blob_len = 1 + TOKEN_CRYPTO_NONCE_LEN + pt_len + TOKEN_CRYPTO_TAG_LEN;
  uint8_t *blob = malloc(blob_len);
  if (!blob) {
    OPENSSL_cleanse(key, sizeof(key));
    return NULL;
  }

  blob[0] = TOKEN_CRYPTO_VERSION;
  memcpy(blob + 1, nonce, TOKEN_CRYPTO_NONCE_LEN);

  uint8_t *ct_dst = blob + 1 + TOKEN_CRYPTO_NONCE_LEN;
  uint8_t *tag_dst = ct_dst + pt_len;

  /* Build the AAD: version_byte || kind_string (no NUL). */
  size_t kind_len = strlen(kind);
  size_t aad_len = 1 + kind_len;
  uint8_t *aad = malloc(aad_len);
  if (!aad) {
    free(blob);
    OPENSSL_cleanse(key, sizeof(key));
    return NULL;
  }
  aad[0] = TOKEN_CRYPTO_VERSION;
  memcpy(aad + 1, kind, kind_len);

  /* AES-256-GCM encryption via EVP. */
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  char *result = NULL;
  int out_len = 0;

  if (!ctx)
    goto done;

  if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
    goto done;
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, TOKEN_CRYPTO_NONCE_LEN, NULL) != 1)
    goto done;
  if (EVP_EncryptInit_ex(ctx, NULL, NULL, key, nonce) != 1)
    goto done;
  /* Feed AAD — no ciphertext output at this stage. */
  if (EVP_EncryptUpdate(ctx, NULL, &out_len, aad, (int)aad_len) != 1)
    goto done;
  /* Encrypt the plaintext. */
  if (EVP_EncryptUpdate(ctx, ct_dst, &out_len, (const uint8_t *)plaintext, (int)pt_len) != 1)
    goto done;
  /* GCM is a stream mode: ciphertext length always equals plaintext length.
   * If a future EVP backend ever violates this, fail closed rather than
   * write the tag at a wrong offset. */
  if (out_len != (int)pt_len)
    goto done;
  /* GCM produces zero output bytes on Final; final_len is expected to be 0. */
  int final_len = 0;
  if (EVP_EncryptFinal_ex(ctx, ct_dst + out_len, &final_len) != 1)
    goto done;
  if (final_len != 0)
    goto done;
  /* Extract the 16-byte GCM tag. */
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TOKEN_CRYPTO_TAG_LEN, (void *)tag_dst) != 1)
    goto done;

  result = b64_encode_alloc(blob, blob_len);

done:
  if (ctx)
    EVP_CIPHER_CTX_free(ctx);
  free(aad);
  free(blob);
  OPENSSL_cleanse(key, sizeof(key));
  return result;
}

/*
 * token_crypto_decrypt — Decrypt a GCM-protected token blob loaded from disk.
 *
 * Verifies the GCM authentication tag and the AAD (version + kind) before
 * returning plaintext.  Any mismatch (tampered data, wrong device, wrong kind,
 * truncated blob, malformed base64) results in NULL.
 */
char *token_crypto_decrypt(const char *b64_blob, const char *kind) {
  if (!b64_blob || !kind)
    return NULL;

  /* Decode the base64 envelope. */
  size_t raw_len = 0;
  uint8_t *raw = b64_decode_alloc(b64_blob, &raw_len);
  if (!raw)
    return NULL;

  /*
   * Minimum blob: version(1) + nonce(12) + ciphertext(1) + tag(16) = 30
   * A zero-length plaintext is rejected — tokens are never empty.
   */
  size_t header_len = 1 + TOKEN_CRYPTO_NONCE_LEN;
  size_t min_blob = header_len + 1 + TOKEN_CRYPTO_TAG_LEN;
  if (raw_len < min_blob) {
    free(raw);
    return NULL;
  }

  uint8_t version = raw[0];
  if (version != TOKEN_CRYPTO_VERSION) {
    free(raw);
    return NULL;
  }

  const uint8_t *nonce = raw + 1;
  size_t ct_len = raw_len - header_len - TOKEN_CRYPTO_TAG_LEN;
  const uint8_t *ct = raw + header_len;
  uint8_t tag[TOKEN_CRYPTO_TAG_LEN];
  memcpy(tag, raw + header_len + ct_len, TOKEN_CRYPTO_TAG_LEN);

  if (ct_len > TOKEN_CRYPTO_MAX_PLAINTEXT) {
    free(raw);
    return NULL;
  }

  /* Derive the decryption key. */
  uint8_t key[TOKEN_CRYPTO_KEY_LEN];
  if (!derive_key(kind, key)) {
    free(raw);
    return NULL;
  }

  /* Reconstruct AAD: version_byte || kind_string. */
  size_t kind_len = strlen(kind);
  size_t aad_len = 1 + kind_len;
  uint8_t *aad = malloc(aad_len);
  if (!aad) {
    free(raw);
    OPENSSL_cleanse(key, sizeof(key));
    return NULL;
  }
  aad[0] = version;
  memcpy(aad + 1, kind, kind_len);

  /* Allocate plaintext output buffer (same size as ciphertext in GCM). */
  char *plaintext = malloc(ct_len + 1);
  if (!plaintext) {
    free(aad);
    free(raw);
    OPENSSL_cleanse(key, sizeof(key));
    return NULL;
  }

  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  char *result = NULL;
  int out_len = 0;

  if (!ctx)
    goto done;

  if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
    goto done;
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, TOKEN_CRYPTO_NONCE_LEN, NULL) != 1)
    goto done;
  if (EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce) != 1)
    goto done;
  /* Feed AAD. */
  if (EVP_DecryptUpdate(ctx, NULL, &out_len, aad, (int)aad_len) != 1)
    goto done;
  /* Decrypt ciphertext. */
  if (EVP_DecryptUpdate(ctx, (uint8_t *)plaintext, &out_len, ct, (int)ct_len) != 1)
    goto done;

  /* Set the expected tag before calling Final. */
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TOKEN_CRYPTO_TAG_LEN, (void *)tag) != 1)
    goto done;

  /* Final verifies the tag — returns <= 0 if the tag does not match. */
  if (EVP_DecryptFinal_ex(ctx, (uint8_t *)plaintext + out_len, &out_len) <= 0) {
    /* Tag mismatch: do not expose partial plaintext.
     * OPENSSL_cleanse is used instead of memset because the compiler is
     * permitted to elide a memset of a buffer that is freed immediately
     * afterward; OPENSSL_cleanse resists that optimisation. */
    OPENSSL_cleanse(plaintext, ct_len + 1);
    goto done;
  }

  plaintext[ct_len] = '\0';
  result = plaintext;
  plaintext = NULL; /* Transferred to caller. */

done:
  if (ctx)
    EVP_CIPHER_CTX_free(ctx);
  free(plaintext);
  free(aad);
  free(raw);
  OPENSSL_cleanse(key, sizeof(key));
  OPENSSL_cleanse(tag, sizeof(tag));
  return result;
}
