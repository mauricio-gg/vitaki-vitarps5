/* json_escape_tests.c — Unit tests for the JSON \uXXXX / UTF-8 helpers that
 * live in vita/src/psn_auth.c.
 *
 * Because those helpers are declared static inside a translation unit that
 * depends on libcurl and other Vita-specific headers not available on the host
 * build machine, the helper functions are duplicated here verbatim.  The
 * helpers have no external dependencies beyond the C standard library.
 *
 * If the helpers in psn_auth.c are ever changed, this file must be updated to
 * match so that the tests continue to reflect actual production behaviour.
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

/* =========================================================================
 * Helpers duplicated from vita/src/psn_auth.c (static linkage copy).
 * Keep in sync with the originals.
 * ========================================================================= */

/* Minimal json_find_key_value stub sufficient for testing json_get_string_len
 * and json_get_string: given a key, returns a pointer to the first '"' of
 * the string value in a flat {"key":"value"} JSON object.  Not a general JSON
 * parser — enough for the tests below. */
static const char *json_find_key_value_test(const char *json, const char *key) {
  if (!json || !key)
    return NULL;
  /* Find "key": pattern */
  size_t klen = strlen(key);
  const char *p = json;
  while (*p) {
    if (*p == '"') {
      const char *start = p + 1;
      const char *end = strchr(start, '"');
      if (!end)
        return NULL;
      if ((size_t)(end - start) == klen && memcmp(start, key, klen) == 0) {
        /* Found the key — skip whitespace and colon */
        p = end + 1;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
          p++;
        if (*p != ':')
          return NULL;
        p++;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
          p++;
        return p;
      }
    }
    p++;
  }
  return NULL;
}

/* --- helper implementations (verbatim copy from psn_auth.c) --- */

static bool json_parse_hex4(const char **pp, unsigned int *out_val) {
  const char *p = *pp;
  unsigned int v = 0;
  int i;
  for (i = 0; i < 4; i++) {
    unsigned char c = (unsigned char)p[i];
    unsigned int digit;
    if (c >= '0' && c <= '9')
      digit = c - '0';
    else if (c >= 'a' && c <= 'f')
      digit = 10 + (c - 'a');
    else if (c >= 'A' && c <= 'F')
      digit = 10 + (c - 'A');
    else
      return false;
    v = (v << 4) | digit;
  }
  *out_val = v;
  *pp = p + 4;
  return true;
}

static int json_utf8_len(unsigned long cp) {
  if (cp <= 0x7FUL)
    return 1;
  if (cp <= 0x7FFUL)
    return 2;
  if (cp <= 0xFFFFUL)
    return 3;
  return 4;
}

static int json_utf8_encode(unsigned long cp, unsigned char *buf) {
  if (cp <= 0x7FUL) {
    buf[0] = (unsigned char)cp;
    return 1;
  }
  if (cp <= 0x7FFUL) {
    buf[0] = (unsigned char)(0xC0u | (cp >> 6));
    buf[1] = (unsigned char)(0x80u | (cp & 0x3Fu));
    return 2;
  }
  if (cp <= 0xFFFFUL) {
    buf[0] = (unsigned char)(0xE0u | (cp >> 12));
    buf[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
    buf[2] = (unsigned char)(0x80u | (cp & 0x3Fu));
    return 3;
  }
  buf[0] = (unsigned char)(0xF0u | (cp >> 18));
  buf[1] = (unsigned char)(0x80u | ((cp >> 12) & 0x3Fu));
  buf[2] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
  buf[3] = (unsigned char)(0x80u | (cp & 0x3Fu));
  return 4;
}

static bool json_decode_unicode_escape(const char **pp, unsigned long *out_cp) {
  unsigned int hi;
  if (!json_parse_hex4(pp, &hi))
    return false;

  if (hi >= 0xD800u && hi <= 0xDBFFu) {
    if ((*pp)[0] != '\\' || (*pp)[1] != 'u')
      return false;
    *pp += 2;
    unsigned int lo;
    if (!json_parse_hex4(pp, &lo))
      return false;
    if (lo < 0xDC00u || lo > 0xDFFFu)
      return false;
    *out_cp = 0x10000UL + (((unsigned long)(hi - 0xD800u)) << 10)
              + (unsigned long)(lo - 0xDC00u);
    return true;
  }

  if (hi >= 0xDC00u && hi <= 0xDFFFu)
    return false;

  *out_cp = (unsigned long)hi;
  return true;
}

/* Wrappers that use json_find_key_value_test so we can test with simple JSON
 * strings directly without needing the full psn_auth.c link. */
static bool test_json_get_string_len(const char *json, const char *key, size_t *out_len) {
  if (!out_len)
    return false;
  *out_len = 0;
  const char *p = json_find_key_value_test(json, key);
  if (!p || *p != '"')
    return false;

  p++;
  size_t len = 0;
  while (*p && *p != '"') {
    if (*p == '\\') {
      p++;
      if (!*p)
        return false;
      if (*p == 'u') {
        p++;
        unsigned long cp;
        if (!json_decode_unicode_escape(&p, &cp))
          return false;
        len += (size_t)json_utf8_len(cp);
      } else {
        len++;
        p++;
      }
      continue;
    }
    len++;
    p++;
  }
  if (*p != '"')
    return false;
  *out_len = len;
  return true;
}

static bool test_json_get_string(const char *json, const char *key, char *out, size_t out_size) {
  if (!out || out_size == 0)
    return false;
  out[0] = '\0';
  const char *p = json_find_key_value_test(json, key);
  if (!p || *p != '"')
    return false;

  p++;
  size_t o = 0;
  while (*p && *p != '"') {
    if (*p == '\\') {
      p++;
      if (!*p)
        return false;
      if (*p == 'u') {
        p++;
        unsigned long cp;
        if (!json_decode_unicode_escape(&p, &cp))
          return false;
        unsigned char utf8[4];
        int nb = json_utf8_encode(cp, utf8);
        int i;
        /* Emit UTF-8 bytes; fail if they would not fit.  Unlike the
         * single-char and plain-ASCII overflow paths (which break with p
         * still pointing at the unwritten character so *p != '"'), here p
         * has already been advanced past the full \uXXXX sequence by
         * json_decode_unicode_escape.  A break would leave p after the
         * escape, causing *p == '"' to report success despite silent
         * truncation — so return false instead. */
        if (o + (size_t)nb + 1 > out_size)
          return false;
        for (i = 0; i < nb; i++)
          out[o++] = (char)utf8[i];
        continue;
      }
      if (o + 1 >= out_size)
        break;
      switch (*p) {
        case '"':
        case '\\':
        case '/':
          out[o++] = *p;
          break;
        case 'b':
          out[o++] = '\b';
          break;
        case 'f':
          out[o++] = '\f';
          break;
        case 'n':
          out[o++] = '\n';
          break;
        case 'r':
          out[o++] = '\r';
          break;
        case 't':
          out[o++] = '\t';
          break;
        default:
          out[o++] = *p;
          break;
      }
      p++;
      continue;
    }
    if (o + 1 >= out_size)
      break;
    out[o++] = *p++;
  }
  out[o] = '\0';
  return *p == '"';
}

/* =========================================================================
 * Test helpers
 * ========================================================================= */

/* Verify that len == copy byte count and copy output == expected bytes. */
static void check_roundtrip(const char *json, const char *key,
                             const char *expected_bytes, size_t expected_len) {
  size_t len = 0;
  assert(test_json_get_string_len(json, key, &len));
  assert(len == expected_len);

  char *buf = (char *)malloc(len + 1);
  assert(buf);
  assert(test_json_get_string(json, key, buf, len + 1));
  assert(memcmp(buf, expected_bytes, len) == 0);
  assert(buf[len] == '\0');
  free(buf);
}

/* Verify that both helpers return false for the given JSON. */
static void check_failure(const char *json, const char *key) {
  size_t len = 0;
  bool len_ok = test_json_get_string_len(json, key, &len);
  assert(!len_ok);

  char buf[64];
  bool copy_ok = test_json_get_string(json, key, buf, sizeof(buf));
  assert(!copy_ok);
}

/* =========================================================================
 * Individual tests
 * ========================================================================= */

/* Plain ASCII token — existing behaviour must be unchanged. */
static void test_plain_ascii(void) {
  const char *json = "{\"token\":\"abc123\"}";
  const char expected[] = "abc123";
  check_roundtrip(json, "token", expected, 6);
}

/* Standard single-char escapes: \", \\, \/, \n, \r, \t */
static void test_standard_single_char_escapes(void) {
  /* \" \\ \/ decode to 1 byte each */
  check_roundtrip("{\"v\":\"\\\"\\\\\\/ \"}", "v", "\"\\/ ", 4);

  /* \n \r \t */
  const char *json = "{\"v\":\"\\n\\r\\t\"}";
  const char expected[] = "\n\r\t";
  check_roundtrip(json, "v", expected, 3);
}

/* \u00E9 = U+00E9 LATIN SMALL LETTER E WITH ACUTE → 2-byte UTF-8 0xC3 0xA9 */
static void test_u00e9_two_byte_utf8(void) {
  const char *json = "{\"desc\":\"caf\\u00E9\"}";
  const unsigned char expected[] = { 'c', 'a', 'f', 0xC3, 0xA9 };
  check_roundtrip(json, "desc", (const char *)expected, 5);
}

/* \u20AC = U+20AC EURO SIGN → 3-byte UTF-8 0xE2 0x82 0xAC */
static void test_u20ac_three_byte_utf8(void) {
  const char *json = "{\"price\":\"\\u20AC10\"}";
  const unsigned char expected[] = { 0xE2, 0x82, 0xAC, '1', '0' };
  check_roundtrip(json, "price", (const char *)expected, 5);
}

/* Surrogate pair \uD83D\uDE00 = U+1F600 GRINNING FACE
 * → 4-byte UTF-8: 0xF0 0x9F 0x98 0x80 */
static void test_surrogate_pair_emoji(void) {
  const char *json = "{\"e\":\"\\uD83D\\uDE00\"}";
  const unsigned char expected[] = { 0xF0, 0x9F, 0x98, 0x80 };
  check_roundtrip(json, "e", (const char *)expected, 4);
}

/* Multiple \uXXXX escapes in one value, mixed with plain text. */
static void test_mixed_unicode_and_ascii(void) {
  /* "caf\u00E9 \u20AC" → "caf" + é(2B) + " " + €(3B) = 8 bytes */
  const char *json = "{\"v\":\"caf\\u00E9 \\u20AC\"}";
  const unsigned char expected[] = {
    'c', 'a', 'f', 0xC3, 0xA9, ' ', 0xE2, 0x82, 0xAC
  };
  check_roundtrip(json, "v", (const char *)expected, 9);
}

/* Lone high surrogate (no following \uDCxx) must return false. */
static void test_lone_high_surrogate_fails(void) {
  check_failure("{\"v\":\"\\uD83D\"}", "v");
}

/* High surrogate followed by a non-surrogate codepoint must return false. */
static void test_high_surrogate_wrong_low_fails(void) {
  check_failure("{\"v\":\"\\uD83D\\u0041\"}", "v");
}

/* Lone low surrogate must return false. */
static void test_lone_low_surrogate_fails(void) {
  check_failure("{\"v\":\"\\uDE00\"}", "v");
}

/* \u followed by non-hex characters must return false. */
static void test_non_hex_unicode_escape_fails(void) {
  check_failure("{\"v\":\"\\uGHIJ\"}", "v");
}

/* \u followed by fewer than 4 characters (NUL-terminated short) must return false. */
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
  assert(!test_json_get_string_len(json, "v", &len));
  char buf[16];
  assert(!test_json_get_string(json, "v", buf, sizeof(buf)));
}

/* len + 1 == exact buffer size invariant: json_get_string_len gives the byte
 * count such that a buffer of len+1 is always exactly sufficient. */
static void test_len_plus_one_is_exact_buffer_size(void) {
  const char *json = "{\"v\":\"\\uD83D\\uDE00 caf\\u00E9\"}";
  /* Expected: 4-byte emoji + ' ' + 'c' 'a' 'f' + 2-byte é = 9 bytes */
  size_t len = 0;
  assert(test_json_get_string_len(json, "v", &len));
  assert(len == 9);

  char buf[10]; /* len+1 */
  assert(test_json_get_string(json, "v", buf, sizeof(buf)));
  assert(buf[9] == '\0');

  const unsigned char expected[] = {
    0xF0, 0x9F, 0x98, 0x80, ' ', 'c', 'a', 'f', 0xC3, 0xA9
  };
  assert(memcmp(buf, expected, 9) == 0);
}

/* U+0000 (null codepoint, \u0000) — edge case: encodes to 1 UTF-8 byte (0x00).
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
  assert(test_json_get_string_len(json, "v", &len));
  assert(len == 3);

  char buf[4];
  assert(test_json_get_string(json, "v", buf, sizeof(buf)));
  assert(buf[0] == 'a');
  assert(buf[1] == '\0'); /* NUL byte from \u0000 */
  assert(buf[2] == 'b');  /* still written past the NUL */
  assert(buf[3] == '\0'); /* terminator */
}

/* Regression: \uXXXX overflow at end-of-string must return false, not true.
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
  bool ok = test_json_get_string(json, "v", buf, sizeof(buf));
  assert(!ok);

  /* Also verify with a buffer of exactly 2 bytes: room for one byte + NUL,
   * but the codepoint encodes to 2 UTF-8 bytes, so it still must not fit. */
  char buf2[2];
  ok = test_json_get_string(json, "v", buf2, sizeof(buf2));
  assert(!ok);
}

/* \b decodes to 0x08 (BACKSPACE), length 1. */
static void test_backspace_escape(void) {
  const char *json = "{\"v\":\"\\b\"}";
  const unsigned char expected[] = { 0x08 };
  check_roundtrip(json, "v", (const char *)expected, 1);
}

/* \f decodes to 0x0C (FORM FEED), length 1. */
static void test_form_feed_escape(void) {
  const char *json = "{\"v\":\"\\f\"}";
  const unsigned char expected[] = { 0x0C };
  check_roundtrip(json, "v", (const char *)expected, 1);
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
  test_mixed_unicode_and_ascii();
  test_lone_high_surrogate_fails();
  test_high_surrogate_wrong_low_fails();
  test_lone_low_surrogate_fails();
  test_non_hex_unicode_escape_fails();
  test_truncated_unicode_escape_fails();
  test_len_plus_one_is_exact_buffer_size();
  test_null_codepoint_edge_case();
  test_unicode_escape_overflow_returns_false();
  test_backspace_escape();
  test_form_feed_escape();
}
