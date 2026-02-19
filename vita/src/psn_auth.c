#include <ctype.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "context.h"
#include "psn_auth.h"

#ifndef VITARPS5_PSN_OAUTH_DEVICE_CODE_URL
#define VITARPS5_PSN_OAUTH_DEVICE_CODE_URL ""
#endif
#ifndef VITARPS5_PSN_OAUTH_TOKEN_URL
#define VITARPS5_PSN_OAUTH_TOKEN_URL ""
#endif
#ifndef VITARPS5_PSN_OAUTH_CLIENT_ID
#define VITARPS5_PSN_OAUTH_CLIENT_ID ""
#endif
#ifndef VITARPS5_PSN_OAUTH_CLIENT_SECRET
#define VITARPS5_PSN_OAUTH_CLIENT_SECRET ""
#endif
#ifndef VITARPS5_PSN_OAUTH_SCOPE
#define VITARPS5_PSN_OAUTH_SCOPE "psn:clientapp"
#endif

#define TOKEN_EXPIRY_SKEW_SEC 90ULL
#define RESPONSE_CAP_BYTES (16 * 1024)

typedef struct {
  PsnAuthState state;
  char last_error[192];
  char device_code[256];
  char user_code[64];
  char verification_url[256];
  uint64_t device_code_expires_at_unix;
  uint64_t next_poll_unix;
  uint32_t poll_interval_sec;
} PsnAuthRuntime;

typedef struct {
  char *data;
  size_t len;
} HttpBuffer;

static PsnAuthRuntime g_psn_auth = {
    .state = PSN_AUTH_STATE_LOGGED_OUT,
    .poll_interval_sec = 5,
};

static bool has_text(const char *s) {
  return s && s[0];
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
  return now_unix + TOKEN_EXPIRY_SKEW_SEC <
         context.config.psn_oauth_expires_at_unix;
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

static size_t auth_write_cb(void *contents, size_t size, size_t nmemb,
                            void *userp) {
  size_t append = size * nmemb;
  HttpBuffer *buf = (HttpBuffer *)userp;
  if (!buf || append == 0)
    return 0;
  size_t next_len = buf->len + append;
  char *next = auth_realloc(buf->data, next_len + 1);
  if (!next)
    return 0;
  buf->data = next;
  memcpy(buf->data + buf->len, contents, append);
  buf->len = next_len;
  buf->data[buf->len] = '\0';
  return append;
}

static bool append_form_kv(CURL *curl, char *dst, size_t dst_size, size_t *off,
                           const char *key, const char *value) {
  if (!curl || !dst || !off || !key || !has_text(value))
    return true;
  char *enc = curl_easy_escape(curl, value, 0);
  if (!enc)
    return false;
  int wrote = snprintf(dst + *off, dst_size - *off, "%s%s=%s",
                       (*off > 0) ? "&" : "", key, enc);
  curl_free(enc);
  if (wrote < 0 || (size_t)wrote >= dst_size - *off)
    return false;
  *off += (size_t)wrote;
  return true;
}

static bool oauth_post_form(const char *url, const char *form_data,
                            long *http_code_out, char **response_out) {
  if (!has_text(url) || !has_text(form_data))
    return false;

  bool ok = false;
  CURL *curl = curl_easy_init();
  struct curl_slist *headers = NULL;
  HttpBuffer response = {.data = NULL, .len = 0};
  long http_code = 0;

  if (!curl)
    goto cleanup;

  headers = curl_slist_append(headers,
                              "Content-Type: application/x-www-form-urlencoded");
  headers = curl_slist_append(headers, "Accept: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form_data);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, auth_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  if (curl_easy_perform(curl) != CURLE_OK)
    goto cleanup;
  if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code) != CURLE_OK)
    goto cleanup;

  if (http_code_out)
    *http_code_out = http_code;
  if (response_out) {
    *response_out = response.data;
    response.data = NULL;
  }
  ok = true;

cleanup:
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

static bool json_get_string(const char *json, const char *key, char *out,
                            size_t out_size) {
  if (!out || out_size == 0)
    return false;
  out[0] = '\0';
  const char *p = json_find_key_value(json, key);
  if (!p || *p != '"')
    return false;

  p++;
  size_t o = 0;
  while (*p && *p != '"' && o + 1 < out_size) {
    if (*p == '\\') {
      p++;
      if (!*p)
        break;
      switch (*p) {
      case '"':
      case '\\':
      case '/':
        out[o++] = *p;
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
  char access_token[1024];
  char refresh_token[1024];
  uint64_t expires_in = 0;
  if (!json_get_string(response, "access_token", access_token,
                       sizeof(access_token))) {
    return false;
  }
  if (!json_get_string(response, "refresh_token", refresh_token,
                       sizeof(refresh_token))) {
    if (has_text(context.config.psn_oauth_refresh_token)) {
      snprintf(refresh_token, sizeof(refresh_token), "%s",
               context.config.psn_oauth_refresh_token);
    } else {
      refresh_token[0] = '\0';
    }
  }
  if (!json_get_uint64(response, "expires_in", &expires_in) || expires_in == 0) {
    expires_in = 3600;
  }

  set_config_string(&context.config.psn_oauth_access_token, access_token);
  set_config_string(&context.config.psn_oauth_refresh_token, refresh_token);
  context.config.psn_oauth_expires_at_unix = now_unix + expires_in;
  clear_device_flow_fields();
  g_psn_auth.state = PSN_AUTH_STATE_TOKEN_VALID;
  psn_auth_clear_error();
  return true;
}

static bool oauth_configured_for_device_flow(void) {
  return has_text(VITARPS5_PSN_OAUTH_DEVICE_CODE_URL) &&
         has_text(VITARPS5_PSN_OAUTH_TOKEN_URL) &&
         has_text(VITARPS5_PSN_OAUTH_CLIENT_ID);
}

static bool oauth_configured_for_refresh(void) {
  return has_text(VITARPS5_PSN_OAUTH_TOKEN_URL) &&
         has_text(VITARPS5_PSN_OAUTH_CLIENT_ID);
}

PsnAuthState psn_auth_state(uint64_t now_unix) {
  if (!psn_auth_enabled())
    return PSN_AUTH_STATE_DISABLED;

  if (g_psn_auth.state == PSN_AUTH_STATE_DEVICE_LOGIN_PENDING ||
      g_psn_auth.state == PSN_AUTH_STATE_DEVICE_LOGIN_POLLING ||
      g_psn_auth.state == PSN_AUTH_STATE_TOKEN_REFRESHING) {
    return g_psn_auth.state;
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
  if (state == PSN_AUTH_STATE_DEVICE_LOGIN_PENDING ||
      state == PSN_AUTH_STATE_DEVICE_LOGIN_POLLING)
    return "Awaiting device login";
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
    psn_auth_set_error("OAuth device flow not configured in this build");
    return false;
  }

  CURL *curl = curl_easy_init();
  if (!curl) {
    psn_auth_set_error("Failed to initialize OAuth client");
    return false;
  }

  char form[1024];
  size_t off = 0;
  bool form_ok = append_form_kv(curl, form, sizeof(form), &off, "client_id",
                                VITARPS5_PSN_OAUTH_CLIENT_ID) &&
                 append_form_kv(curl, form, sizeof(form), &off, "scope",
                                VITARPS5_PSN_OAUTH_SCOPE);
  curl_easy_cleanup(curl);
  if (!form_ok) {
    psn_auth_set_error("Failed to build device login request");
    return false;
  }

  long http_code = 0;
  char *response = NULL;
  if (!oauth_post_form(VITARPS5_PSN_OAUTH_DEVICE_CODE_URL, form, &http_code,
                       &response)) {
    psn_auth_set_error("Device login request failed");
    return false;
  }

  bool ok = false;
  if (http_code == 200) {
    uint64_t expires_in = 600;
    uint64_t poll_interval = 5;
    if (json_get_string(response, "device_code", g_psn_auth.device_code,
                        sizeof(g_psn_auth.device_code)) &&
        json_get_string(response, "user_code", g_psn_auth.user_code,
                        sizeof(g_psn_auth.user_code))) {
      if (!json_get_string(response, "verification_uri",
                           g_psn_auth.verification_url,
                           sizeof(g_psn_auth.verification_url))) {
        json_get_string(response, "verification_uri_complete",
                        g_psn_auth.verification_url,
                        sizeof(g_psn_auth.verification_url));
      }
      json_get_uint64(response, "expires_in", &expires_in);
      json_get_uint64(response, "interval", &poll_interval);
      g_psn_auth.device_code_expires_at_unix = now_unix + expires_in;
      g_psn_auth.poll_interval_sec = (uint32_t)((poll_interval > 0) ? poll_interval : 5);
      g_psn_auth.next_poll_unix = now_unix + g_psn_auth.poll_interval_sec;
      g_psn_auth.state = PSN_AUTH_STATE_DEVICE_LOGIN_PENDING;
      psn_auth_clear_error();
      ok = true;
    }
  }

  if (!ok) {
    char error_desc[160];
    if (response &&
        json_get_string(response, "error_description", error_desc,
                        sizeof(error_desc))) {
      psn_auth_set_error(error_desc);
    } else {
      psn_auth_set_error("Device login request rejected");
    }
  }

  free(response);
  return ok;
}

bool psn_auth_poll_device_login(uint64_t now_unix) {
  if (!psn_auth_device_login_active())
    return false;
  if (!has_text(g_psn_auth.device_code)) {
    psn_auth_set_error("Missing device code; restart login");
    return false;
  }
  if (now_unix < g_psn_auth.next_poll_unix)
    return false;
  if (now_unix >= g_psn_auth.device_code_expires_at_unix) {
    psn_auth_set_error("Device login code expired");
    return false;
  }

  CURL *curl = curl_easy_init();
  if (!curl) {
    psn_auth_set_error("Failed to initialize OAuth poll client");
    return false;
  }

  g_psn_auth.state = PSN_AUTH_STATE_DEVICE_LOGIN_POLLING;

  char form[1400];
  size_t off = 0;
  bool form_ok =
      append_form_kv(curl, form, sizeof(form), &off, "client_id",
                     VITARPS5_PSN_OAUTH_CLIENT_ID) &&
      append_form_kv(curl, form, sizeof(form), &off, "grant_type",
                     "urn:ietf:params:oauth:grant-type:device_code") &&
      append_form_kv(curl, form, sizeof(form), &off, "device_code",
                     g_psn_auth.device_code) &&
      append_form_kv(curl, form, sizeof(form), &off, "client_secret",
                     VITARPS5_PSN_OAUTH_CLIENT_SECRET);
  curl_easy_cleanup(curl);
  if (!form_ok) {
    psn_auth_set_error("Failed to build device poll request");
    return false;
  }

  long http_code = 0;
  char *response = NULL;
  if (!oauth_post_form(VITARPS5_PSN_OAUTH_TOKEN_URL, form, &http_code,
                       &response)) {
    g_psn_auth.state = PSN_AUTH_STATE_DEVICE_LOGIN_PENDING;
    g_psn_auth.next_poll_unix = now_unix + g_psn_auth.poll_interval_sec;
    return false;
  }

  bool token_updated = false;
  if (http_code == 200) {
    token_updated = apply_token_response(response, now_unix);
    if (!token_updated)
      psn_auth_set_error("Token response missing required fields");
  } else {
    char oauth_error[96];
    oauth_error[0] = '\0';
    if (!json_get_string(response, "error", oauth_error, sizeof(oauth_error)))
      snprintf(oauth_error, sizeof(oauth_error), "http_%ld", http_code);

    if (strcmp(oauth_error, "authorization_pending") == 0) {
      g_psn_auth.state = PSN_AUTH_STATE_DEVICE_LOGIN_PENDING;
      g_psn_auth.next_poll_unix = now_unix + g_psn_auth.poll_interval_sec;
    } else if (strcmp(oauth_error, "slow_down") == 0) {
      g_psn_auth.poll_interval_sec += 5;
      g_psn_auth.state = PSN_AUTH_STATE_DEVICE_LOGIN_PENDING;
      g_psn_auth.next_poll_unix = now_unix + g_psn_auth.poll_interval_sec;
    } else if (strcmp(oauth_error, "access_denied") == 0) {
      psn_auth_set_error("PSN login denied by user");
    } else if (strcmp(oauth_error, "expired_token") == 0) {
      psn_auth_set_error("PSN device login expired");
    } else {
      char error_desc[160];
      if (json_get_string(response, "error_description", error_desc,
                          sizeof(error_desc))) {
        psn_auth_set_error(error_desc);
      } else {
        psn_auth_set_error("PSN device login failed");
      }
    }
  }

  free(response);
  return token_updated;
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
      append_form_kv(curl, form, sizeof(form), &off, "client_id",
                     VITARPS5_PSN_OAUTH_CLIENT_ID) &&
      append_form_kv(curl, form, sizeof(form), &off, "grant_type",
                     "refresh_token") &&
      append_form_kv(curl, form, sizeof(form), &off, "refresh_token",
                     context.config.psn_oauth_refresh_token) &&
      append_form_kv(curl, form, sizeof(form), &off, "client_secret",
                     VITARPS5_PSN_OAUTH_CLIENT_SECRET);
  curl_easy_cleanup(curl);
  if (!form_ok) {
    psn_auth_set_error("Failed to build refresh request");
    return false;
  }

  long http_code = 0;
  char *response = NULL;
  if (!oauth_post_form(VITARPS5_PSN_OAUTH_TOKEN_URL, form, &http_code,
                       &response)) {
    psn_auth_set_error("Token refresh request failed");
    return false;
  }

  bool refreshed = false;
  if (http_code == 200 && apply_token_response(response, now_unix)) {
    refreshed = true;
  } else {
    char error_desc[160];
    if (response &&
        json_get_string(response, "error_description", error_desc,
                        sizeof(error_desc))) {
      psn_auth_set_error(error_desc);
    } else {
      psn_auth_set_error("Token refresh failed");
    }
  }

  free(response);
  return refreshed;
}
