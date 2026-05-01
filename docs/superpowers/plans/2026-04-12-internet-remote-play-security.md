# Internet Remote Play — PSN OAuth & Holepunch Security Hardening

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **Enforcement:** per repo CLAUDE.md, all Edit/Write MUST be delegated to the `senior-c-architect` agent; review uses `senior-code-guardian`. The orchestrator coordinates only.

**Goal:** Close #96–#105 — the security/hardening cluster surfaced by PR #95/#106 review, covering PSN OAuth credential handling, holepunch DUID alignment, WebSocket header truncation checks, STUN-list fetch robustness on Vita, and CA-bundle expiry tracking.

**Architecture:** Small, isolated C changes across three layers: `vita/src/psn_auth.c`, `vita/src/psn_remote.c`, `vita/src/config.c` (client), and `lib/src/remote/holepunch.c` + `lib/include/chiaki/remote/holepunch.h` (core). One docs-only change (`assets/psn-ca-bundle.pem`). All runtime changes gated or #ifdef'd to preserve vitaki-compatible baseline; no behavioral change to the happy-path streaming flow.

**Tech Stack:** C (VitaSDK / ANSI C), CMake (`vita/CMakeLists.txt`), libcurl, Chiaki holepunch lib. Build via `./tools/build.sh --env testing`.

**Branch / PR:** `internet-remote-play-polish` → draft PR #108.

---

## File Inventory

| File | Tasks | Responsibility |
|------|-------|----------------|
| `vita/src/psn_auth.c` | 1, 2, 3, 4 | OAuth client creds, transport probe, DUID, token buffer sizing |
| `vita/src/psn_remote.c` | 3, 5, 6 | UPnP error surface, rename call site |
| `vita/src/config.c` | 7 | Gate plaintext token persistence |
| `lib/src/remote/holepunch.c` | 6, 8, 9 | Symbol rename def, header_buf truncation check, Vita STUN-fetch skip |
| `lib/include/chiaki/remote/holepunch.h` | 6, 9 | Public API rename, Vita skip signature |
| `assets/psn-ca-bundle.pem` | 10 | Expiry tracking header comment |
| `vita/CMakeLists.txt` | 1 | `#error` injection / required-var check |

---

## Task Ordering Rationale

Ordered by dependency and blast radius — lowest-risk, isolated fixes first; the symbol rename (Task 6) touches the public header so it comes after the psn_auth-local tasks settle.

1. #96 OAuth client fallback removal (build-system gate)
2. #97 Transport probe gating
3. #98 DUID prefix alignment
4. #101 OAuth token buffer length check
5. #99 UPnP error surface
6. #100 Symbol rename `chiaki_holepunch_session_create_offer`
7. #104 Plaintext token storage compile flag
8. #102 `header_buf` snprintf truncation check
9. #105 Skip STUN-list fetch on Vita
10. #103 CA-bundle expiry tracking

---

## Task 1 — #96 Remove hardcoded PSN OAuth client fallback defaults

**Files:**
- Modify: `vita/src/psn_auth.c:30-34`
- Modify: `vita/CMakeLists.txt:108-113`

- [ ] **Step 1: Delete fallback `#define`s in `psn_auth.c`**

Replace the `VITARPS5_PSN_OAUTH_CLIENT_ID` and `VITARPS5_PSN_OAUTH_CLIENT_SECRET` blocks (currently `vita/src/psn_auth.c:30-34`) with a hard `#error`:

```c
#ifndef VITARPS5_PSN_OAUTH_CLIENT_ID
#error "VITARPS5_PSN_OAUTH_CLIENT_ID must be provided via .env.prod/.env.testing -> CMake"
#endif
#ifndef VITARPS5_PSN_OAUTH_CLIENT_SECRET
#error "VITARPS5_PSN_OAUTH_CLIENT_SECRET must be provided via .env.prod/.env.testing -> CMake"
#endif
```

Leave the other URL/scope/redirect defines untouched — those are not secrets.

- [ ] **Step 2: Harden CMake injection to fail fast**

In `vita/CMakeLists.txt` right before the existing `if(DEFINED VITARPS5_PSN_OAUTH_CLIENT_ID)` at line 108, add:

```cmake
if(NOT DEFINED VITARPS5_PSN_OAUTH_CLIENT_ID OR "${VITARPS5_PSN_OAUTH_CLIENT_ID}" STREQUAL "")
    message(FATAL_ERROR "VITARPS5_PSN_OAUTH_CLIENT_ID is required. Provide via .env.prod or .env.testing.")
endif()
if(NOT DEFINED VITARPS5_PSN_OAUTH_CLIENT_SECRET OR "${VITARPS5_PSN_OAUTH_CLIENT_SECRET}" STREQUAL "")
    message(FATAL_ERROR "VITARPS5_PSN_OAUTH_CLIENT_SECRET is required. Provide via .env.prod or .env.testing.")
endif()
```

- [ ] **Step 3: Verify `.env.testing` already provides both**

Run: `grep -E 'VITARPS5_PSN_OAUTH_CLIENT_(ID|SECRET)' .env.testing .env.prod 2>/dev/null`
Expected: both vars present in each env file present on disk. If missing, add them (values are the well-known reverse-engineered PS-app credentials previously in source — move them into env files only; do not check values into a task note).

- [ ] **Step 4: Build without env profile fails, with env profile succeeds**

Run: `./tools/build.sh` (no `--env`)
Expected: CMake FATAL_ERROR.

Run: `./tools/build.sh --env testing`
Expected: build succeeds.

- [ ] **Step 5: Commit**

```bash
git add vita/src/psn_auth.c vita/CMakeLists.txt .env.testing .env.prod
git commit -m "security: fail build when PSN OAuth creds not provided via env (#96)"
```

---

## Task 2 — #97 Gate `log_oauth_transport_probe` to failure path only

**Files:**
- Modify: `vita/src/psn_auth.c:450` (probe call site inside `oauth_post_form`)

- [ ] **Step 1: Remove the unconditional probe call**

Delete the `log_oauth_transport_probe(url);` call currently at `vita/src/psn_auth.c:450` (the one just before `curl_easy_perform`).

- [ ] **Step 2: Move the probe into the `perform_res != CURLE_OK` branch**

Inside the existing `if (perform_res != CURLE_OK) {` block at `vita/src/psn_auth.c:461`, add the probe call as the first statement:

```c
  if (perform_res != CURLE_OK) {
    log_oauth_transport_probe(url);
    curl_easy_getinfo(curl, CURLINFO_SSL_VERIFYRESULT, &verify_peer);
    ...
```

- [ ] **Step 3: Build and sanity-check logs**

Run: `./tools/build.sh --env testing`
Expected: build success. Manual smoke test (optional, hardware) should show no `PSN auth transport probe` lines on the success path.

- [ ] **Step 4: Commit**

```bash
git add vita/src/psn_auth.c
git commit -m "perf(psn-auth): only run OAuth transport probe on curl failure (#97)"
```

---

## Task 3 — #98 Align PSN client DUID prefix with holepunch

**Files:**
- Modify: `vita/src/psn_auth.c:48-53`
- Verify: `lib/include/chiaki/remote/holepunch.h:42,49,160-165`

- [ ] **Step 1: Switch `psn_auth.c` to use the public holepunch DUID generator**

In `vita/src/psn_auth.c`, add the include near the other `<chiaki/...>` includes (top of file, line ~12):

```c
#include <chiaki/remote/holepunch.h>
```

Delete the `PSN_CLIENT_DUID_PREFIX`, `PSN_CLIENT_DUID_RANDOM_BYTES`, and `PSN_CLIENT_DUID_SIZE` macros at `vita/src/psn_auth.c:51-55`.

- [ ] **Step 2: Replace the hand-rolled DUID generation with `chiaki_holepunch_generate_client_device_uid`**

Find every in-file callsite that currently builds a DUID using `PSN_CLIENT_DUID_PREFIX`. Replace each with:

```c
char duid[CHIAKI_DUID_STR_SIZE];
size_t duid_size = sizeof(duid);
if (chiaki_holepunch_generate_client_device_uid(duid, &duid_size) != CHIAKI_ERR_SUCCESS) {
    LOGE("PSN auth: failed to generate client DUID");
    return false; /* or the file's local failure-return convention */
}
```

Adjust `return` statements to match the containing function's error convention (inspect each site).

- [ ] **Step 3: Build**

Run: `./tools/build.sh --env testing`
Expected: build succeeds, no new warnings about unused macros.

- [ ] **Step 4: Verify DUID format in logs**

Manual: on-device sign-in, confirm DUIDs logged in `ux0:data/vita-chiaki/vita-chiaki.log` start with `00000007000a00c00001028700140000` (64 hex chars + NUL = `CHIAKI_DUID_STR_SIZE` 65).

- [ ] **Step 5: Commit**

```bash
git add vita/src/psn_auth.c
git commit -m "fix(psn-auth): use chiaki_holepunch DUID generator to match Sony format (#98)"
```

---

## Task 4 — #101 Replace fixed 1024-byte OAuth token buffers

**Files:**
- Modify: `vita/src/psn_auth.c:596-625` (`apply_token_response`)

- [ ] **Step 1: Change `apply_token_response` to length-check before accepting**

Replace the stack buffers at `vita/src/psn_auth.c:596-597`:

```c
  char access_token[1024];
  char refresh_token[1024];
```

with length probes + oversized-rejection. New body:

```c
static bool apply_token_response(const char *response, uint64_t now_unix) {
  size_t access_len = 0;
  size_t refresh_len = 0;
  if (!json_get_string_len(response, "access_token", &access_len) || access_len == 0) {
    LOGE("PSN auth: access_token missing from token response");
    return false;
  }
  /* Hard cap to bound allocation on malformed servers. */
  if (access_len >= 8192) {
    LOGE("PSN auth: access_token length %zu exceeds 8192-byte cap", access_len);
    return false;
  }
  char *access_token = malloc(access_len + 1);
  if (!access_token) return false;
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
    if (!refresh_token) { free(access_token); return false; }
    if (!json_get_string(response, "refresh_token", refresh_token, refresh_len + 1)) {
      free(access_token); free(refresh_token);
      return false;
    }
  } else if (has_text(context.config.psn_oauth_refresh_token)) {
    refresh_token = strdup(context.config.psn_oauth_refresh_token);
    if (!refresh_token) { free(access_token); return false; }
  } else {
    refresh_token = strdup("");
    if (!refresh_token) { free(access_token); return false; }
  }

  uint64_t expires_in = 0;
  if (!json_get_uint64(response, "expires_in", &expires_in) || expires_in == 0) {
    expires_in = 3600;
  }

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
```

- [ ] **Step 2: Add `json_get_string_len` helper**

Locate the existing `json_get_string` definition in `vita/src/psn_auth.c` (grep for `static bool json_get_string`). Immediately above it, add:

```c
/* Returns the byte length of the JSON string value for `key` without copying. */
static bool json_get_string_len(const char *response, const char *key, size_t *out_len);
```

Implement it by refactoring `json_get_string` to share the scanner, or by a minimal standalone implementation that finds `"key"\s*:\s*"..."` and reports the unescaped length. Keep it static.

If refactor is too invasive for one task, implement a standalone scanner that mirrors `json_get_string`'s existing quote/escape handling.

- [ ] **Step 3: Build**

Run: `./tools/build.sh --env testing`
Expected: success, no leak warnings from static analysis.

- [ ] **Step 4: Manual verification**

Manual: sign in to PSN, verify token valid after restart (covers both the happy path and the refresh-reuse branch).

- [ ] **Step 5: Commit**

```bash
git add vita/src/psn_auth.c
git commit -m "security(psn-auth): dynamic OAuth token buffers with length cap (#101)"
```

---

## Task 5 — #99 Set `psn_remote_last_error` on UPnP discovery failure

**Files:**
- Modify: `vita/src/psn_remote.c:240-245`

- [ ] **Step 1: Add `psn_remote_set_error` call in the UPnP failure branch**

At `vita/src/psn_remote.c:241-245`, replace:

```c
  ChiakiErrorCode err = chiaki_holepunch_upnp_discover(session);
  if (err != CHIAKI_ERR_SUCCESS) {
    LOGE("PSN remote prepare failed: upnp_discover: %s", chiaki_error_string(err));
    chiaki_holepunch_session_discard(session);
    return 1;
  }
```

with:

```c
  ChiakiErrorCode err = chiaki_holepunch_upnp_discover(session);
  if (err != CHIAKI_ERR_SUCCESS) {
    LOGE("PSN remote prepare failed: upnp_discover: %s", chiaki_error_string(err));
    char msg[160];
    snprintf(msg, sizeof(msg), "UPnP discovery failed: %s", chiaki_error_string(err));
    psn_remote_set_error(msg);
    chiaki_holepunch_session_discard(session);
    return 1;
  }
```

- [ ] **Step 2: Build**

Run: `./tools/build.sh --env testing`
Expected: success.

- [ ] **Step 3: Commit**

```bash
git add vita/src/psn_remote.c
git commit -m "fix(psn-remote): surface UPnP discovery failure to UI (#99)"
```

---

## Task 6 — #100 Rename `holepunch_session_create_offer` to `chiaki_holepunch_session_create_offer`

**Files:**
- Modify: `lib/include/chiaki/remote/holepunch.h:229`
- Modify: `lib/src/remote/holepunch.c:2557` (definition) + all internal log strings that embed the function name (2575, 2589, 2595, 2602, 2613, 2629, 2642, 2649, 2656, 2727, 2737, 2854, 2858, 2871, 2886, 2898, 2908)
- Modify: `vita/src/psn_remote.c:276` (single external caller)
- Verify: `lib/src/remote/stun.h:509` (comment reference only)

- [ ] **Step 1: Rename the declaration**

In `lib/include/chiaki/remote/holepunch.h:229`, replace `holepunch_session_create_offer` with `chiaki_holepunch_session_create_offer`.

- [ ] **Step 2: Rename the definition and all `CHIAKI_LOGx` call-site strings**

In `lib/src/remote/holepunch.c`, use a mechanical substitution: the function name appears in the definition signature (line ~2557) and as a literal inside log format strings. Preserve the embedded name in logs for grep-ability so users searching old logs still match — search for `holepunch_session_create_offer:` and replace with `chiaki_holepunch_session_create_offer:` across the file.

Run: `grep -n "holepunch_session_create_offer" lib/src/remote/holepunch.c`
Expected: every match already has the `chiaki_` prefix.

- [ ] **Step 3: Update the single external caller**

`vita/src/psn_remote.c:276`:

```c
err = chiaki_holepunch_session_create_offer(session);
```

- [ ] **Step 4: Update the stale comment in `lib/src/remote/stun.h:509`**

Replace the reference to `holepunch_session_create_offer` with `chiaki_holepunch_session_create_offer`.

- [ ] **Step 5: Full build + CI format**

Run: `./tools/build.sh --env testing`
Expected: success. No unresolved-symbol errors.

Run: `./tools/build.sh` (prod path to catch any missed caller)
Expected: the Task 1 FATAL_ERROR fires (no env). This is fine — the point is the compile step at least starts. Use `--env testing` to confirm link.

- [ ] **Step 6: Commit**

```bash
git add lib/include/chiaki/remote/holepunch.h lib/src/remote/holepunch.c lib/src/remote/stun.h vita/src/psn_remote.c
git commit -m "api: rename holepunch_session_create_offer -> chiaki_holepunch_session_create_offer (#100)"
```

---

## Task 7 — #104 Gate plaintext OAuth token storage behind compile flag

**Files:**
- Modify: `vita/src/config.c:425-430`
- Modify: `vita/CMakeLists.txt` (new option)

- [ ] **Step 1: Add CMake option**

In `vita/CMakeLists.txt`, near other `option()` calls, add:

```cmake
option(VITARPS5_PLAINTEXT_TOKEN_STORAGE "Persist PSN OAuth tokens in plaintext config. INSECURE — disable for production until token encryption (#81) lands." OFF)
if(VITARPS5_PLAINTEXT_TOKEN_STORAGE)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_PLAINTEXT_TOKEN_STORAGE=1)
endif()
```

- [ ] **Step 2: Gate the fprintfs in `config.c`**

At `vita/src/config.c:425-430`, wrap the two `fprintf`s:

```c
#ifdef VITARPS5_PLAINTEXT_TOKEN_STORAGE
  if (cfg->psn_oauth_access_token) {
    fprintf(fp, "psn_oauth_access_token = \"%s\"\n", cfg->psn_oauth_access_token);
  }
  if (cfg->psn_oauth_refresh_token) {
    fprintf(fp, "psn_oauth_refresh_token = \"%s\"\n", cfg->psn_oauth_refresh_token);
  }
#else
  /* Plaintext token storage is disabled. Enable the VITARPS5_PLAINTEXT_TOKEN_STORAGE
   * CMake option only for local debugging — see #81 for encrypted storage work. */
#endif
```

- [ ] **Step 3: Enable the flag in `.env.testing` only**

In `tools/build.sh` around line ~160 (the `if [ "$ENV_PROFILE_PATH" = ".env.testing" ]` block), add:

```bash
cmake_args+=("-DVITARPS5_PLAINTEXT_TOKEN_STORAGE=ON")
```

This preserves current behavior for the testing profile (so QA token persistence still works) and makes `prod` builds skip the plaintext writes.

- [ ] **Step 4: Build both profiles**

Run: `./tools/build.sh --env testing`
Expected: success. Manual: sign in, kill app, confirm tokens persist across restart.

Run: `./tools/build.sh --env prod` (assuming `.env.prod` exists)
Expected: success. Manual (optional): sign in, kill app, confirm tokens do NOT persist — user is logged out next launch (acceptable — tracked by #81).

- [ ] **Step 5: Commit**

```bash
git add vita/CMakeLists.txt vita/src/config.c tools/build.sh
git commit -m "security(config): gate plaintext PSN token persistence behind compile flag (#104)"
```

---

## Task 8 — #102 Check `snprintf` return in shared `header_buf[128]`

**Files:**
- Modify: `lib/src/remote/holepunch.c:2240-2280` (the 8 `snprintf(header_buf, ...)` sites)

- [ ] **Step 1: Add a macro for the repeated pattern**

Immediately after the `char header_buf[128];` declaration at `lib/src/remote/holepunch.c:2240`, add:

```c
#define APPEND_WS_HEADER(fmt, ...) do { \
    int _n = snprintf(header_buf, sizeof(header_buf), fmt, __VA_ARGS__); \
    if (_n < 0 || (size_t)_n >= sizeof(header_buf)) { \
        CHIAKI_LOGE(session->log, "WebSocket header truncated: " fmt, __VA_ARGS__); \
        err = CHIAKI_ERR_UNKNOWN; \
        goto cleanup_headers; \
    } \
    headers = curl_slist_append(headers, header_buf); \
} while (0)
```

(Adjust the `goto` label to match the enclosing function's existing cleanup label; if no cleanup label exists, factor one in or inline the cleanup.)

- [ ] **Step 2: Replace each of the 8 snprintf+append pairs with `APPEND_WS_HEADER`**

At lines 2254, 2257, 2260, 2263, 2266, 2269, 2272, 2275 — each `snprintf(header_buf, ...)` call is followed by a `curl_slist_append(headers, header_buf)` (confirm during implementation). Replace both lines with a single `APPEND_WS_HEADER(...)`.

- [ ] **Step 3: Build**

Run: `./tools/build.sh --env testing`
Expected: success.

- [ ] **Step 4: Commit**

```bash
git add lib/src/remote/holepunch.c
git commit -m "fix(holepunch): detect WebSocket header truncation in shared buffer (#102)"
```

---

## Task 9 — #105 Skip GitHub STUN-list fetch on Vita (Option A)

**Files:**
- Modify: `lib/src/remote/holepunch.c` — `get_stun_servers` (starts line ~5873) and its IPv6 sibling (starts line ~5958)

- [ ] **Step 1: Early-return in `get_stun_servers` on Vita**

At the top of `get_stun_servers` (after the opening brace at `lib/src/remote/holepunch.c:5874`), add:

```c
#ifdef __PSVITA__
    /* On Vita the bundled CA set is Sony-only; the GitHub fetch always fails with
     * CURLE_SSL_CACERT. Skip it and rely on the hardcoded STUN_SERVERS fallback
     * in stun.h. See issue #105. */
    CHIAKI_LOGI(session->log, "Skipping dynamic STUN list fetch on Vita (using built-in list)");
    return CHIAKI_ERR_NETWORK; /* non-fatal — caller already falls back */
#endif
```

- [ ] **Step 2: Mirror the skip in the IPv6 variant**

Add the same early-return at the top of the IPv6 function (lookup starts at `STUN_HOSTS_URL_IPV6`, body near line ~5960).

- [ ] **Step 3: Build**

Run: `./tools/build.sh --env testing`
Expected: success.

- [ ] **Step 4: Verify log cleanliness**

Manual on-device: start a PSN internet remote-play attempt, tail `ux0:data/vita-chiaki/vita-chiaki.log`, confirm:
  - No `CURL error 60` lines from `get_stun_servers`.
  - One-time INFO line per session: `Skipping dynamic STUN list fetch on Vita`.
  - NAT-type discovery still reports (i.e., session proceeds).

- [ ] **Step 5: Commit**

```bash
git add lib/src/remote/holepunch.c
git commit -m "perf(holepunch): skip dynamic STUN-list fetch on Vita (#105)"
```

---

## Task 10 — #103 Track `assets/psn-ca-bundle.pem` certificate expiries

**Files:**
- Modify: `assets/psn-ca-bundle.pem` (prepend comment block)

- [ ] **Step 1: Extract fingerprints and expiries**

Run: `awk '/-----BEGIN CERTIFICATE-----/,/-----END CERTIFICATE-----/' assets/psn-ca-bundle.pem | openssl storeutl -certs /dev/stdin 2>/dev/null || openssl crl2pkcs7 -nocrl -certfile assets/psn-ca-bundle.pem | openssl pkcs7 -print_certs -noout`

If the above does not enumerate cleanly, fall back to splitting the file and running `openssl x509 -in <cert> -noout -subject -fingerprint -sha256 -enddate` for each.

Expected output: one line per embedded cert with subject CN, SHA-256 fingerprint, and `notAfter`.

- [ ] **Step 2: Prepend a tracking comment to the PEM**

Add a block comment at the top of `assets/psn-ca-bundle.pem` (PEM parsers ignore lines outside `BEGIN/END CERTIFICATE` markers — but to be safe, use `#` style or leave a blank line):

```
# VitaRPS5 PSN CA bundle
# Bundle scope: Sony/PSN API endpoints only. Do NOT add unrelated CAs.
# Refresh BEFORE the earliest notAfter below — target 60 days of margin.
#
# Cert tracking (regenerate with:
#   openssl crl2pkcs7 -nocrl -certfile assets/psn-ca-bundle.pem | \
#     openssl pkcs7 -print_certs -noout):
#
#   <Subject CN 1> — SHA256 <fingerprint> — notAfter <YYYY-MM-DD>
#   <Subject CN 2> — SHA256 <fingerprint> — notAfter <YYYY-MM-DD>
#   ...
#
# Last audited: 2026-04-12 (claude + @mauricio-gg)
```

Fill in the three entries with the actual output from Step 1.

- [ ] **Step 3: Build to confirm PEM still parses**

Run: `./tools/build.sh --env testing`
Expected: success. Curl will still consume the file at runtime; preceding `#` comment lines are tolerated by OpenSSL's PEM parser.

- [ ] **Step 4: Commit**

```bash
git add assets/psn-ca-bundle.pem
git commit -m "docs(assets): record PSN CA bundle fingerprints and expiries (#103)"
```

---

## Final Verification

- [ ] **Full clean build, both profiles**

Run: `./tools/build.sh --env testing`
Run (if `.env.prod` exists locally): `./tools/build.sh --env prod`
Expected: both succeed.

- [ ] **Grep for leftover references**

Run: `grep -rn "PSN_CLIENT_DUID_PREFIX\|holepunch_session_create_offer\b" --include='*.c' --include='*.h' . | grep -v chiaki_holepunch_session_create_offer`
Expected: no matches (or only a deliberate renaming comment).

- [ ] **Hardware smoke test (`./tools/build.sh --env testing` + `deploy <vita_ip>`)**

Manual, on Vita:
  - PSN sign-in completes.
  - Host refresh lists PS5.
  - Internet remote-play session negotiates (STUN discovery works via built-in list).
  - Log sanity: no curl-60, no plaintext tokens written under default testing build (tokens still present — that's the testing flag; prod build would omit).

- [ ] **Push and mark PR ready**

Run: `git push`
Move draft PR #108 out of draft only once hardware smoke passes.
