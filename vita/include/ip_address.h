// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// Number of numbers in a dotted IPv4 address.
#define IP_ADDRESS_OCTET_COUNT 4

/// Size of a buffer that holds any formatted address: "255.255.255.255" plus the terminator.
#define IP_ADDRESS_STRING_SIZE 16

/**
 * Checks typed text as a dotted IPv4 address and splits it into its four numbers.
 *
 * Accepts exactly four decimal numbers 0-255 separated by single dots, with optional spaces,
 * tabs or line breaks around the whole text (trimmed). Each number is 1 to 3 digits. Leading
 * zeros are accepted and always read as decimal, never octal: "010.1.1.1" is 10.1.1.1.
 * Rejects everything else: too few or too many parts, empty parts, signs, letters, a number
 * above 255, whitespace inside the text, an empty string.
 *
 * @param text    Typed text (NUL-terminated). NULL is rejected.
 * @param octets  Receives the four numbers on success. Untouched on failure. May be NULL when
 *                only the check is wanted.
 * @return true when text is a valid address.
 */
bool ip_address_parse(const char *text, uint8_t octets[IP_ADDRESS_OCTET_COUNT]);

/**
 * Tells whether an address can belong to a console: not 0.x.x.x (this includes 0.0.0.0), not
 * loopback 127.x.x.x, and not multicast, reserved or broadcast (224.0.0.0 and above, so
 * 255.255.255.255 too). Pure range check; ip_address_parse() stays a syntax check.
 *
 * @param octets  The four numbers, from ip_address_parse().
 * @return true when a console could have this address. false when octets is NULL.
 */
bool ip_address_is_console_target(const uint8_t octets[IP_ADDRESS_OCTET_COUNT]);

/**
 * Writes the canonical form of an address ("192.168.1.7", no leading zeros) to out.
 *
 * @param octets    The four numbers.
 * @param out       Destination buffer.
 * @param out_size  Size of out; IP_ADDRESS_STRING_SIZE is always enough.
 * @return true on success, false when out is NULL or too small (out is then left empty).
 */
bool ip_address_format(const uint8_t octets[IP_ADDRESS_OCTET_COUNT], char *out, size_t out_size);
