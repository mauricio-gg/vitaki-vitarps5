/**
 * @file ui_utf16.h
 * @brief UTF-16 to UTF-8 for the text the system keyboard returns
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * ui_utf16_to_utf8() - Convert the text of the system keyboard to UTF-8.
 * @src:      UTF-16 text ending at a 0 unit or after @src_max units; NULL counts as empty.
 * @src_max:  Most units to read.
 * @dst:      Output; always NUL-terminated when @dst_size is at least 1.
 * @dst_size: Size of @dst in bytes.
 *
 * Handles the Basic Multilingual Plane only, which covers what the keyboard returns. A character
 * whose bytes do not all fit is left out whole, so the output never ends in half a character, and
 * nothing is written past @dst_size.
 */
void ui_utf16_to_utf8(const uint16_t *src, size_t src_max, char *dst, size_t dst_size);
