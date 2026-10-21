/* json_escape_tests.c — Tests for the JSON string helpers in vita/src/json_string.c (token and
 * error-string decoding for PSN auth: \uXXXX escapes, surrogate pairs, UTF-8 encoding).
 *
 * The tests call the real json_get_string_len / json_get_string, compiled into vitarps5_tests.
 *
 * Tests are registered in run_json_escape_tests() at the bottom of the file
 * and called from main() in config_vita_tests.c.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "json_string.h"

/* =========================================================================
 * Test helpers
 * ========================================================================= */

/* Verify that len == copy byte count and copy output == expected bytes. */
static void check_roundtrip(const char *json, const char *key,
                             const char *expected_bytes, size_t expected_len) {
  size_t len = 0;
  assert(json_get_string_len(json, key, &len));
  assert(len == expected_len);

  char *buf = (char *)malloc(len + 1);
  assert(buf);
  assert(json_get_string(json, key, buf, len + 1));
  assert(memcmp(buf, expected_bytes, len) == 0);
  assert(buf[len] == '\0');
  free(buf);
}

/* Verify that both helpers return false for the given JSON. */
static void check_failure(const char *json, const char *key) {
  size_t len = 0;
  bool len_ok = json_get_string_len(json, key, &len);
  assert(!len_ok);

  char buf[64];
  bool copy_ok = json_get_string(json, key, buf, sizeof(buf));
  assert(!copy_ok);
}

/* =========================================================================
 * Individual tests
 * ========================================================================= */

/* catches: an ordinary access token being altered or truncated by the decoder. */
static void test_plain_ascii(void) {
  const char *json = "{\"token\":\"abc123\"}";
  const char expected[] = "abc123";
  check_roundtrip(json, "token", expected, 6);
}

/* catches: wrong bytes for the single-character escapes (\", \\, \/, \n, \r, \t) in a token or
 * Sony error text. */
static void test_standard_single_char_escapes(void) {
  /* \" \\ \/ decode to 1 byte each */
  check_roundtrip("{\"v\":\"\\\"\\\\\\/ \"}", "v", "\"\\/ ", 4);

  /* \n \r \t */
  const char *json = "{\"v\":\"\\n\\r\\t\"}";
  const char expected[] = "\n\r\t";
  check_roundtrip(json, "v", expected, 3);
}

/* catches: wrong bytes for a 2-byte \uXXXX character (U+00E9 -> 0xC3 0xA9). */
static void test_u00e9_two_byte_utf8(void) {
  const char *json = "{\"desc\":\"caf\\u00E9\"}";
  const unsigned char expected[] = { 'c', 'a', 'f', 0xC3, 0xA9 };
  check_roundtrip(json, "desc", (const char *)expected, 5);
}

/* catches: wrong bytes for a 3-byte \uXXXX character (U+20AC -> 0xE2 0x82 0xAC). */
static void test_u20ac_three_byte_utf8(void) {
  const char *json = "{\"price\":\"\\u20AC10\"}";
  const unsigned char expected[] = { 0xE2, 0x82, 0xAC, '1', '0' };
  check_roundtrip(json, "price", (const char *)expected, 5);
}

/* catches: a surrogate pair (\uD83D\uDE00 = U+1F600) decoded to the wrong 4 bytes. */
static void test_surrogate_pair_emoji(void) {
  const char *json = "{\"e\":\"\\uD83D\\uDE00\"}";
  const unsigned char expected[] = { 0xF0, 0x9F, 0x98, 0x80 };
  check_roundtrip(json, "e", (const char *)expected, 4);
}

/* catches: a lone high surrogate being accepted as valid text. */
static void test_lone_high_surrogate_fails(void) {
  check_failure("{\"v\":\"\\uD83D\"}", "v");
}

/* catches: a high surrogate followed by a non-surrogate escape being accepted. */
static void test_high_surrogate_wrong_low_fails(void) {
  check_failure("{\"v\":\"\\uD83D\\u0041\"}", "v");
}

/* catches: a low surrogate with no high surrogate being accepted. */
static void test_lone_low_surrogate_fails(void) {
  check_failure("{\"v\":\"\\uDE00\"}", "v");
}

/* catches: a \u escape with non-hex digits being accepted. */
static void test_non_hex_unicode_escape_fails(void) {
  check_failure("{\"v\":\"\\uGHIJ\"}", "v");
}

/* catches: a \u escape cut short by the end of the string being accepted or read past its end. */
static void test_truncated_unicode_escape_fails(void) {
  /* Construct a string where the \u escape has only 2 hex digits before NUL.
   * We can't put NUL inside a C string literal, so build it in a char array. */
  char json[] = "{\"v\":\"\\u00\"}";
  /* Force the closing quote out of the way by placing NUL after the 2 hex digits. */
  /* json is: { " v " : " \ u 0 0 " }
   * indices: 0 1 2 3 4 5 6 7 8 9 10 11 12
   * We want NUL at index 10 (after '0','0') so the escape is incomplete. */
  json[10] = '\0';
  size_t len = 0;
  assert(!json_get_string_len(json, "v", &len));
  char buf[16];
  assert(!json_get_string(json, "v", buf, sizeof(buf)));
}

/* catches: json_get_string_len disagreeing with json_get_string, so a buffer of len + 1 (how
 * parse_token_grant allocates) overruns or rejects a valid value. */
static void test_len_plus_one_is_exact_buffer_size(void) {
  const char *json = "{\"v\":\"\\uD83D\\uDE00 caf\\u00E9\"}";
  /* Expected: 4-byte emoji + ' ' + 'caf' + 2-byte é = 10 bytes */
  size_t len = 0;
  assert(json_get_string_len(json, "v", &len));
  assert(len == 10);

  char buf[11]; /* len+1 */
  assert(json_get_string(json, "v", buf, sizeof(buf)));
  assert(buf[10] == '\0');

  const unsigned char expected[] = {
    0xF0, 0x9F, 0x98, 0x80, ' ', 'c', 'a', 'f', 0xC3, 0xA9
  };
  assert(memcmp(buf, expected, 10) == 0);
}

/* catches: \u0000 miscounted by the sizer or crashing the copier (buffer overrun).
 * U+0000 (null codepoint, \u0000) — edge case: encodes to 1 UTF-8 byte (0x00).
 * The sizer must count it; the copier can't write it into the string body
 * because it would terminate the C string early.  This is a known limitation
 * of null-terminated output and is acceptable — the important thing is that
 * the helpers don't crash or miscount. */
static void test_null_codepoint_edge_case(void) {
  /* \u0000 in the middle of a token: "a\u0000b" → 3 decoded bytes but the
   * output string looks like "a" because of C string semantics.  The sizer
   * must still return 3 and the copier must return true. */
  const char *json = "{\"v\":\"a\\u0000b\"}";
  size_t len = 0;
  assert(json_get_string_len(json, "v", &len));
  assert(len == 3);

  char buf[4];
  assert(json_get_string(json, "v", buf, sizeof(buf)));
  assert(buf[0] == 'a');
  assert(buf[1] == '\0'); /* NUL byte from \u0000 */
  assert(buf[2] == 'b');  /* still written past the NUL */
  assert(buf[3] == '\0'); /* terminator */
}

/* catches: a value cut short by a too-small buffer being reported as complete (silent truncation).
 * Regression: \uXXXX overflow at end-of-string must return false, not true.
 *
 * When the buffer is too small to hold the decoded UTF-8 bytes of a \uXXXX
 * escape that appears just before the closing '"', the post-loop check
 * '*p == '"'' would have incorrectly returned true under the old break path
 * because json_decode_unicode_escape had already advanced p past the escape.
 * The fix changes that path to return false. */
static void test_unicode_escape_overflow_returns_false(void) {
  /* \u00E9 = U+00E9 LATIN SMALL LETTER E WITH ACUTE → 2-byte UTF-8.
   * A buffer of size 1 (only room for the NUL terminator) cannot hold even a
   * single byte of output, so the overflow guard must fire and return false. */
  const char *json = "{\"v\":\"\\u00E9\"}";
  char buf[1];
  bool ok = json_get_string(json, "v", buf, sizeof(buf));
  assert(!ok);

  /* Also verify with a buffer of exactly 2 bytes: room for one byte + NUL,
   * but the codepoint encodes to 2 UTF-8 bytes, so it still must not fit. */
  char buf2[2];
  ok = json_get_string(json, "v", buf2, sizeof(buf2));
  assert(!ok);
}

/* =========================================================================
 * Entry point called from config_vita_tests.c main()
 * ========================================================================= */

void run_json_escape_tests(void) {
  test_plain_ascii();
  test_standard_single_char_escapes();
  test_u00e9_two_byte_utf8();
  test_u20ac_three_byte_utf8();
  test_surrogate_pair_emoji();
  test_lone_high_surrogate_fails();
  test_high_surrogate_wrong_low_fails();
  test_lone_low_surrogate_fails();
  test_non_hex_unicode_escape_fails();
  test_truncated_unicode_escape_fails();
  test_len_plus_one_is_exact_buffer_size();
  test_null_codepoint_edge_case();
  test_unicode_escape_overflow_returns_false();
}
