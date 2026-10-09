// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the typed-IP check in vita/src/ip_address.c (issue #330, Enter IP
// address). `./tools/build.sh test` only cross-compiles, so run these on the host:
//   cc -std=c99 -Wall -Wextra -I vita/include test/ip_address_tests.c \
//      vita/src/ip_address.c -o /tmp/ip_address_tests && /tmp/ip_address_tests

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ip_address.h"

static bool parses_to(const char *text, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  uint8_t octets[IP_ADDRESS_OCTET_COUNT] = {0};
  if (!ip_address_parse(text, octets))
    return false;
  return octets[0] == a && octets[1] == b && octets[2] == c && octets[3] == d;
}

/** A good address, with the range edges and stray spaces around it, must be accepted and read
 * to the right numbers; refusing it blocks pairing, misreading it probes the wrong console. */
static void test_accepts_valid_addresses(void) {
  assert(parses_to("192.168.1.7", 192, 168, 1, 7));
  assert(parses_to("0.0.0.0", 0, 0, 0, 0));
  assert(parses_to("255.255.255.255", 255, 255, 255, 255));
  assert(parses_to("  10.0.0.1 \t\r\n", 10, 0, 0, 1));
}

/** Leading zeros are decimal. Read as octal "010" would be 8 and the probe would go to the
 * wrong host. */
static void test_leading_zeros_are_decimal(void) {
  assert(parses_to("010.001.000.099", 10, 1, 0, 99));
  assert(parses_to("192.168.001.007", 192, 168, 1, 7));
  assert(!ip_address_parse("0001.1.1.1", NULL));
}

/** Text that is not exactly four numbers 0-255 must be refused, or a bad address reaches the
 * network and the user sees "not found" instead of "not an IP address". */
static void test_rejects_bad_text(void) {
  const char *bad[] = {
      "192.168.1.7.7", "256.1.1.1", "1.256.1.1", "1.2.3",     "1.2.3.",    ".1.2.3",
      "1..2.3",        "",          "   ",       "abc",       "1.2.3.x",   "a.b.c.d",
      "+1.2.3.4",      "-1.2.3.4",  "1.2.3.-4",  "1 .2.3.4",  "1.2 .3.4",  "1.2.3.4 5",
      "1,2,3,4",       "1.2.3.4/24", "1.2.3.4:987", "999.999.999.999", "0x1.2.3.4",
  };
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    if (ip_address_parse(bad[i], NULL)) {
      fprintf(stderr, "wrongly accepted: \"%s\"\n", bad[i]);
      assert(0);
    }
  }
  assert(!ip_address_parse(NULL, NULL));
}

static bool console_target(const char *text) {
  uint8_t octets[IP_ADDRESS_OCTET_COUNT];
  return ip_address_parse(text, octets) && ip_address_is_console_target(octets);
}

/** Addresses no console has (this network, loopback, multicast, reserved, broadcast) must be
 * refused, or the probe waits 5 s for nothing or sends a broadcast; their legal neighbours must
 * pass, or a real console on that range could not be paired. */
static void test_console_target_range(void) {
  const char *refused[] = {"0.0.0.0", "0.1.2.3", "127.0.0.1", "127.255.255.255", "224.0.0.1",
                           "239.255.255.255", "240.0.0.1", "255.255.255.255"};
  for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) {
    if (console_target(refused[i])) {
      fprintf(stderr, "wrongly allowed: \"%s\"\n", refused[i]);
      assert(0);
    }
  }
  const char *allowed[] = {"1.0.0.0", "126.255.255.255", "128.0.0.1", "192.168.1.7",
                           "223.255.255.255"};
  for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
    if (!console_target(allowed[i])) {
      fprintf(stderr, "wrongly refused: \"%s\"\n", allowed[i]);
      assert(0);
    }
  }
  assert(!ip_address_is_console_target(NULL));
}

/** A refused address must leave the caller's numbers alone. */
static void test_failure_leaves_output_untouched(void) {
  uint8_t octets[IP_ADDRESS_OCTET_COUNT] = {9, 9, 9, 9};
  assert(!ip_address_parse("1.2.3", octets));
  assert(octets[0] == 9 && octets[1] == 9 && octets[2] == 9 && octets[3] == 9);
}

/** The text sent and shown is the canonical form, without leading zeros. */
static void test_format_is_canonical(void) {
  uint8_t octets[IP_ADDRESS_OCTET_COUNT];
  char out[IP_ADDRESS_STRING_SIZE];
  assert(ip_address_parse(" 192.168.001.007 ", octets));
  assert(ip_address_format(octets, out, sizeof(out)));
  assert(strcmp(out, "192.168.1.7") == 0);

  assert(ip_address_parse("255.255.255.255", octets));
  assert(ip_address_format(octets, out, sizeof(out)));
  assert(strcmp(out, "255.255.255.255") == 0);

  char tiny[8];
  assert(!ip_address_format(octets, tiny, sizeof(tiny)));
  assert(tiny[0] == '\0');
}

int main(void) {
  test_accepts_valid_addresses();
  test_leading_zeros_are_decimal();
  test_rejects_bad_text();
  test_console_target_range();
  test_failure_leaves_output_untouched();
  test_format_is_canonical();
  printf("ip_address_tests: all passed\n");
  return 0;
}
