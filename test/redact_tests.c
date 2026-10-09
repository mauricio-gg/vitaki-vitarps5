// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native unit test for lib/src/redact.c (GH #361): credentials must never reach a log line
// with more than their first 4 characters. All token values below are made up.
//
// `./tools/build.sh test` only cross-compiles for arm-vita-eabi and never runs the ELF, so this
// native run is the real check.
//
// Build & run:
//   cc -std=c99 -I lib/include test/redact_tests.c lib/src/redact.c -o /tmp/redact_tests && \
//      /tmp/redact_tests

#include <assert.h>
#include <stdio.h>
#include <string.h>

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
	printf("redact_tests: all passed\n");
	return 0;
}
