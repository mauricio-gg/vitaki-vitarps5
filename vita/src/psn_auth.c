#include <ctype.h>
#include <curl/curl.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <chiaki/remote/holepunch.h>

#include "config.h"
#include "context.h"
#include "psn_auth.h"

#ifndef VITARPS5_PSN_OAUTH_DEVICE_CODE_URL
#define VITARPS5_PSN_OAUTH_DEVICE_CODE_URL ""
#endif
#ifndef VITARPS5_PSN_OAUTH_AUTHORIZE_URL
#define VITARPS5_PSN_OAUTH_AUTHORIZE_URL \
  "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/authorize"
#endif
#ifndef VITARPS5_PSN_OAUTH_TOKEN_URL
#define VITARPS5_PSN_OAUTH_TOKEN_URL "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/token"
#endif
#ifndef VITARPS5_PSN_OAUTH_CLIENT_ID
#error "VITARPS5_PSN_OAUTH_CLIENT_ID must be provided via .env.prod/.env.testing -> CMake"
#endif
#ifndef VITARPS5_PSN_OAUTH_CLIENT_SECRET
#error "VITARPS5_PSN_OAUTH_CLIENT_SECRET must be provided via .env.prod/.env.testing -> CMake"
#endif
#ifndef VITARPS5_PSN_OAUTH_SCOPE
#define VITARPS5_PSN_OAUTH_SCOPE                           \
  "psn:clientapp referenceDataService:countryConfig.read " \
  "pushNotification:webSocket.desktop.connect "            \
  "sessionManager:remotePlaySession.system.update"
#endif
#ifndef VITARPS5_PSN_OAUTH_REDIRECT_URI
#define VITARPS5_PSN_OAUTH_REDIRECT_URI "https://remoteplay.dl.playstation.net/remoteplay/redirect"
#endif

#define TOKEN_EXPIRY_SKEW_SEC 90ULL
#define RESPONSE_CAP_BYTES (16 * 1024)
#define AUTH_VERIFICATION_URL_MAX 1536
#define PSN_CA_BUNDLE_PATH "app0:/assets/psn-ca-bundle.pem"

typedef struct {
  PsnAuthState state;
  char last_error[192];
  char device_code[256];
  char user_code[64];
  char verification_url[AUTH_VERIFICATION_URL_MAX];
  uint64_t device_code_expires_at_unix;
  uint64_t next_poll_unix;
  uint32_t poll_interval_sec;
} PsnAuthRuntime;

typedef struct {
  char *data;
  size_t len;
  bool truncated;
} HttpBuffer;

#ifdef VITARPS5_DEBUG_OAUTH
typedef struct {
  const char *url;
} CurlDebugContext;
#endif /* VITARPS5_DEBUG_OAUTH */

static PsnAuthRuntime g_psn_auth = {
    .state = PSN_AUTH_STATE_LOGGED_OUT,
    .poll_interval_sec = 5,
};

static bool has_text(const char *s) {
  return s && s[0];
}

static const char *oauth_device_code_url(void) {
  if (has_text(context.config.psn_oauth_device_code_url))
    return context.config.psn_oauth_device_code_url;
  return VITARPS5_PSN_OAUTH_DEVICE_CODE_URL;
}

static const char *oauth_authorize_url(void) {
  if (has_text(context.config.psn_oauth_authorize_url))
    return context.config.psn_oauth_authorize_url;
  return VITARPS5_PSN_OAUTH_AUTHORIZE_URL;
}

static const char *oauth_token_url(void) {
  if (has_text(context.config.psn_oauth_token_url))
    return context.config.psn_oauth_token_url;
  return VITARPS5_PSN_OAUTH_TOKEN_URL;
}

static const char *oauth_client_id(void) {
  if (has_text(context.config.psn_oauth_client_id))
    return context.config.psn_oauth_client_id;
  return VITARPS5_PSN_OAUTH_CLIENT_ID;
}

static const char *oauth_client_secret(void) {
  if (has_text(context.config.psn_oauth_client_secret))
    return context.config.psn_oauth_client_secret;
  return VITARPS5_PSN_OAUTH_CLIENT_SECRET;
}

static const char *oauth_scope(void) {
  if (has_text(context.config.psn_oauth_scope))
    return context.config.psn_oauth_scope;
  return VITARPS5_PSN_OAUTH_SCOPE;
}

static const char *oauth_redirect_uri(void) {
  if (has_text(context.config.psn_oauth_redirect_uri))
    return context.config.psn_oauth_redirect_uri;
  return VITARPS5_PSN_OAUTH_REDIRECT_URI;
}

bool psn_auth_enabled(void) {
  return context.config.psn_remoteplay_enabled;
}

bool psn_auth_has_tokens(void) {
  return has_text(context.config.psn_oauth_access_token) &&
         has_text(context.config.psn_oauth_refresh_token);
}

bool psn_auth_token_is_valid(uint64_t now_unix) {
  if (!psn_auth_enabled() || !has_text(context.config.psn_oauth_access_token) ||
      context.config.psn_oauth_expires_at_unix == 0) {
    return false;
  }
  return now_unix + TOKEN_EXPIRY_SKEW_SEC < context.config.psn_oauth_expires_at_unix;
}

const char *psn_auth_access_token(void) {
  return context.config.psn_oauth_access_token;
}

static void psn_auth_clear_error(void) {
  g_psn_auth.last_error[0] = '\0';
}

static void psn_auth_set_error(const char *msg) {
  g_psn_auth.state = PSN_AUTH_STATE_ERROR;
  if (!msg)
    msg = "Unknown PSN authentication error";
  snprintf(g_psn_auth.last_error, sizeof(g_psn_auth.last_error), "%s", msg);
  LOGE("PSN auth: %s", g_psn_auth.last_error);
}

static void set_config_string(char **dst, const char *src) {
  if (!dst)
    return;
  free(*dst);
  *dst = NULL;
  if (has_text(src))
    *dst = strdup(src);
}

static bool ensure_client_duid(void) {
  if (has_text(context.config.psn_client_duid))
    return true;

  char duid[CHIAKI_DUID_STR_SIZE];
  size_t duid_size = sizeof(duid);
  if (chiaki_holepunch_generate_client_device_uid(duid, &duid_size) != CHIAKI_ERR_SUCCESS) {
    LOGE("PSN auth: failed to generate client DUID");
    return false;
  }

  set_config_string(&context.config.psn_client_duid, duid);
  LOGD("PSN auth generated client DUID: %s", context.config.psn_client_duid);
  return true;
}

void psn_auth_clear_tokens(void) {
  set_config_string(&context.config.psn_oauth_access_token, NULL);
  set_config_string(&context.config.psn_oauth_refresh_token, NULL);
  context.config.psn_oauth_expires_at_unix = 0;
  psn_auth_cancel_device_login();
}

static void *auth_realloc(void *ptr, size_t new_size) {
  if (new_size > RESPONSE_CAP_BYTES)
    return NULL;
  return realloc(ptr, new_size);
}

static size_t auth_write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t append = size * nmemb;
  HttpBuffer *buf = (HttpBuffer *)userp;
  if (!buf || append == 0)
    return 0;
  size_t next_len = buf->len + append;
  char *next = auth_realloc(buf->data, next_len + 1);
  if (!next) {
    if (!buf->truncated) {
      buf->truncated = true;
      LOGE("PSN auth HTTP response exceeded %u bytes; truncating", (unsigned)RESPONSE_CAP_BYTES);
    }
    /* Return full chunk size so libcurl does not abort with CURLE_WRITE_ERROR.
     * Overflow bytes are intentionally discarded. */
    return append;
  }
  buf->data = next;
  memcpy(buf->data + buf->len, contents, append);
  buf->len = next_len;
  buf->data[buf->len] = '\0';
  return append;
}

static bool append_form_kv(CURL *curl, char *dst, size_t dst_size, size_t *off, const char *key,
                           const char *value) {
  if (!curl || !dst || !off || !key || !has_text(value))
    return true;
  char *enc = curl_easy_escape(curl, value, 0);
  if (!enc)
    return false;
  int wrote = snprintf(dst + *off, dst_size - *off, "%s%s=%s", (*off > 0) ? "&" : "", key, enc);
  curl_free(enc);
  if (wrote < 0 || (size_t)wrote >= dst_size - *off)
    return false;
  *off += (size_t)wrote;
  return true;
}

static bool parse_url_host_port(const char *url, char *host, size_t host_size, char *port,
                                size_t port_size) {
  const char *scheme_end;
  const char *host_start;
  const char *host_end;
  if (!has_text(url) || !host || host_size == 0 || !port || port_size == 0)
    return false;

  host[0] = '\0';
  snprintf(port, port_size, "%s", "443");
  scheme_end = strstr(url, "://");
  host_start = scheme_end ? scheme_end + 3 : url;
  if (*host_start == '[') {
    host_start++;
    host_end = strchr(host_start, ']');
    if (!host_end)
      return false;
    if (host_end[1] == ':')
      snprintf(port, port_size, "%.*s", (int)strcspn(host_end + 2, "/?#"), host_end + 2);
  } else {
    host_end = host_start + strcspn(host_start, ":/?#");
    if (*host_end == ':')
      snprintf(port, port_size, "%.*s", (int)strcspn(host_end + 1, "/?#"), host_end + 1);
  }

  if ((size_t)(host_end - host_start) >= host_size)
    return false;
  snprintf(host, host_size, "%.*s", (int)(host_end - host_start), host_start);
  return has_text(host);
}

static void log_sockaddr_text(const struct sockaddr *addr, char *buf, size_t buf_size) {
  void *src = NULL;
  if (!addr || !buf || buf_size == 0) {
    return;
  }
  buf[0] = '\0';
  if (addr->sa_family == AF_INET) {
    src = &((const struct sockaddr_in *)addr)->sin_addr;
  } else if (addr->sa_family == AF_INET6) {
    src = &((const struct sockaddr_in6 *)addr)->sin6_addr;
  } else {
    snprintf(buf, buf_size, "family=%d", addr->sa_family);
    return;
  }
  if (!inet_ntop(addr->sa_family, src, buf, buf_size))
    snprintf(buf, buf_size, "inet_ntop_error=%d", errno);
}

static void log_oauth_transport_probe(const char *url) {
  char host[256];
  char port[16];
  struct addrinfo hints = {0};
  struct addrinfo *res = NULL;
  struct addrinfo *ai = NULL;
  int gai_rc;

  if (!parse_url_host_port(url, host, sizeof(host), port, sizeof(port))) {
    LOGE("PSN auth probe could not parse URL host from %s", url ? url : "<null>");
    return;
  }

  LOGD("PSN auth probe resolving host=%s port=%s", host, port);
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  gai_rc = getaddrinfo(host, port, &hints, &res);
  if (gai_rc != 0) {
    LOGE("PSN auth probe getaddrinfo failed host=%s port=%s rc=%d (%s)", host, port, gai_rc,
         gai_strerror(gai_rc));
    return;
  }

  for (ai = res; ai; ai = ai->ai_next) {
    char addr_buf[INET6_ADDRSTRLEN + 16];
    int sock_fd;
    int rc;
    log_sockaddr_text(ai->ai_addr, addr_buf, sizeof(addr_buf));
    LOGD("PSN auth probe candidate family=%d socktype=%d proto=%d addr=%s", ai->ai_family,
         ai->ai_socktype, ai->ai_protocol, addr_buf);

    sock_fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sock_fd < 0) {
      LOGE("PSN auth probe socket failed host=%s addr=%s errno=%d", host, addr_buf, errno);
      continue;
    }

    rc = connect(sock_fd, ai->ai_addr, (socklen_t)ai->ai_addrlen);
    if (rc == 0) {
      LOGD("PSN auth probe TCP connect succeeded host=%s addr=%s", host, addr_buf);
    } else {
      LOGE("PSN auth probe TCP connect failed host=%s addr=%s errno=%d", host, addr_buf, errno);
    }
    close(sock_fd);
  }

  freeaddrinfo(res);
}

/*
 * VITARPS5_DEBUG_OAUTH: DEVELOPMENT ONLY.
 * Enables verbose libcurl tracing, which emits full outbound HTTP headers
 * (including Authorization: Basic ...) and request/response bodies to the
 * log file. NEVER enable in a build that will be shipped or used to
 * generate a support bundle — credentials and tokens will leak.
 */
#ifdef VITARPS5_DEBUG_OAUTH
static int oauth_curl_debug_cb(CURL *handle, curl_infotype type, char *data, size_t size,
                               void *userp) {
  (void)handle;
  CurlDebugContext *ctx = (CurlDebugContext *)userp;
  const char *url = (ctx && ctx->url) ? ctx->url : "<unknown>";
  if (!data || size == 0)
    return 0;

  switch (type) {
    case CURLINFO_TEXT:
      LOGD("PSN auth curl info url=%s text=%.*s", url, (int)size, data);
      break;
    case CURLINFO_HEADER_OUT:
      LOGD("PSN auth curl header_out url=%s data=%.*s", url, (int)size, data);
      break;
    case CURLINFO_HEADER_IN:
      LOGD("PSN auth curl header_in url=%s data=%.*s", url, (int)size, data);
      break;
    case CURLINFO_SSL_DATA_OUT:
      LOGD("PSN auth curl ssl_data_out url=%s size=%u", url, (unsigned)size);
      break;
    case CURLINFO_SSL_DATA_IN:
      LOGD("PSN auth curl ssl_data_in url=%s size=%u", url, (unsigned)size);
      break;
    case CURLINFO_DATA_OUT:
      LOGD("PSN auth curl data_out url=%s size=%u", url, (unsigned)size);
      break;
    case CURLINFO_DATA_IN:
      LOGD("PSN auth curl data_in url=%s size=%u", url, (unsigned)size);
      break;
    default:
      break;
  }
  return 0;
}
#endif /* VITARPS5_DEBUG_OAUTH */

static bool oauth_post_form(const char *url, const char *form_data, const char *basic_user,
                            const char *basic_pass, long *http_code_out, char **response_out) {
  if (!has_text(url) || !has_text(form_data))
    return false;

  bool ok = false;
  CURL *curl = curl_easy_init();
  struct curl_slist *headers = NULL;
  HttpBuffer response = {.data = NULL, .len = 0};
#ifdef VITARPS5_DEBUG_OAUTH
  CurlDebugContext debug_ctx = {.url = url};
#endif /* VITARPS5_DEBUG_OAUTH */
  long http_code = 0;
  char error_buf[CURL_ERROR_SIZE] = {0};
  long verify_peer = 1;
  long verify_host = 2;
  char *effective_url = NULL;
  long primary_port = 0;
  char *primary_ip = NULL;
  long local_port = 0;
  char *local_ip = NULL;
  FILE *ca_file = NULL;

  if (!curl) {
    LOGE("PSN auth HTTP init failed for %s", url ? url : "<null>");
    goto cleanup;
  }

  headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
  headers = curl_slist_append(headers, "Accept: application/json");

  ca_file = fopen(PSN_CA_BUNDLE_PATH, "rb");
  if (!ca_file) {
    LOGE("PSN auth CA bundle open failed path=%s errno=%d", PSN_CA_BUNDLE_PATH, errno);
    goto cleanup;
  }
  fclose(ca_file);
  ca_file = NULL;

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form_data);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
  curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
  curl_easy_setopt(curl, CURLOPT_CAINFO, PSN_CA_BUNDLE_PATH);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buf);
#ifdef VITARPS5_DEBUG_OAUTH
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
  curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, oauth_curl_debug_cb);
  curl_easy_setopt(curl, CURLOPT_DEBUGDATA, &debug_ctx);
#endif /* VITARPS5_DEBUG_OAUTH */
  if (has_text(basic_user) && has_text(basic_pass)) {
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(curl, CURLOPT_USERNAME, basic_user);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, basic_pass);
  }
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, auth_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  LOGD("PSN auth HTTP POST url=%s cafile=%s basic_user=%s basic_pass_present=%s form_len=%u", url,
       PSN_CA_BUNDLE_PATH, has_text(basic_user) ? basic_user : "<none>",
       has_text(basic_pass) ? "true" : "false", (unsigned)strlen(form_data));
  curl_easy_getinfo(curl, CURLINFO_SSL_VERIFYRESULT, &verify_peer);
  LOGD("PSN auth curl preflight url=%s ipresolve=v4 timeout=15 verify_peer_result=%ld", url,
       verify_peer);

  CURLcode perform_res = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective_url);
  curl_easy_getinfo(curl, CURLINFO_PRIMARY_IP, &primary_ip);
  curl_easy_getinfo(curl, CURLINFO_PRIMARY_PORT, &primary_port);
  curl_easy_getinfo(curl, CURLINFO_LOCAL_IP, &local_ip);
  curl_easy_getinfo(curl, CURLINFO_LOCAL_PORT, &local_port);
  if (perform_res != CURLE_OK) {
    log_oauth_transport_probe(url);
    curl_easy_getinfo(curl, CURLINFO_SSL_VERIFYRESULT, &verify_peer);
    curl_easy_getinfo(curl, CURLINFO_HTTPAUTH_AVAIL, &verify_host);
    LOGE(
        "PSN auth HTTP transport failed url=%s effective_url=%s curl=%d (%s) errbuf=%s "
        "primary_ip=%s primary_port=%ld local_ip=%s local_port=%ld ssl_verify_result=%ld "
        "httpauth_avail=%ld response_len=%u truncated=%s",
        url, effective_url ? effective_url : "<none>", (int)perform_res,
        curl_easy_strerror(perform_res), error_buf[0] ? error_buf : "<empty>",
        primary_ip ? primary_ip : "<none>", primary_port, local_ip ? local_ip : "<none>",
        local_port, verify_peer, verify_host, (unsigned)response.len,
        response.truncated ? "true" : "false");
    goto cleanup;
  }
  CURLcode info_res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  if (info_res != CURLE_OK) {
    LOGE("PSN auth HTTP status lookup failed url=%s curl=%d (%s) response_len=%u truncated=%s", url,
         (int)info_res, curl_easy_strerror(info_res), (unsigned)response.len,
         response.truncated ? "true" : "false");
    goto cleanup;
  }

  LOGD(
      "PSN auth HTTP response url=%s effective_url=%s primary_ip=%s primary_port=%ld local_ip=%s "
      "local_port=%ld status=%ld response_len=%u truncated=%s",
      url, effective_url ? effective_url : "<none>", primary_ip ? primary_ip : "<none>",
      primary_port, local_ip ? local_ip : "<none>", local_port, http_code, (unsigned)response.len,
      response.truncated ? "true" : "false");

  if (http_code_out)
    *http_code_out = http_code;
  if (response_out) {
    *response_out = response.data;
    response.data = NULL;
  }
  ok = true;

cleanup:
  if (ca_file)
    fclose(ca_file);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  free(response.data);
  return ok;
}

static const char *json_find_key_value(const char *json, const char *key) {
  if (!json || !key)
    return NULL;
  char key_expr[96];
  if (snprintf(key_expr, sizeof(key_expr), "\"%s\"", key) <= 0)
    return NULL;

  const char *p = strstr(json, key_expr);
  if (!p)
    return NULL;
  p += strlen(key_expr);
  while (*p && isspace((unsigned char)*p))
    p++;
  if (*p != ':')
    return NULL;
  p++;
  while (*p && isspace((unsigned char)*p))
    p++;
  return p;
}

/* ---------------------------------------------------------------------------
 * Internal UTF-8 / \uXXXX helpers shared by json_get_string_len and
 * json_get_string.  All three are file-scope only.
 * --------------------------------------------------------------------------- */

/* Parses exactly 4 hexadecimal characters starting at *pp into *out_val.
 * Advances *pp by 4 on success.  Returns false (without advancing) if any
 * of the four characters is not a valid hex digit. */
static bool json_parse_hex4(const char **pp, unsigned int *out_val) {
  const char *p = *pp;
  unsigned int v = 0;
  int i;
  for (i = 0; i < 4; i++) {
    unsigned char c = (unsigned char)p[i];
    unsigned int digit;
    if (c >= '0' && c <= '9')
      digit = c - '0';
    else if (c >= 'a' && c <= 'f')
      digit = 10 + (c - 'a');
    else if (c >= 'A' && c <= 'F')
      digit = 10 + (c - 'A');
    else
      return false;
    v = (v << 4) | digit;
  }
  *out_val = v;
  *pp = p + 4;
  return true;
}

/* Returns the number of UTF-8 bytes needed to encode Unicode codepoint cp.
 * cp must be a valid Unicode scalar value (U+0000..U+10FFFF, excluding
 * surrogates); the caller is responsible for that precondition. */
static int json_utf8_len(unsigned long cp) {
  if (cp <= 0x7FUL)
    return 1;
  if (cp <= 0x7FFUL)
    return 2;
  if (cp <= 0xFFFFUL)
    return 3;
  return 4;
}

/* Encodes Unicode codepoint cp into buf (which must have at least 4 bytes).
 * Returns the number of bytes written.  Same precondition as json_utf8_len. */
static int json_utf8_encode(unsigned long cp, unsigned char *buf) {
  if (cp <= 0x7FUL) {
    buf[0] = (unsigned char)cp;
    return 1;
  }
  if (cp <= 0x7FFUL) {
    buf[0] = (unsigned char)(0xC0u | (cp >> 6));
    buf[1] = (unsigned char)(0x80u | (cp & 0x3Fu));
    return 2;
  }
  if (cp <= 0xFFFFUL) {
    buf[0] = (unsigned char)(0xE0u | (cp >> 12));
    buf[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
    buf[2] = (unsigned char)(0x80u | (cp & 0x3Fu));
    return 3;
  }
  buf[0] = (unsigned char)(0xF0u | (cp >> 18));
  buf[1] = (unsigned char)(0x80u | ((cp >> 12) & 0x3Fu));
  buf[2] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
  buf[3] = (unsigned char)(0x80u | (cp & 0x3Fu));
  return 4;
}

/* Consumes a \uXXXX escape (the leading backslash and 'u' have already been
 * consumed by the caller; *pp points to the first hex digit).  Handles UTF-16
 * surrogate pairs by peeking ahead for a following \uXXXX low-surrogate.
 *
 * On success, advances *pp past all consumed characters and stores the decoded
 * Unicode codepoint in *out_cp.  Returns false on malformed input (non-hex
 * digits, lone high surrogate, unexpected low surrogate without a preceding
 * high surrogate). */
static bool json_decode_unicode_escape(const char **pp, unsigned long *out_cp) {
  unsigned int hi;
  if (!json_parse_hex4(pp, &hi))
    return false;

  /* High surrogate: must be followed immediately by \uDC00-\uDFFF. */
  if (hi >= 0xD800u && hi <= 0xDBFFu) {
    if ((*pp)[0] != '\\' || (*pp)[1] != 'u')
      return false; /* lone high surrogate — reject */
    *pp += 2;       /* consume the '\' and 'u' of the low surrogate escape */
    unsigned int lo;
    if (!json_parse_hex4(pp, &lo))
      return false;
    if (lo < 0xDC00u || lo > 0xDFFFu)
      return false; /* second escape is not a valid low surrogate */
    /* RFC 2781 surrogate-pair reassembly → supplementary codepoint */
    *out_cp = 0x10000UL + (((unsigned long)(hi - 0xD800u)) << 10) + (unsigned long)(lo - 0xDC00u);
    return true;
  }

  /* Low surrogate without a preceding high surrogate: reject. */
  if (hi >= 0xDC00u && hi <= 0xDFFFu)
    return false;

  *out_cp = (unsigned long)hi;
  return true;
}

/* ---------------------------------------------------------------------------
 * JSON string helpers
 * --------------------------------------------------------------------------- */

/* Returns the byte length (excluding null terminator) of the decoded JSON
 * string value for `key`.  Mirrors the escape-handling logic of
 * json_get_string exactly so that json_get_string_len(...) + 1 is always the
 * correct allocation size for json_get_string.
 *
 * \uXXXX escapes are decoded to UTF-8; surrogate pairs are combined into a
 * single supplementary codepoint before encoding.  Sets *out_len and returns
 * true on success; returns false if the key is not found, the value is not a
 * quoted string, the closing quote is missing, or a \uXXXX escape is
 * malformed (non-hex digits, lone surrogate, etc.). */
static bool json_get_string_len(const char *json, const char *key, size_t *out_len) {
  if (!out_len)
    return false;
  *out_len = 0;
  const char *p = json_find_key_value(json, key);
  if (!p || *p != '"')
    return false;

  p++;
  size_t len = 0;
  while (*p && *p != '"') {
    if (*p == '\\') {
      p++;
      if (!*p)
        return false; /* truncated escape — parse failure */
      if (*p == 'u') {
        /* \uXXXX — may be a surrogate pair spanning two \uXXXX sequences. */
        p++;
        unsigned long cp;
        if (!json_decode_unicode_escape(&p, &cp))
          return false;
        len += (size_t)json_utf8_len(cp);
      } else {
        /* All other single-character escapes decode to exactly one byte:
         * \" \\ \/ \b \f \n \r \t and any other char (liberal). */
        len++;
        p++;
      }
      continue;
    }
    len++;
    p++;
  }
  if (*p != '"')
    return false;
  *out_len = len;
  return true;
}

static bool json_get_string(const char *json, const char *key, char *out, size_t out_size) {
  if (!out || out_size == 0)
    return false;
  out[0] = '\0';
  const char *p = json_find_key_value(json, key);
  if (!p || *p != '"')
    return false;

  p++;
  size_t o = 0;
  while (*p && *p != '"') {
    if (*p == '\\') {
      p++;
      if (!*p)
        return false; /* truncated escape — parse failure */
      if (*p == 'u') {
        /* \uXXXX — decode to UTF-8, possibly via a surrogate pair. */
        p++;
        unsigned long cp;
        if (!json_decode_unicode_escape(&p, &cp))
          return false;
        unsigned char utf8[4];
        int nb = json_utf8_encode(cp, utf8);
        int i;
        /* Emit UTF-8 bytes; stop if they would not fit (preserve existing
         * behaviour of other escapes which also stop at out_size - 1). */
        if (o + (size_t)nb + 1 > out_size)
          break;
        for (i = 0; i < nb; i++)
          out[o++] = (char)utf8[i];
        continue;
      }
      /* Single-character escapes. */
      if (o + 1 >= out_size)
        break;
      switch (*p) {
        case '"':
        case '\\':
        case '/':
          out[o++] = *p;
          break;
        case 'b':
          out[o++] = '\b';
          break;
        case 'f':
          out[o++] = '\f';
          break;
        case 'n':
          out[o++] = '\n';
          break;
        case 'r':
          out[o++] = '\r';
          break;
        case 't':
          out[o++] = '\t';
          break;
        default:
          out[o++] = *p;
          break;
      }
      p++;
      continue;
    }
    if (o + 1 >= out_size)
      break;
    out[o++] = *p++;
  }
  out[o] = '\0';
  return *p == '"';
}

static bool json_get_uint64(const char *json, const char *key, uint64_t *out) {
  if (!out)
    return false;
  const char *p = json_find_key_value(json, key);
  if (!p || !isdigit((unsigned char)*p))
    return false;
  uint64_t v = 0;
  while (*p && isdigit((unsigned char)*p)) {
    v = (v * 10ULL) + (uint64_t)(*p - '0');
    p++;
  }
  *out = v;
  return true;
}

static void clear_device_flow_fields(void) {
  g_psn_auth.device_code[0] = '\0';
  g_psn_auth.user_code[0] = '\0';
  g_psn_auth.verification_url[0] = '\0';
  g_psn_auth.device_code_expires_at_unix = 0;
  g_psn_auth.next_poll_unix = 0;
  g_psn_auth.poll_interval_sec = 5;
}

static bool apply_token_response(const char *response, uint64_t now_unix) {
  size_t access_len = 0;
  size_t refresh_len = 0;
  if (!json_get_string_len(response, "access_token", &access_len) || access_len == 0) {
    LOGE("PSN auth: access_token missing from token response");
    return false;
  }
  if (access_len >= 8192) {
    LOGE("PSN auth: access_token length %zu exceeds 8192-byte cap", access_len);
    return false;
  }
  char *access_token = malloc(access_len + 1);
  if (!access_token)
    return false;
  if (!json_get_string(response, "access_token", access_token, access_len + 1)) {
    free(access_token);
    return false;
  }

  char *refresh_token = NULL;
  if (json_get_string_len(response, "refresh_token", &refresh_len) && refresh_len > 0) {
    if (refresh_len >= 8192) {
      LOGE("PSN auth: refresh_token length %zu exceeds 8192-byte cap", refresh_len);
      free(access_token);
      return false;
    }
    refresh_token = malloc(refresh_len + 1);
    if (!refresh_token) {
      free(access_token);
      return false;
    }
    if (!json_get_string(response, "refresh_token", refresh_token, refresh_len + 1)) {
      free(access_token);
      free(refresh_token);
      return false;
    }
  } else if (has_text(context.config.psn_oauth_refresh_token)) {
    refresh_token = strdup(context.config.psn_oauth_refresh_token);
    if (!refresh_token) {
      free(access_token);
      return false;
    }
  } else {
    refresh_token = strdup("");
    if (!refresh_token) {
      free(access_token);
      return false;
    }
  }

  uint64_t expires_in = 0;
  if (!json_get_uint64(response, "expires_in", &expires_in) || expires_in == 0) {
    expires_in = 3600;
  }

  /* set_config_string calls strdup internally — safe to free our copies after. */
  set_config_string(&context.config.psn_oauth_access_token, access_token);
  set_config_string(&context.config.psn_oauth_refresh_token, refresh_token);
  free(access_token);
  free(refresh_token);
  context.config.psn_oauth_expires_at_unix = now_unix + expires_in;
  clear_device_flow_fields();
  g_psn_auth.state = PSN_AUTH_STATE_TOKEN_VALID;
  psn_auth_clear_error();
  return true;
}

static bool oauth_configured_for_device_flow(void) {
  return has_text(oauth_authorize_url()) && has_text(oauth_token_url()) &&
         has_text(oauth_redirect_uri()) && has_text(oauth_client_id());
}

static bool oauth_configured_for_refresh(void) {
  return has_text(oauth_token_url()) && has_text(oauth_client_id());
}

PsnAuthState psn_auth_state(uint64_t now_unix) {
  if (!psn_auth_enabled())
    return PSN_AUTH_STATE_DISABLED;

  if (g_psn_auth.state == PSN_AUTH_STATE_DEVICE_LOGIN_PENDING ||
      g_psn_auth.state == PSN_AUTH_STATE_DEVICE_LOGIN_POLLING ||
      g_psn_auth.state == PSN_AUTH_STATE_TOKEN_REFRESHING) {
    return g_psn_auth.state;
  }
  if (g_psn_auth.state == PSN_AUTH_STATE_ERROR && has_text(g_psn_auth.last_error)) {
    return PSN_AUTH_STATE_ERROR;
  }

  if (psn_auth_token_is_valid(now_unix))
    return PSN_AUTH_STATE_TOKEN_VALID;

  if (has_text(context.config.psn_oauth_access_token) ||
      has_text(context.config.psn_oauth_refresh_token))
    return PSN_AUTH_STATE_LOGGED_OUT;

  return PSN_AUTH_STATE_LOGGED_OUT;
}

const char *psn_auth_last_error(void) {
  return g_psn_auth.last_error;
}

const char *psn_auth_state_label(void) {
  uint64_t now_unix = (uint64_t)time(NULL);
  PsnAuthState state = psn_auth_state(now_unix);
  if (state == PSN_AUTH_STATE_DISABLED)
    return "Disabled";
  if (state == PSN_AUTH_STATE_TOKEN_VALID)
    return "Authenticated";
  if (state == PSN_AUTH_STATE_TOKEN_REFRESHING)
    return "Refreshing token";
  if (state == PSN_AUTH_STATE_DEVICE_LOGIN_PENDING || state == PSN_AUTH_STATE_DEVICE_LOGIN_POLLING)
    return "Awaiting browser sign-in";
  if (state == PSN_AUTH_STATE_ERROR && has_text(g_psn_auth.last_error))
    return g_psn_auth.last_error;
  if (psn_auth_has_tokens() && !psn_auth_token_is_valid(now_unix))
    return "Token expired";
  return "Not authenticated";
}

bool psn_auth_device_login_active(void) {
  return g_psn_auth.state == PSN_AUTH_STATE_DEVICE_LOGIN_PENDING ||
         g_psn_auth.state == PSN_AUTH_STATE_DEVICE_LOGIN_POLLING;
}

const char *psn_auth_device_user_code(void) {
  return g_psn_auth.user_code;
}

const char *psn_auth_device_verification_url(void) {
  return g_psn_auth.verification_url;
}

void psn_auth_cancel_device_login(void) {
  clear_device_flow_fields();
  if (psn_auth_enabled() && psn_auth_token_is_valid((uint64_t)time(NULL))) {
    g_psn_auth.state = PSN_AUTH_STATE_TOKEN_VALID;
  } else {
    g_psn_auth.state = PSN_AUTH_STATE_LOGGED_OUT;
  }
}

bool psn_auth_begin_device_login(uint64_t now_unix) {
  if (!psn_auth_enabled()) {
    psn_auth_set_error("PSN internet mode is disabled");
    return false;
  }
  if (!oauth_configured_for_device_flow()) {
    psn_auth_set_error("PSN login setup unavailable in this build");
    return false;
  }
  if (!ensure_client_duid()) {
    psn_auth_set_error("Failed to prepare client device ID");
    return false;
  }

  CURL *curl = curl_easy_init();
  if (!curl) {
    psn_auth_set_error("Failed to initialize OAuth client");
    return false;
  }

  char auth_url[1536];
  size_t off = 0;
  const char *base = oauth_authorize_url();
  int base_wrote =
      snprintf(auth_url, sizeof(auth_url), "%s%s", base, strchr(base, '?') ? "&" : "?");
  if (base_wrote <= 0 || (size_t)base_wrote >= sizeof(auth_url)) {
    curl_easy_cleanup(curl);
    psn_auth_set_error("Failed to build authorization URL");
    return false;
  }
  off = (size_t)base_wrote;
  bool form_ok =
      append_form_kv(curl, auth_url, sizeof(auth_url), &off, "service_entity",
                     "urn:service-entity:psn") &&
      append_form_kv(curl, auth_url, sizeof(auth_url), &off, "response_type", "code") &&
      append_form_kv(curl, auth_url, sizeof(auth_url), &off, "duid",
                     context.config.psn_client_duid) &&
      append_form_kv(curl, auth_url, sizeof(auth_url), &off, "client_id", oauth_client_id()) &&
      append_form_kv(curl, auth_url, sizeof(auth_url), &off, "scope", oauth_scope()) &&
      append_form_kv(curl, auth_url, sizeof(auth_url), &off, "redirect_uri",
                     oauth_redirect_uri()) &&
      append_form_kv(curl, auth_url, sizeof(auth_url), &off, "request_locale", "en_US") &&
      append_form_kv(curl, auth_url, sizeof(auth_url), &off, "ui", "pr") &&
      append_form_kv(curl, auth_url, sizeof(auth_url), &off, "service_logo", "ps") &&
      append_form_kv(curl, auth_url, sizeof(auth_url), &off, "layout_type", "popup") &&
      append_form_kv(curl, auth_url, sizeof(auth_url), &off, "smcid", "remoteplay") &&
      append_form_kv(curl, auth_url, sizeof(auth_url), &off, "prompt", "always") &&
      append_form_kv(curl, auth_url, sizeof(auth_url), &off, "PlatformPrivacyWs1", "minimal");
  curl_easy_cleanup(curl);
  if (!form_ok) {
    psn_auth_set_error("Failed to build authorization URL");
    return false;
  }

  LOGD(
      "PSN auth authorize tuple authorize_url=%s redirect_uri=%s duid=%s client_id=%s scope=%s "
      "locale=%s ui=%s service_logo=%s layout_type=%s smcid=%s prompt=%s privacy=%s",
      oauth_authorize_url(), oauth_redirect_uri(), context.config.psn_client_duid,
      oauth_client_id(), oauth_scope(), "en_US", "pr", "ps", "popup", "remoteplay", "always",
      "minimal");
  clear_device_flow_fields();
  int verify_wrote =
      snprintf(g_psn_auth.verification_url, sizeof(g_psn_auth.verification_url), "%s", auth_url);
  if (verify_wrote < 0 || (size_t)verify_wrote >= sizeof(g_psn_auth.verification_url)) {
    clear_device_flow_fields();
    psn_auth_set_error("Authorization URL too long for this build");
    return false;
  }
  LOGD("PSN auth authorize URL: %s", g_psn_auth.verification_url);
  snprintf(g_psn_auth.user_code, sizeof(g_psn_auth.user_code), "%s", "Paste redirect URL/code");
  g_psn_auth.device_code_expires_at_unix = now_unix + (10 * 60);
  g_psn_auth.state = PSN_AUTH_STATE_DEVICE_LOGIN_PENDING;
  psn_auth_clear_error();
  return true;
}

bool psn_auth_poll_device_login(uint64_t now_unix) {
  (void)now_unix;
  return false;
}

static void trim_ascii_whitespace(const char **start, const char **end) {
  while (*start < *end && isspace((unsigned char)**start))
    (*start)++;
  while (*end > *start && isspace((unsigned char)*((*end) - 1)))
    (*end)--;
}

static bool decode_url_component_range(const char *start, const char *end, char *out,
                                       size_t out_size) {
  if (!out || out_size == 0 || !start || !end || end < start)
    return false;

  size_t o = 0;
  while (start < end && o + 1 < out_size) {
    if (*start == '%' && start + 2 < end && isxdigit((unsigned char)start[1]) &&
        isxdigit((unsigned char)start[2])) {
      char hex[3] = {start[1], start[2], '\0'};
      out[o++] = (char)strtol(hex, NULL, 16);
      start += 3;
      continue;
    }
    if (*start == '+') {
      out[o++] = ' ';
      start++;
      continue;
    }
    out[o++] = *start++;
  }
  out[o] = '\0';
  return o > 0;
}

static bool extract_oauth_param(const char *input, const char *param_name, char *out,
                                size_t out_size) {
  size_t name_len;
  const char *p;
  if (!has_text(input) || !has_text(param_name) || !out || out_size == 0)
    return false;

  name_len = strlen(param_name);
  for (p = input; *p; p++) {
    if ((p == input || p[-1] == '?' || p[-1] == '&' || p[-1] == '#' || p[-1] == ' ' ||
         p[-1] == '\n' || p[-1] == '\r' || p[-1] == '"' || p[-1] == '\'') &&
        strncmp(p, param_name, name_len) == 0 && p[name_len] == '=') {
      const char *value_start = p + name_len + 1;
      const char *value_end = value_start;
      while (*value_end && *value_end != '&' && *value_end != '#' && *value_end != '"' &&
             *value_end != '\'' && *value_end != '\n' && *value_end != '\r')
        value_end++;
      return decode_url_component_range(value_start, value_end, out, out_size);
    }
  }
  return false;
}

static bool parse_auth_code_input(const char *input, char *out_code, size_t out_code_size,
                                  char *out_error, size_t out_error_size) {
  const char *start;
  const char *end;
  if (!has_text(input) || !out_code || out_code_size == 0)
    return false;

  out_code[0] = '\0';
  if (out_error && out_error_size > 0)
    out_error[0] = '\0';

  if (extract_oauth_param(input, "error_description", out_error, out_error_size)) {
    return false;
  }
  if (extract_oauth_param(input, "error", out_error, out_error_size)) {
    return false;
  }
  if (extract_oauth_param(input, "code", out_code, out_code_size)) {
    return true;
  }

  start = input;
  end = input + strlen(input);
  trim_ascii_whitespace(&start, &end);
  if (end > start && ((*start == '"' && end[-1] == '"') || (*start == '\'' && end[-1] == '\''))) {
    start++;
    end--;
    trim_ascii_whitespace(&start, &end);
  }
  return decode_url_component_range(start, end, out_code, out_code_size);
}

static const char *classify_auth_input(const char *input) {
  if (!has_text(input))
    return "empty";
  if (strstr(input, "error=") || strstr(input, "error_description="))
    return "oauth_error";
  if (strstr(input, "code="))
    return "full_url";
  if (strstr(input, "://"))
    return "url_without_code";
  return "bare_code";
}

bool psn_auth_submit_authorization_response(const char *input, uint64_t now_unix) {
  if (!psn_auth_device_login_active()) {
    psn_auth_set_error("Login is not active");
    return false;
  }

  char auth_code[1024];
  char auth_error[160];
  const char *input_type = classify_auth_input(input);
  LOGD("PSN auth submit input_type=%s input_len=%u", input_type,
       (unsigned)(has_text(input) ? strlen(input) : 0));
  if (!parse_auth_code_input(input, auth_code, sizeof(auth_code), auth_error, sizeof(auth_error))) {
    if (auth_error[0])
      psn_auth_set_error(auth_error);
    else
      psn_auth_set_error("Could not parse authorization code");
    return false;
  }
  LOGD("PSN auth parsed authorization code: code_len=%u source=%s", (unsigned)strlen(auth_code),
       strcmp(input_type, "full_url") == 0 ? "full_url" : "fallback");

  CURL *curl = curl_easy_init();
  if (!curl) {
    psn_auth_set_error("Failed to initialize token exchange client");
    return false;
  }

  g_psn_auth.state = PSN_AUTH_STATE_DEVICE_LOGIN_POLLING;
  char form[1800];
  size_t off = 0;
  bool form_ok =
      append_form_kv(curl, form, sizeof(form), &off, "grant_type", "authorization_code") &&
      append_form_kv(curl, form, sizeof(form), &off, "code", auth_code) &&
      append_form_kv(curl, form, sizeof(form), &off, "scope", oauth_scope()) &&
      append_form_kv(curl, form, sizeof(form), &off, "redirect_uri", oauth_redirect_uri());
  curl_easy_cleanup(curl);
  if (!form_ok) {
    psn_auth_set_error("Failed to build token exchange request");
    return false;
  }

  long http_code = 0;
  char *response = NULL;
  LOGD("PSN auth token exchange url=%s redirect_uri=%s client_id=%s form_len=%u", oauth_token_url(),
       oauth_redirect_uri(), oauth_client_id(), (unsigned)strlen(form));
  if (!oauth_post_form(oauth_token_url(), form, oauth_client_id(), oauth_client_secret(),
                       &http_code, &response)) {
    g_psn_auth.state = PSN_AUTH_STATE_DEVICE_LOGIN_PENDING;
    psn_auth_set_error("Authorization code exchange failed");
    return false;
  }

  bool ok = false;
  if (http_code == 200 && apply_token_response(response, now_unix)) {
    LOGD("PSN auth token exchange succeeded response_len=%u",
         (unsigned)(response ? strlen(response) : 0));
    ok = true;
  } else {
    LOGE("PSN auth token exchange rejected status=%ld response_len=%u", http_code,
         (unsigned)(response ? strlen(response) : 0));
    char error_desc[160];
    if (response &&
        json_get_string(response, "error_description", error_desc, sizeof(error_desc))) {
      psn_auth_set_error(error_desc);
    } else {
      psn_auth_set_error("Authorization code was rejected");
    }
    g_psn_auth.state = PSN_AUTH_STATE_DEVICE_LOGIN_PENDING;
  }

  free(response);
  return ok;
}

bool psn_auth_refresh_token_if_needed(uint64_t now_unix, bool force) {
  if (!psn_auth_enabled())
    return false;
  if (!force && psn_auth_token_is_valid(now_unix))
    return true;
  if (!has_text(context.config.psn_oauth_refresh_token))
    return false;
  if (!oauth_configured_for_refresh()) {
    psn_auth_set_error("OAuth refresh endpoint not configured in this build");
    return false;
  }

  CURL *curl = curl_easy_init();
  if (!curl) {
    psn_auth_set_error("Failed to initialize token refresh client");
    return false;
  }

  g_psn_auth.state = PSN_AUTH_STATE_TOKEN_REFRESHING;
  char form[1400];
  size_t off = 0;
  bool form_ok =
      append_form_kv(curl, form, sizeof(form), &off, "grant_type", "refresh_token") &&
      append_form_kv(curl, form, sizeof(form), &off, "refresh_token",
                     context.config.psn_oauth_refresh_token) &&
      append_form_kv(curl, form, sizeof(form), &off, "scope", oauth_scope()) &&
      append_form_kv(curl, form, sizeof(form), &off, "redirect_uri", oauth_redirect_uri());
  curl_easy_cleanup(curl);
  if (!form_ok) {
    psn_auth_set_error("Failed to build refresh request");
    return false;
  }

  long http_code = 0;
  char *response = NULL;
  LOGD("PSN auth refresh exchange url=%s redirect_uri=%s client_id=%s form_len=%u",
       oauth_token_url(), oauth_redirect_uri(), oauth_client_id(), (unsigned)strlen(form));
  if (!oauth_post_form(oauth_token_url(), form, oauth_client_id(), oauth_client_secret(),
                       &http_code, &response)) {
    psn_auth_set_error("Token refresh request failed");
    return false;
  }

  bool refreshed = false;
  if (http_code == 200 && apply_token_response(response, now_unix)) {
    LOGD("PSN auth refresh succeeded response_len=%u", (unsigned)(response ? strlen(response) : 0));
    refreshed = true;
  } else {
    LOGE("PSN auth refresh rejected status=%ld response_len=%u", http_code,
         (unsigned)(response ? strlen(response) : 0));
    char error_desc[160];
    if (response &&
        json_get_string(response, "error_description", error_desc, sizeof(error_desc))) {
      psn_auth_set_error(error_desc);
    } else {
      psn_auth_set_error("Token refresh failed");
    }
  }

  free(response);
  return refreshed;
}
