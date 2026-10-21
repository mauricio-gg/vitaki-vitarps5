/**
 * @file json_string.h
 * @brief Pure JSON string-value helpers used by the PSN auth code
 *
 * No SDK or lib dependency, so they can be checked natively.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

/** Pointer to the value after `"key":` in json (whitespace skipped), or NULL if absent. */
const char *json_find_key_value(const char *json, const char *key);

/**
 * Byte length (excluding the NUL) of the decoded string value for key.
 * json_get_string_len() + 1 is always a big enough buffer for json_get_string().
 *
 * @return false if the key is missing, the value is not a quoted string, or an escape is malformed
 */
bool json_get_string_len(const char *json, const char *key, size_t *out_len);

/**
 * Copies the decoded string value for key into out (NUL-terminated, size out_size).
 *
 * @return false if the key is missing, the value is not a quoted string, an escape is malformed,
 *         or the value does not fit
 */
bool json_get_string(const char *json, const char *key, char *out, size_t out_size);
