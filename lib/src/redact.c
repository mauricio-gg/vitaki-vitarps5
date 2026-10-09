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

/** JSON keys and form or query parameter names whose value is a credential (case-insensitive). */
static const char *const redact_keys[] = {
	"access_token", "refresh_token", "id_token", "code", "client_secret", "npsso",
	"accessToken", "refreshToken", "idToken", "clientSecret", "authCode", "skey"
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
		if(tolower((unsigned char)s[i]) != tolower((unsigned char)lit[i]))
			return false;
	return true;
}

static bool name_ends_with_ci(const char *s, size_t n, const char *lit)
{
	size_t ln = strlen(lit);
	return n >= ln && name_equals_ci(s + (n - ln), ln, lit);
}

/** How the value of a secret header is laid out. */
typedef enum
{
	HEADER_VALUE_PLAIN, // the whole value is the credential
	HEADER_VALUE_SCHEME, // "<scheme> <credential>", as in Authorization: Bearer ...
	HEADER_VALUE_COOKIE, // "<name>=<credential>; attributes", only the first pair is a credential
	HEADER_VALUE_COOKIE_LIST // "<name>=<credential>; <name>=<credential>", every pair is one
} HeaderValueKind;

/** Is name the name of a header whose value is a credential? */
static bool is_secret_header(const char *name, size_t n, HeaderValueKind *kind)
{
	*kind = HEADER_VALUE_PLAIN;
	if(name_equals_ci(name, n, "authorization"))
	{
		*kind = HEADER_VALUE_SCHEME;
		return true;
	}
	if(name_equals_ci(name, n, "cookie"))
	{
		*kind = HEADER_VALUE_COOKIE_LIST;
		return true;
	}
	if(name_equals_ci(name, n, "set-cookie"))
	{
		*kind = HEADER_VALUE_COOKIE;
		return true;
	}
	return name_equals_ci(name, n, "rp-key") || name_equals_ci(name, n, "rp-auth") ||
		name_equals_ci(name, n, "user-credential") || name_ends_with_ci(name, n, "registkey") ||
		name_ends_with_ci(name, n, "-token");
}

/**
 * Match a secret header line starting at in[i] (which must be a line start).
 *
 * @param[out] prefix_len Bytes to copy as they are (name, colon, blanks, scheme word)
 * @param[out] secret_len Bytes of the credential following the prefix
 * @param[out] cookie_list Set when the line is a Cookie request header, whose later pairs are
 *             credentials too (see match_cookie_pair())
 */
static bool match_header(const char *in, size_t len, size_t i, size_t *prefix_len, size_t *secret_len,
	bool *cookie_list)
{
	size_t p = i;
	while(p < len && in[p] != ':' && in[p] != '\n' && in[p] != '\r' && p - i <= REDACT_HEADER_NAME_MAX)
		p++;
	if(p >= len || in[p] != ':')
		return false;
	HeaderValueKind kind;
	if(!is_secret_header(in + i, p - i, &kind))
		return false;
	p++;
	while(p < len && (in[p] == ' ' || in[p] == '\t'))
		p++;
	size_t end = p;
	while(end < len && in[end] != '\r' && in[end] != '\n')
		end++;
	*cookie_list = kind == HEADER_VALUE_COOKIE_LIST;
	if(kind == HEADER_VALUE_COOKIE || kind == HEADER_VALUE_COOKIE_LIST)
	{
		size_t s = p;
		while(s < end && in[s] != '=' && in[s] != ';')
			s++;
		if(s < end && in[s] == '=')
			p = s + 1;
		const char *semicolon = memchr(in + p, ';', end - p);
		if(semicolon)
			end = (size_t)(semicolon - in);
	}
	else if(kind == HEADER_VALUE_SCHEME)
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
 * Match a later `name=value` pair of a Cookie request header at in[i], the start of the pair
 * (after "; "). The caller knows the line is a Cookie header.
 *
 * @param[out] prefix_len Bytes of `name=`
 * @param[out] secret_len Bytes of the value, up to ; or the end of the line
 */
static bool match_cookie_pair(const char *in, size_t len, size_t i, size_t *prefix_len, size_t *secret_len)
{
	size_t p = i;
	while(p < len && in[p] != '=' && in[p] != ';' && in[p] != '\r' && in[p] != '\n')
		p++;
	if(p >= len || in[p] != '=')
		return false;
	p++;
	size_t end = p;
	while(end < len && in[end] != ';' && in[end] != '\r' && in[end] != '\n')
		end++;
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
		if(name_equals_ci(in + i + 1, k - i - 1, redact_keys[j]))
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
		if(i + kl >= len || !name_equals_ci(in + i, kl, redact_keys[j]) || in[i + kl] != '=')
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
	bool cookie_line = false; // inside a Cookie request header: every pair is a credential
	while(in && i < in_size)
	{
		size_t prefix_len = 0;
		size_t secret_len = 0;
		bool line_start = i == 0 || in[i - 1] == '\n';
		bool cookie_pair = cookie_line && i > 0 && (in[i - 1] == ';' || (in[i - 1] == ' ' && i > 1 && in[i - 2] == ';'));
		bool matched = (cookie_pair && match_cookie_pair(in, in_size, i, &prefix_len, &secret_len))
			|| (line_start && match_header(in, in_size, i, &prefix_len, &secret_len, &cookie_line))
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
			if(in[i] == '\n')
				cookie_line = false;
			out_put(&o, in + i, 1);
			i++;
		}
	}
	out[o.len] = '\0';
	return o.len;
}
