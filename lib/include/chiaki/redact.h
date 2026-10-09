// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_REDACT_H
#define CHIAKI_REDACT_H

#include "common.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Copy text to a buffer with every credential value shortened, so the result is safe to log.
 *
 * A credential keeps its first 4 characters; the rest is replaced by "***". Values of 4
 * characters or fewer are left alone. Everything that is not a credential is copied byte for
 * byte. Redacted:
 *  - header lines Authorization (the scheme word stays, e.g. "Bearer abcd***"), RP-Key and any
 *    header whose name ends in RegistKey (names are case-insensitive)
 *  - JSON string values of access_token, refresh_token, id_token, code and client_secret
 *  - form or query parameters access_token=, refresh_token=, id_token=, code= and client_secret=
 *    (the name must start at a parameter boundary, so error_code= and response_type=code stay)
 *
 * Uses no heap, so it is safe inside curl callbacks. If the buffer is too small the output is
 * cut; a cut never shows more of a credential than the first 4 characters.
 *
 * @param in Input text, not necessarily NUL-terminated
 * @param in_size Number of bytes in in
 * @param out Buffer that receives the NUL-terminated redacted copy
 * @param out_size Size of out in bytes
 * @return Length of the output, not counting the NUL; 0 if out_size is 0
 */
CHIAKI_EXPORT size_t chiaki_redact_secrets(const char *in, size_t in_size, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_REDACT_H
