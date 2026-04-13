/*
 * token_crypto_tests.c — Unit tests for PSN OAuth token encryption.
 *
 * Tests run on the host build.  The hardware OpenPsID is replaced by a fixed
 * 16-byte test ID injected via token_crypto_set_test_device_id() (compiled in
 * only when VITARPS5_TEST_BUILD is defined).
 *
 * Test coverage:
 *  1.  Round-trip (access): encrypt then decrypt returns the original plaintext.
 *  2.  Round-trip (refresh): same for the "refresh" kind.
 *  3.  Tampered ciphertext byte → decrypt returns NULL.
 *  4.  Tampered tag byte → decrypt returns NULL.
 *  5.  Tampered nonce byte → decrypt returns NULL (different key stream → tag fail).
 *  6.  Wrong kind ("access" encrypted, "refresh" decrypted) → NULL (AAD mismatch).
 *  7.  Malformed base64 input → NULL.
 *  8.  Truncated blob (too short) → NULL.
 *  9.  Empty / NULL plaintext input → NULL (no blob produced).
 *  10. NULL kind → NULL.
 *  11. Nonce uniqueness: two encryptions of the same plaintext produce different
 *      blobs, confirming RAND_bytes generates a fresh nonce each time.
 *  12. Tampered version byte: flipping byte[0] to an unknown version → NULL,
 *      confirming the explicit version check in decrypt.
 *  13. Empty string blob: passing "" (empty C string, not NULL) to decrypt →
 *      NULL, confirming the minimum-blob-length guard rejects zero decoded bytes.
 *  14. Max-plaintext boundary: pt_len == 8192 (TOKEN_CRYPTO_MAX_PLAINTEXT, the
 *      internal limit defined in token_crypto.c) must encrypt and round-trip
 *      successfully; pt_len == 8193 must be rejected by encrypt (guard is
 *      `pt_len > TOKEN_CRYPTO_MAX_PLAINTEXT`, so == 8192 is the exact edge).
 *
 * GitHub issue #81.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "token_crypto.h"
#include "chiaki/base64.h"

/* ------------------------------------------------------------------ */
/* Test device ID injected for all tests.                               */
/* ------------------------------------------------------------------ */
static const uint8_t TEST_DEVICE_ID[16] = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10
};

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

/*
 * b64_decode_blob — Decode a base64 blob string into a heap-allocated byte
 * buffer.  Sets *out_len.  Returns NULL on failure.
 */
static uint8_t *b64_decode_blob(const char *b64, size_t *out_len) {
    size_t in_len = strlen(b64);
    size_t max_out = (in_len / 4) * 3 + 3;
    uint8_t *buf = malloc(max_out);
    if (!buf)
        return NULL;
    *out_len = max_out;
    ChiakiErrorCode rc = chiaki_base64_decode(b64, in_len, buf, out_len);
    if (rc != CHIAKI_ERR_SUCCESS) {
        free(buf);
        return NULL;
    }
    return buf;
}

/*
 * b64_encode_blob — Encode a byte buffer as a heap-allocated base64 string.
 */
static char *b64_encode_blob(const uint8_t *data, size_t len) {
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

/* ------------------------------------------------------------------ */
/* Tests                                                                */
/* ------------------------------------------------------------------ */

/*
 * test_roundtrip — Encrypt then decrypt should return the exact original string.
 */
static void test_roundtrip(void) {
    const char *plaintext = "eyJhbGciOiJSUzI1NiJ9.test_access_token_payload";
    char *blob = token_crypto_encrypt(plaintext, "access");
    assert(blob != NULL);

    char *recovered = token_crypto_decrypt(blob, "access");
    assert(recovered != NULL);
    assert(strcmp(recovered, plaintext) == 0);

    free(blob);
    free(recovered);
    puts("  [PASS] test_roundtrip");
}

/*
 * test_roundtrip_refresh — Same as above but for the "refresh" kind.
 */
static void test_roundtrip_refresh(void) {
    const char *plaintext = "v3.test_refresh_token_0987654321abcdef";
    char *blob = token_crypto_encrypt(plaintext, "refresh");
    assert(blob != NULL);

    char *recovered = token_crypto_decrypt(blob, "refresh");
    assert(recovered != NULL);
    assert(strcmp(recovered, plaintext) == 0);

    free(blob);
    free(recovered);
    puts("  [PASS] test_roundtrip_refresh");
}

/*
 * test_tampered_ciphertext — Flipping a byte in the ciphertext region must
 * cause GCM tag verification to fail, returning NULL.
 *
 * Blob layout: version(1) || nonce(12) || ciphertext(N) || tag(16).
 * The ciphertext starts at offset 13.
 */
static void test_tampered_ciphertext(void) {
    const char *plaintext = "sample_access_token_string";
    char *blob = token_crypto_encrypt(plaintext, "access");
    assert(blob != NULL);

    size_t raw_len = 0;
    uint8_t *raw = b64_decode_blob(blob, &raw_len);
    assert(raw != NULL);
    free(blob);

    /* Flip a byte in the ciphertext (offset 13 = 1 version + 12 nonce). */
    assert(raw_len > 13 + 16); /* at least version + nonce + 1 ct byte + tag */
    raw[13] ^= 0xff;

    char *tampered_b64 = b64_encode_blob(raw, raw_len);
    assert(tampered_b64 != NULL);
    free(raw);

    char *result = token_crypto_decrypt(tampered_b64, "access");
    assert(result == NULL);
    free(tampered_b64);
    puts("  [PASS] test_tampered_ciphertext");
}

/*
 * test_tampered_tag — Flipping a byte in the GCM tag must cause verification
 * to fail.
 *
 * The tag occupies the last 16 bytes of the blob.
 */
static void test_tampered_tag(void) {
    const char *plaintext = "another_access_token";
    char *blob = token_crypto_encrypt(plaintext, "access");
    assert(blob != NULL);

    size_t raw_len = 0;
    uint8_t *raw = b64_decode_blob(blob, &raw_len);
    assert(raw != NULL);
    free(blob);

    /* Tag is the last 16 bytes. */
    assert(raw_len >= 16);
    raw[raw_len - 1] ^= 0x01;

    char *tampered_b64 = b64_encode_blob(raw, raw_len);
    assert(tampered_b64 != NULL);
    free(raw);

    char *result = token_crypto_decrypt(tampered_b64, "access");
    assert(result == NULL);
    free(tampered_b64);
    puts("  [PASS] test_tampered_tag");
}

/*
 * test_tampered_nonce — Flipping a byte in the nonce changes the key stream;
 * the decrypted bytes are garbage and the tag will not match.
 *
 * The nonce occupies bytes 1–12.
 */
static void test_tampered_nonce(void) {
    const char *plaintext = "nonce_test_access_token";
    char *blob = token_crypto_encrypt(plaintext, "access");
    assert(blob != NULL);

    size_t raw_len = 0;
    uint8_t *raw = b64_decode_blob(blob, &raw_len);
    assert(raw != NULL);
    free(blob);

    /* Flip the first byte of the nonce (offset 1). */
    raw[1] ^= 0x80;

    char *tampered_b64 = b64_encode_blob(raw, raw_len);
    assert(tampered_b64 != NULL);
    free(raw);

    char *result = token_crypto_decrypt(tampered_b64, "access");
    assert(result == NULL);
    free(tampered_b64);
    puts("  [PASS] test_tampered_nonce");
}

/*
 * test_wrong_kind — Encrypting as "access" then decrypting as "refresh" must
 * fail because the AAD (which includes the kind string) differs.
 */
static void test_wrong_kind(void) {
    const char *plaintext = "cross_kind_test_token";
    char *blob = token_crypto_encrypt(plaintext, "access");
    assert(blob != NULL);

    char *result = token_crypto_decrypt(blob, "refresh");
    assert(result == NULL);

    free(blob);
    puts("  [PASS] test_wrong_kind");
}

/*
 * test_malformed_base64 — Feeding non-base64 garbage must return NULL without
 * crashing.
 */
static void test_malformed_base64(void) {
    char *result = token_crypto_decrypt("!!!not-valid-base64!!!", "access");
    assert(result == NULL);
    puts("  [PASS] test_malformed_base64");
}

/*
 * test_truncated_blob — A blob that is too short to hold the minimum structure
 * (version + nonce + 1 byte ciphertext + tag = 30 bytes) must return NULL.
 */
static void test_truncated_blob(void) {
    /* 10 bytes of raw data → base64-encoded → definitely too short. */
    const uint8_t short_data[10] = {0x01, 0x02, 0x03, 0x04, 0x05,
                                    0x06, 0x07, 0x08, 0x09, 0x0a};
    char *short_b64 = b64_encode_blob(short_data, sizeof(short_data));
    assert(short_b64 != NULL);

    char *result = token_crypto_decrypt(short_b64, "access");
    assert(result == NULL);

    free(short_b64);
    puts("  [PASS] test_truncated_blob");
}

/*
 * test_empty_plaintext — token_crypto_encrypt must reject an empty string.
 */
static void test_empty_plaintext(void) {
    char *result = token_crypto_encrypt("", "access");
    assert(result == NULL);
    puts("  [PASS] test_empty_plaintext");
}

/*
 * test_null_inputs — NULL plaintext and NULL kind must be rejected gracefully.
 */
static void test_null_inputs(void) {
    assert(token_crypto_encrypt(NULL, "access") == NULL);
    assert(token_crypto_encrypt("tok", NULL)   == NULL);
    assert(token_crypto_decrypt(NULL, "access") == NULL);
    assert(token_crypto_decrypt("blob", NULL)   == NULL);
    puts("  [PASS] test_null_inputs");
}

/*
 * test_nonce_uniqueness — Encrypt the same plaintext+kind twice and verify that
 * both calls succeed but produce distinct base64 blobs.  Because AES-256-GCM
 * uses a randomly generated 12-byte nonce, identical inputs must produce
 * different ciphertext+tag bytes with overwhelming probability.  This confirms
 * that RAND_bytes is exercised on every call.
 */
static void test_nonce_uniqueness(void) {
    const char *plaintext = "nonce_uniqueness_test_token";
    char *blob1 = token_crypto_encrypt(plaintext, "access");
    char *blob2 = token_crypto_encrypt(plaintext, "access");

    assert(blob1 != NULL);
    assert(blob2 != NULL);
    /* Two independent encryptions must yield distinct blobs. */
    assert(strcmp(blob1, blob2) != 0);

    free(blob1);
    free(blob2);
    puts("  [PASS] test_nonce_uniqueness");
}

/*
 * test_empty_string_blob — Passing an empty C string ("") to token_crypto_decrypt
 * must return NULL.  An empty string base64-decodes to zero bytes, which is
 * shorter than the minimum blob length (version + nonce + 1 byte ciphertext +
 * tag = 30 bytes), so the minimum-length guard must fire before any OpenSSL
 * call is made.
 */
static void test_empty_string_blob(void) {
    char *result = token_crypto_decrypt("", "access");
    assert(result == NULL);
    puts("  [PASS] test_empty_string_blob");
}

/*
 * test_tampered_version_byte — Encrypt a token, base64-decode the blob, flip
 * byte[0] (the version field) to 0x02, base64-re-encode, then assert that
 * token_crypto_decrypt returns NULL.  This exercises the explicit
 * `version != TOKEN_CRYPTO_VERSION` guard at the top of the decrypt path.
 *
 * Blob layout: version(1) || nonce(12) || ciphertext(N) || tag(16).
 */
static void test_tampered_version_byte(void) {
    const char *plaintext = "version_check_test_token";
    char *blob = token_crypto_encrypt(plaintext, "access");
    assert(blob != NULL);

    size_t raw_len = 0;
    uint8_t *raw = b64_decode_blob(blob, &raw_len);
    assert(raw != NULL);
    free(blob);

    /* Overwrite the version byte (offset 0) with an unknown version value. */
    raw[0] = 0x02;

    char *tampered_b64 = b64_encode_blob(raw, raw_len);
    assert(tampered_b64 != NULL);
    free(raw);

    char *result = token_crypto_decrypt(tampered_b64, "access");
    assert(result == NULL);
    free(tampered_b64);
    puts("  [PASS] test_tampered_version_byte");
}

/*
 * test_max_plaintext_boundary — Verify the TOKEN_CRYPTO_MAX_PLAINTEXT boundary.
 *
 * TOKEN_CRYPTO_MAX_PLAINTEXT is 8192, defined internally in token_crypto.c.
 * The size guard is `pt_len > TOKEN_CRYPTO_MAX_PLAINTEXT`, which means:
 *   - pt_len == 8192  → accepted (encrypt + decrypt must succeed)
 *   - pt_len == 8193  → rejected (encrypt must return NULL)
 *
 * Buffer sizing:
 *   - At-limit buffer:  8192 'A' chars + NUL at index 8192 → strlen == 8192.
 *   - Over-limit buffer: 8193 'A' chars + NUL at index 8193 → strlen == 8193.
 */
static void test_max_plaintext_boundary(void) {
    /* --- at-limit: exactly 8192 characters (== TOKEN_CRYPTO_MAX_PLAINTEXT) --- */
    /* Allocate 8192 + 1 bytes to hold the 8192 'A' chars plus the terminating NUL. */
    char *at_limit = malloc(8192 + 1);
    assert(at_limit != NULL);
    memset(at_limit, 'A', 8192);
    at_limit[8192] = '\0'; /* strlen(at_limit) == 8192 */

    char *blob = token_crypto_encrypt(at_limit, "access");
    assert(blob != NULL); /* pt_len == 8192 must be accepted */

    char *recovered = token_crypto_decrypt(blob, "access");
    assert(recovered != NULL);
    assert(strcmp(recovered, at_limit) == 0); /* full round-trip */

    free(recovered);
    free(blob);
    free(at_limit);

    /* --- over-limit: 8193 characters (> TOKEN_CRYPTO_MAX_PLAINTEXT) --- */
    /* Allocate 8193 + 1 bytes to hold the 8193 'A' chars plus the terminating NUL. */
    char *over_limit = malloc(8193 + 1);
    assert(over_limit != NULL);
    memset(over_limit, 'A', 8193);
    over_limit[8193] = '\0'; /* strlen(over_limit) == 8193 */

    char *rejected = token_crypto_encrypt(over_limit, "access");
    assert(rejected == NULL); /* pt_len == 8193 must be rejected */

    free(over_limit);
    puts("  [PASS] test_max_plaintext_boundary");
}

/* ------------------------------------------------------------------ */
/* Entry point called by the main test runner                           */
/* ------------------------------------------------------------------ */

void run_token_crypto_tests(void) {
    puts("token_crypto_tests:");

    /* Install the synthetic device ID before any crypto operation. */
    token_crypto_set_test_device_id(TEST_DEVICE_ID);

    test_roundtrip();
    test_roundtrip_refresh();
    test_tampered_ciphertext();
    test_tampered_tag();
    test_tampered_nonce();
    test_wrong_kind();
    test_malformed_base64();
    test_truncated_blob();
    test_empty_string_blob();
    test_max_plaintext_boundary();
    test_empty_plaintext();
    test_null_inputs();
    test_nonce_uniqueness();
    test_tampered_version_byte();

    puts("  token_crypto_tests: all passed");
}
