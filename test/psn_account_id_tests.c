// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the typed-in PSN account ID rules in vita/src/psn_account_id.c
// (issue #201). `./tools/build.sh test` only cross-compiles, so run these on the host:
//   cc -std=c99 -Wall -Wextra -I vita/include -I lib/include test/psn_account_id_tests.c \
//      vita/src/psn_account_id.c lib/src/base64.c -o /tmp/psn_account_id_tests \
//      && /tmp/psn_account_id_tests

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "psn_account_id.h"

#define VITA_ID "EjRWeJq83vA="
#define MANUAL_ID "782rkHhWNBI="

static PsnAccountIdParse parse(const char *text, char *out) {
  return psn_account_id_parse(text, out, PSN_ACCOUNT_ID_B64_BUF_SIZE);
}

/** A decimal ID must give the bytes PSN means: little-endian, as Chiaki-NG and psn-account-id.py
 * build them. Big-endian would pair with a different account and the console would refuse. */
static void test_decimal_is_little_endian(void) {
  char out[PSN_ACCOUNT_ID_B64_BUF_SIZE];
  // 0x1234567890ABCDEF -> bytes EF CD AB 90 78 56 34 12
  assert(parse("1311768467294899695", out) == PSN_ACCOUNT_ID_PARSE_OK);
  assert(strcmp(out, MANUAL_ID) == 0);
  // The largest value that fits, 2^64 - 1.
  assert(parse("18446744073709551615", out) == PSN_ACCOUNT_ID_PARSE_OK);
  assert(strcmp(out, "//////////8=") == 0);
}

/** A valid typed ID must be accepted and stored as given (padding whitespace from the keyboard
 * trimmed); a bad one must never be stored. */
static void test_accepts_valid_base64_and_trims_whitespace(void) {
  char out[PSN_ACCOUNT_ID_B64_BUF_SIZE];
  assert(parse(VITA_ID, out) == PSN_ACCOUNT_ID_PARSE_OK);
  assert(strcmp(out, VITA_ID) == 0);
  assert(parse("  " VITA_ID "\n", out) == PSN_ACCOUNT_ID_PARSE_OK);
  assert(strcmp(out, VITA_ID) == 0);
}

/** The UI shows a different message for a malformed ID and for zeros; mixing them up, or letting
 * either through, stores an ID the console rejects. */
static void test_refusals_are_distinct(void) {
  char out[PSN_ACCOUNT_ID_B64_BUF_SIZE];
  assert(parse("AAAAAAAAAAA=", out) == PSN_ACCOUNT_ID_PARSE_ALL_ZERO);
  assert(parse("0", out) == PSN_ACCOUNT_ID_PARSE_ALL_ZERO);
  assert(parse("0000000000", out) == PSN_ACCOUNT_ID_PARSE_ALL_ZERO);

  assert(parse("", out) == PSN_ACCOUNT_ID_PARSE_BAD_FORMAT);
  assert(parse("   ", out) == PSN_ACCOUNT_ID_PARSE_BAD_FORMAT);
  assert(parse(NULL, out) == PSN_ACCOUNT_ID_PARSE_BAD_FORMAT);
  assert(parse("EjRWeA==", out) == PSN_ACCOUNT_ID_PARSE_BAD_FORMAT);         // 4 bytes
  assert(parse("EjRWeJq83vA", out) == PSN_ACCOUNT_ID_PARSE_BAD_FORMAT);      // no '='
  assert(parse("EjRWeJq83vAAAAA=", out) == PSN_ACCOUNT_ID_PARSE_BAD_FORMAT); // 11 bytes
  assert(parse("EjRWeJq8 3vA=", out) == PSN_ACCOUNT_ID_PARSE_BAD_FORMAT);    // inner space
  assert(parse("EjRWeJq83v!=", out) == PSN_ACCOUNT_ID_PARSE_BAD_FORMAT);
  assert(parse("-5", out) == PSN_ACCOUNT_ID_PARSE_BAD_FORMAT);
  assert(parse("+5", out) == PSN_ACCOUNT_ID_PARSE_BAD_FORMAT);
  assert(parse("12 34", out) == PSN_ACCOUNT_ID_PARSE_BAD_FORMAT);
  assert(parse("18446744073709551616", out) == PSN_ACCOUNT_ID_PARSE_BAD_FORMAT); // 2^64
  assert(parse("99999999999999999999999", out) == PSN_ACCOUNT_ID_PARSE_BAD_FORMAT);
}

/** Pairing must use the ID the user chose, and must never be left with a broken manual ID when a
 * Vita ID exists (hand-edited config). Vita choice must behave exactly as before #201. */
static void test_id_in_use_follows_choice_with_fallback(void) {
  PsnAccountIdSource used;
  assert(strcmp(psn_account_id_in_use(PSN_ACCOUNT_ID_SOURCE_VITA, VITA_ID, MANUAL_ID, &used),
                VITA_ID) == 0);
  assert(used == PSN_ACCOUNT_ID_SOURCE_VITA);

  assert(strcmp(psn_account_id_in_use(PSN_ACCOUNT_ID_SOURCE_MANUAL, VITA_ID, MANUAL_ID, &used),
                MANUAL_ID) == 0);
  assert(used == PSN_ACCOUNT_ID_SOURCE_MANUAL);

  const char *bad[] = {NULL, "", "AAAAAAAAAAA=", "not base64!"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    assert(strcmp(psn_account_id_in_use(PSN_ACCOUNT_ID_SOURCE_MANUAL, VITA_ID, bad[i], &used),
                  VITA_ID) == 0);
    assert(used == PSN_ACCOUNT_ID_SOURCE_VITA);
  }
  // Vita chosen with no Vita ID: nothing, so pairing is blocked as before; the manual ID is not
  // silently substituted.
  assert(psn_account_id_in_use(PSN_ACCOUNT_ID_SOURCE_VITA, NULL, MANUAL_ID, NULL) == NULL);
}

int main(void) {
  test_decimal_is_little_endian();
  test_accepts_valid_base64_and_trims_whitespace();
  test_refusals_are_distinct();
  test_id_in_use_follows_choice_with_fallback();
  puts("psn account id tests passed");
  return 0;
}
