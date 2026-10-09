// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/redact.h>

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

/** Number of leading characters of a credential that stay readable. */
#define REDACT_KEEP_CHARS 4
#define REDACT_MARKER "***"
/** Longest header name looked at; anything longer is not one of ours. */
#define REDACT_HEADER_NAME_MAX 64

static const char *const redact_keys[] = {
	"access_token", "refresh_token", "id_token", "code", "client_secret"
};
#define REDACT_KEY_COUNT (sizeof(redact_keys) / sizeof(redact_keys[0]))

/** Bounded output cursor; always leaves room for the closing NUL. */
typedef struct
{
	char *buf;
	size_t size;
	size_t len;
} RedactOut;

static void out_put(RedactOut *o, const char *s, size_t n)
{
	for(size_t i = 0; i < n && o->len + 1 < o->size; i++)
		o->buf[o->len++] = s[i];
}

/** Emit a credential: its first REDACT_KEEP_CHARS characters, then the marker if more followed. */
static void out_secret(RedactOut *o, const char *s, size_t n)
{
	if(n <= REDACT_KEEP_CHARS)
	{
		out_put(o, s, n);
		return;
	}
	out_put(o, s, REDACT_KEEP_CHARS);
	out_put(o, REDACT_MARKER, sizeof(REDACT_MARKER) - 1);
}

static bool name_equals_ci(const char *s, size_t n, const char *lit)
{
	size_t ln = strlen(lit);
	if(n != ln)
		return false;
	for(size_t i = 0; i < n; i++)
		if(tolower((unsigned char)s[i]) != lit[i])
			return false;
	return true;
}

static bool name_ends_with_ci(const char *s, size_t n, const char *lit)
{
	size_t ln = strlen(lit);
	return n >= ln && name_equals_ci(s + (n - ln), ln, lit);
}

/** Is name the name of a header whose value is a credential? */
static bool is_secret_header(const char *name, size_t n, bool *has_scheme)
{
	*has_scheme = name_equals_ci(name, n, "authorization");
	return *has_scheme || name_equals_ci(name, n, "rp-key") || name_ends_with_ci(name, n, "registkey");
}

/**
 * Match a secret header line starting at in[i] (which must be a line start).
 *
 * @param[out] prefix_len Bytes to copy as they are (name, colon, blanks, scheme word)
 * @param[out] secret_len Bytes of the credential following the prefix
 */
static bool match_header(const char *in, size_t len, size_t i, size_t *prefix_len, size_t *secret_len)
{
	size_t p = i;
	while(p < len && in[p] != ':' && in[p] != '\n' && in[p] != '\r' && p - i <= REDACT_HEADER_NAME_MAX)
		p++;
	if(p >= len || in[p] != ':')
		return false;
	bool has_scheme;
	if(!is_secret_header(in + i, p - i, &has_scheme))
		return false;
	p++;
	while(p < len && (in[p] == ' ' || in[p] == '\t'))
		p++;
	size_t end = p;
	while(end < len && in[end] != '\r' && in[end] != '\n')
		end++;
	if(has_scheme)
	{
		size_t s = p;
		while(s < end && in[s] != ' ' && in[s] != '\t')
			s++;
		while(s < end && (in[s] == ' ' || in[s] == '\t'))
			s++;
		if(s < end)
			p = s;
	}
	*prefix_len = p - i;
	*secret_len = end - p;
	return true;
}

/**
 * Match `"key" : "value"` at the opening quote in[i], for a secret key and a string value.
 *
 * @param[out] prefix_len Bytes up to and including the value's opening quote
 * @param[out] secret_len Bytes of the value, up to its closing quote or the end of input
 */
static bool match_json(const char *in, size_t len, size_t i, size_t *prefix_len, size_t *secret_len)
{
	size_t k = i + 1;
	while(k < len && in[k] != '"' && in[k] != '\n')
		k++;
	if(k >= len || in[k] != '"')
		return false;
	bool is_key = false;
	for(size_t j = 0; j < REDACT_KEY_COUNT; j++)
		if(strlen(redact_keys[j]) == k - i - 1 && !memcmp(in + i + 1, redact_keys[j], k - i - 1))
			is_key = true;
	if(!is_key)
		return false;
	size_t p = k + 1;
	while(p < len && (in[p] == ' ' || in[p] == '\t'))
		p++;
	if(p >= len || in[p] != ':')
		return false;
	p++;
	while(p < len && (in[p] == ' ' || in[p] == '\t' || in[p] == '\r' || in[p] == '\n'))
		p++;
	if(p >= len || in[p] != '"')
		return false;
	p++;
	size_t end = p;
	while(end < len && in[end] != '"')
		end += (in[end] == '\\' && end + 1 < len) ? 2 : 1;
	if(end > len)
		end = len;
	*prefix_len = p - i;
	*secret_len = end - p;
	return true;
}

static bool is_param_name_char(char c)
{
	return isalnum((unsigned char)c) || c == '_' || c == '-';
}

/**
 * Match `key=value` at in[i] where key starts at a parameter boundary.
 *
 * @param[out] prefix_len Bytes of `key=`
 * @param[out] secret_len Bytes of the value, up to &, blank, quote or the end of input
 */
static bool match_param(const char *in, size_t len, size_t i, size_t *prefix_len, size_t *secret_len)
{
	if(i > 0 && is_param_name_char(in[i - 1]))
		return false;
	for(size_t j = 0; j < REDACT_KEY_COUNT; j++)
	{
		size_t kl = strlen(redact_keys[j]);
		if(i + kl >= len || memcmp(in + i, redact_keys[j], kl) || in[i + kl] != '=')
			continue;
		size_t p = i + kl + 1;
		size_t end = p;
		while(end < len && in[end] != '&' && in[end] != '"' && in[end] != '\'' && !isspace((unsigned char)in[end]))
			end++;
		*prefix_len = p - i;
		*secret_len = end - p;
		return true;
	}
	return false;
}

CHIAKI_EXPORT size_t chiaki_redact_secrets(const char *in, size_t in_size, char *out, size_t out_size)
{
	if(!out || out_size == 0)
		return 0;
	RedactOut o = { out, out_size, 0 };
	size_t i = 0;
	while(in && i < in_size)
	{
		size_t prefix_len = 0;
		size_t secret_len = 0;
		bool line_start = i == 0 || in[i - 1] == '\n';
		bool matched = (line_start && match_header(in, in_size, i, &prefix_len, &secret_len))
			|| (in[i] == '"' && match_json(in, in_size, i, &prefix_len, &secret_len))
			|| match_param(in, in_size, i, &prefix_len, &secret_len);
		if(matched)
		{
			out_put(&o, in + i, prefix_len);
			out_secret(&o, in + i + prefix_len, secret_len);
			i += prefix_len + secret_len;
		}
		else
		{
			out_put(&o, in + i, 1);
			i++;
		}
	}
	out[o.len] = '\0';
	return o.len;
}
