// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native unit test for lib/src/redact.c (GH #361): credentials must never reach a log line
// with more than their first 4 characters. All token values below are made up.
//
// `./tools/build.sh test` only cross-compiles for arm-vita-eabi and never runs the ELF, so this
// native run is the real check.
//
// Build & run:
//   cc -std=c99 -I lib/include test/redact_tests.c lib/src/redact.c lib/src/log.c \
//      -o /tmp/redact_tests && /tmp/redact_tests

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <chiaki/log.h>
#include <chiaki/redact.h>

#define OUT_SIZE 2048

static void redact(const char *in, char *out, size_t out_size)
{
	// strlen is fine here: the helper must not need a NUL, but test input has one.
	chiaki_redact_secrets(in, strlen(in), out, out_size);
}

// A WebSocket request header block: only the token tail may change, every other byte stays.
static void test_bearer_header_block(void)
{
	const char *in =
		"GET /np/pushNotification HTTP/1.1\r\n"
		"Host: example.invalid\r\n"
		"authorization: Bearer abcd1234-fake-token-tail\r\n"
		"X-PSN-RETRY-INTERVAL-MIN: 5\r\n\r\n";
	const char *expected =
		"GET /np/pushNotification HTTP/1.1\r\n"
		"Host: example.invalid\r\n"
		"authorization: Bearer abcd***\r\n"
		"X-PSN-RETRY-INTERVAL-MIN: 5\r\n\r\n";
	char out[OUT_SIZE];
	redact(in, out, sizeof(out));
	assert(!strcmp(out, expected));
}

static void test_basic_auth_header(void)
{
	char out[OUT_SIZE];
	redact("Authorization: Basic ZmFrZWlkOmZha2VzZWNyZXQ=\r\n", out, sizeof(out));
	assert(!strcmp(out, "Authorization: Basic ZmFr***\r\n"));
}

// Token response body: both tokens cut, expires_in and a numeric code untouched.
static void test_token_json(void)
{
	const char *in = "{\"access_token\": \"abcd1234-fake-access\",\"token_type\":\"bearer\","
		"\"refresh_token\":\"wxyz5678-fake-refresh\",\"expires_in\":3599,\"code\":123}";
	const char *expected = "{\"access_token\": \"abcd***\",\"token_type\":\"bearer\","
		"\"refresh_token\":\"wxyz***\",\"expires_in\":3599,\"code\":123}";
	char out[OUT_SIZE];
	redact(in, out, sizeof(out));
	assert(!strcmp(out, expected));
}

// Form body: code and client_secret cut; response_type=code, error_code and a short value stay.
static void test_form_params(void)
{
	const char *in = "grant_type=authorization_code&code=abcd1234fake&client_secret=wxyz5678fake"
		"&response_type=code&error_code=fake-error-1&access_token=abc";
	const char *expected = "grant_type=authorization_code&code=abcd***&client_secret=wxyz***"
		"&response_type=code&error_code=fake-error-1&access_token=abc";
	char out[OUT_SIZE];
	redact(in, out, sizeof(out));
	assert(!strcmp(out, expected));
}

// Console pairing keys in the decrypted regist response.
static void test_regist_payload(void)
{
	const char *in = "HTTP/1.1 200 OK\r\nRP-Version: 10.0\r\nPS5-RegistKey: 0123456789abcdef\r\n"
		"RP-Key: fedcba9876543210\r\nRP-KeyType: 2\r\n\r\n";
	const char *expected = "HTTP/1.1 200 OK\r\nRP-Version: 10.0\r\nPS5-RegistKey: 0123***\r\n"
		"RP-Key: fedc***\r\nRP-KeyType: 2\r\n\r\n";
	char out[OUT_SIZE];
	redact(in, out, sizeof(out));
	assert(!strcmp(out, expected));
}

static void test_no_secrets_is_identical(void)
{
	const char *in = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
		"{\"status\":\"ok\",\"codes\":[1,2]} authorize?response_type=code&scope=psn\r\n";
	char out[OUT_SIZE];
	redact(in, out, sizeof(out));
	assert(!strcmp(out, in));
}

// Curl hands over chunks without a NUL; the helper must stop at in_size.
static void test_input_without_nul(void)
{
	const char chunk[] = { 'c','o','d','e','=','a','b','c','d','e','f','g','X','Y' };
	char out[OUT_SIZE];
	size_t n = chiaki_redact_secrets(chunk, sizeof(chunk) - 2, out, sizeof(out));
	assert(n == strlen("code=abcd***"));
	assert(!strcmp(out, "code=abcd***"));
}

// However small the output buffer, no run of the secret longer than 4 chars may appear.
static void test_small_buffer_never_leaks(void)
{
	const char *in = "Authorization: Bearer abcd1234-fake-token-tail\r\n";
	char out[OUT_SIZE];
	for(size_t size = 1; size < strlen(in) + 4; size++)
	{
		memset(out, 'Z', sizeof(out));
		size_t n = chiaki_redact_secrets(in, strlen(in), out, size);
		assert(n < size);
		assert(out[n] == '\0');
		assert(!strstr(out, "abcd1"));
		assert(!strstr(out, "token"));
	}
	assert(chiaki_redact_secrets(in, strlen(in), out, 0) == 0);
}

// Session-init and ctrl requests carry these; RP-Registkey is the spelling the session uses.
static void test_session_request_headers(void)
{
	const char *in = "GET /sce/rp/session HTTP/1.1\r\nHost: 192.0.2.1:9295\r\n"
		"RP-Registkey: 0011223344556677\r\nRP-Auth: fakeauth1234567\r\nRP-Version: 10.0\r\n\r\n";
	const char *expected = "GET /sce/rp/session HTTP/1.1\r\nHost: 192.0.2.1:9295\r\n"
		"RP-Registkey: 0011***\r\nRP-Auth: fake***\r\nRP-Version: 10.0\r\n\r\n";
	char out[OUT_SIZE];
	redact(in, out, sizeof(out));
	assert(!strcmp(out, expected));
}

static void test_cookie_and_token_headers(void)
{
	const char *in = "Cookie: sid=abcd1234fake; other=zz\r\nSet-Cookie: npsso=wxyz5678fake; Path=/\r\n"
		"X-Foo-Token: qrst9012fake\r\nX-Foo-Tokens: visible\r\n";
	const char *expected = "Cookie: sid=abcd***\r\nSet-Cookie: npsso=wxyz***\r\n"
		"X-Foo-Token: qrst***\r\nX-Foo-Tokens: visible\r\n";
	char out[OUT_SIZE];
	redact(in, out, sizeof(out));
	assert(!strcmp(out, expected));
}

// Names are case-insensitive and camelCase spellings count.
static void test_camel_case_and_case_insensitive_keys(void)
{
	const char *in = "{\"accessToken\":\"abcd1234fake\",\"ClientSecret\":\"wxyz5678fake\","
		"\"npsso\":\"qrst9012fake\",\"authCode\":\"uvwx3456fake\"} idToken=hijk7890fake&Code=lmno1234fake";
	const char *expected = "{\"accessToken\":\"abcd***\",\"ClientSecret\":\"wxyz***\","
		"\"npsso\":\"qrst***\",\"authCode\":\"uvwx***\"} idToken=hijk***&Code=lmno***";
	char out[OUT_SIZE];
	redact(in, out, sizeof(out));
	assert(!strcmp(out, expected));
}

// The authorization code comes back in the redirect URL of the sign-in flow.
static void test_query_string_in_logged_line(void)
{
	const char *in = "Redirected to https://example.invalid/cb?code=abcd1234XYZ&state=keepme done";
	const char *expected = "Redirected to https://example.invalid/cb?code=abcd***&state=keepme done";
	char out[OUT_SIZE];
	redact(in, out, sizeof(out));
	assert(!strcmp(out, expected));
}

// The stream key in a connection-request JSON and the wake credential in a discovery packet.
static void test_stream_key_and_wake_credential(void)
{
	const char *in = "{\"sid\":7,\"skey\":\"QUJDREVGRw==fake\"}\nuser-credential:1234567890123\nclient-type:vr\n";
	const char *expected = "{\"sid\":7,\"skey\":\"QUJD***\"}\nuser-credential:1234***\nclient-type:vr\n";
	char out[OUT_SIZE];
	redact(in, out, sizeof(out));
	assert(!strcmp(out, expected));
}

typedef struct
{
	char text[16384];
} LogCapture;

static void capture_cb(ChiakiLogLevel level, const char *msg, void *user)
{
	(void)level;
	LogCapture *cap = user;
	strncat(cap->text, msg, sizeof(cap->text) - strlen(cap->text) - 2);
	strcat(cap->text, "\n");
}

// The hexdump of an HTTP request must not hold the credential, in the hex or the ASCII column.
static void test_hexdump_redacted_hides_secret(void)
{
	const char *in = "RP-Auth: fakeauth1234567\r\nHost: example.invalid\r\n";
	static LogCapture cap;
	ChiakiLog log;
	cap.text[0] = '\0';
	chiaki_log_init(&log, CHIAKI_LOG_ALL, capture_cb, &cap);
	chiaki_log_hexdump_redacted(&log, CHIAKI_LOG_VERBOSE, (const uint8_t *)in, strlen(in));
	assert(strstr(cap.text, "fake"));            // the 4 kept characters are visible
	assert(!strstr(cap.text, "fakeauth"));       // ASCII column
	assert(!strstr(cap.text, "61 75 74 68"));    // hex of "auth", the 5th to 8th secret chars
	assert(strstr(cap.text, "example"));         // the rest of the request is still dumped
}

// A long buffer is cut, says so, and a secret straddling the cut still shows only 4 characters.
static void test_hexdump_redacted_cut(void)
{
	static char in[3000];
	static LogCapture cap;
	ChiakiLog log;
	memset(in, 'a', sizeof(in));
	in[1009] = '\n';
	memcpy(in + 1010, "RP-Key: fakekey123456789", 24);
	cap.text[0] = '\0';
	chiaki_log_init(&log, CHIAKI_LOG_ALL, capture_cb, &cap);
	chiaki_log_hexdump_redacted(&log, CHIAKI_LOG_VERBOSE, (const uint8_t *)in, sizeof(in));
	assert(strstr(cap.text, "cut at 1024 of 3000 bytes"));
	assert(strstr(cap.text, "fake"));
	assert(!strstr(cap.text, "fakekey"));
}

int main(void)
{
	test_bearer_header_block();
	test_basic_auth_header();
	test_token_json();
	test_form_params();
	test_regist_payload();
	test_no_secrets_is_identical();
	test_input_without_nul();
	test_small_buffer_never_leaks();
	test_session_request_headers();
	test_cookie_and_token_headers();
	test_camel_case_and_case_insensitive_keys();
	test_query_string_in_logged_line();
	test_stream_key_and_wake_credential();
	test_hexdump_redacted_hides_secret();
	test_hexdump_redacted_cut();
	printf("redact_tests: all passed\n");
	return 0;
}
