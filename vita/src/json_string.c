// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

/**
 * @file json_string.c
 * @brief Pure JSON string-value helpers (key lookup, \uXXXX and UTF-8 decoding)
 *
 * Moved out of psn_auth.c so they can be checked natively; no dependency beyond the C library.
 */

#include "json_string.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* Returns a pointer to the value following `"key":` in json (whitespace skipped), or NULL when the
 * key is absent. Not a general JSON parser: it matches the first occurrence of the quoted key. */
const char *json_find_key_value(const char *json, const char *key) {
  if (!json || !key)
    return NULL;
  char key_expr[96];
  if (snprintf(key_expr, sizeof(key_expr), "\"%s\"", key) <= 0)
    return NULL;

  const char *p = strstr(json, key_expr);
  if (!p)
    return NULL;
  p += strlen(key_expr);
  while (*p && isspace((unsigned char)*p))
    p++;
  if (*p != ':')
    return NULL;
  p++;
  while (*p && isspace((unsigned char)*p))
    p++;
  return p;
}

/* ---------------------------------------------------------------------------
 * Internal UTF-8 / \uXXXX helpers shared by json_get_string_len and
 * json_get_string.  All three are file-scope only.
 * --------------------------------------------------------------------------- */

/* Parses exactly 4 hexadecimal characters starting at *pp into *out_val.
 * Advances *pp by 4 on success.  Returns false (without advancing) if any
 * of the four characters is not a valid hex digit. */
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

/* Returns the number of UTF-8 bytes needed to encode Unicode codepoint cp.
 * cp must be a valid Unicode scalar value (U+0000..U+10FFFF, excluding
 * surrogates); the caller is responsible for that precondition.  Violations
 * are caught by assert() in debug builds (stripped under NDEBUG). */
static int json_utf8_len(unsigned long cp) {
  assert(cp <= 0x10FFFFu);
  assert(cp < 0xD800u || cp > 0xDFFFu);
  if (cp <= 0x7FUL)
    return 1;
  if (cp <= 0x7FFUL)
    return 2;
  if (cp <= 0xFFFFUL)
    return 3;
  return 4;
}

/* Encodes Unicode codepoint cp into buf (which must have at least 4 bytes).
 * Returns the number of bytes written.  Same precondition as json_utf8_len. */
static int json_utf8_encode(unsigned long cp, unsigned char *buf) {
  assert(cp <= 0x10FFFFu);
  assert(cp < 0xD800u || cp > 0xDFFFu);
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

/* Consumes a \uXXXX escape (the leading backslash and 'u' have already been
 * consumed by the caller; *pp points to the first hex digit).  Handles UTF-16
 * surrogate pairs by peeking ahead for a following \uXXXX low-surrogate.
 *
 * On success, advances *pp past all consumed characters and stores the decoded
 * Unicode codepoint in *out_cp.  Returns false on malformed input (non-hex
 * digits, lone high surrogate, unexpected low surrogate without a preceding
 * high surrogate). */
static bool json_decode_unicode_escape(const char **pp, unsigned long *out_cp) {
  unsigned int hi;
  if (!json_parse_hex4(pp, &hi))
    return false;

  /* High surrogate: must be followed immediately by \uDC00-\uDFFF. */
  if (hi >= 0xD800u && hi <= 0xDBFFu) {
    if ((*pp)[0] != '\\' || (*pp)[1] != 'u')
      return false; /* lone high surrogate — reject */
    *pp += 2;       /* consume the '\' and 'u' of the low surrogate escape */
    unsigned int lo;
    if (!json_parse_hex4(pp, &lo))
      return false;
    if (lo < 0xDC00u || lo > 0xDFFFu)
      return false; /* second escape is not a valid low surrogate */
    /* RFC 2781 surrogate-pair reassembly → supplementary codepoint */
    *out_cp = 0x10000UL + (((unsigned long)(hi - 0xD800u)) << 10) + (unsigned long)(lo - 0xDC00u);
    return true;
  }

  /* Low surrogate without a preceding high surrogate: reject. */
  if (hi >= 0xDC00u && hi <= 0xDFFFu)
    return false;

  *out_cp = (unsigned long)hi;
  return true;
}

/* ---------------------------------------------------------------------------
 * JSON string helpers
 * --------------------------------------------------------------------------- */

/* Returns the byte length (excluding null terminator) of the decoded JSON
 * string value for `key`.  Mirrors the escape-handling logic of
 * json_get_string exactly so that json_get_string_len(...) + 1 is always the
 * correct allocation size for json_get_string.
 *
 * \uXXXX escapes are decoded to UTF-8; surrogate pairs are combined into a
 * single supplementary codepoint before encoding.  Sets *out_len and returns
 * true on success; returns false if the key is not found, the value is not a
 * quoted string, the closing quote is missing, or a \uXXXX escape is
 * malformed (non-hex digits, lone surrogate, etc.). */
bool json_get_string_len(const char *json, const char *key, size_t *out_len) {
  if (!out_len)
    return false;
  *out_len = 0;
  const char *p = json_find_key_value(json, key);
  if (!p || *p != '"')
    return false;

  p++;
  size_t len = 0;
  while (*p && *p != '"') {
    if (*p == '\\') {
      p++;
      if (!*p)
        return false; /* truncated escape — parse failure */
      if (*p == 'u') {
        /* \uXXXX — may be a surrogate pair spanning two \uXXXX sequences. */
        p++;
        unsigned long cp;
        if (!json_decode_unicode_escape(&p, &cp))
          return false;
        len += (size_t)json_utf8_len(cp);
      } else {
        /* All other single-character escapes decode to exactly one byte:
         * \" \\ \/ \b \f \n \r \t and any other char (liberal). */
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

/* Copies the decoded JSON string value for `key` into out (size out_size, NUL-terminated).
 * Decodes the same escapes as json_get_string_len, so out_size = json_get_string_len() + 1 always
 * fits. Returns false if the key is missing, the value is not a quoted string, an escape is
 * malformed, or the value does not fit in out_size (out is then not a complete value). */
bool json_get_string(const char *json, const char *key, char *out, size_t out_size) {
  if (!out || out_size == 0)
    return false;
  out[0] = '\0';
  const char *p = json_find_key_value(json, key);
  if (!p || *p != '"')
    return false;

  p++;
  size_t o = 0;
  while (*p && *p != '"') {
    if (*p == '\\') {
      p++;
      if (!*p)
        return false; /* truncated escape — parse failure */
      if (*p == 'u') {
        /* \uXXXX — decode to UTF-8, possibly via a surrogate pair. */
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
      /* Single-character escapes. */
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
