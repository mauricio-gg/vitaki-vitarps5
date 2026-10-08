# PSN internet play

How VitaRPS5 reaches a console over the internet through Sony's PSN servers. For the rest of the pipeline (discovery, Takion, video, input) see [architecture.md](architecture.md) and [streaming.md](streaming.md).

Internet play **is implemented and works on hardware** (first end-to-end session validated on a Vita; see the umbrella tracker, issue #91). It is on by default in the build (`CHIAKI_ENABLE_VITA_HOLEPUNCH` is `ON`, `CMakeLists.txt:24`). It sits next to LAN play and does not replace it.

## What the user sees

1. **Turn it on.** Settings has a `psn_remoteplay_enabled` toggle (`vita/src/config.c:426`, `vita/src/ui/ui_settings.c:140-148`). It defaults to off (`config.c:40`).
2. **Sign in.** Profile > PlayStation Network > Log in (`vita/src/ui/ui_profile.c`). It first tries `psn_auth_refresh_token_if_needed()`, so a saved token that still refreshes needs nothing more. Otherwise `psn_auth_begin_device_login()` starts the phone login and the PlayStation Network group shows the Phone Login Assist pane (`vita/src/ui/ui_profile_login.c`). The pane has a QR code (encoded by `ui_qr.c`, drawn by `ui_qr_panel.c`, which renders it once per URL into one texture) that opens Sony's sign-in page on a phone; the QR holds the full authorize URL, which stays in memory in `psn_auth`, while the URL line on screen is a short fixed display form (`my.account.sony.com/sso/ca/authorize`). Start, or a tap on the QR, shows or hides the code. Select opens the Vita browser with the full URL. After signing in, the browser lands on a redirect page; the user copies that full URL (or the code in it), presses Confirm, and pastes it into the system keyboard "Paste full redirect URL"; the text is converted by `ui_utf16.c` and submitted through `psn_auth_submit_authorization_response()` (`open_keyboard()`, `ui_profile_login_poll()`, `submit_text()` in `ui_profile_login.c`). Square cancels the login. The pane's three buttons are touch targets for the same actions. Log out (same group) arms on the first press and logs out on a second press within 3 s.
3. **Pick a console.** After login, and at every app start, the client fetches the list of consoles on the account (`psn_remote_refresh_hosts`, `vita/src/psn_remote.c:442`; called from `vita/src/ui.c:506`). Each console that has Remote Play enabled and a registered seed host (next section) appears as a card with an internet badge.
4. **Connect.** Choosing the card (or "Connect via" internet on a console that is also seen on the LAN) runs `host_stream` (`vita/src/host.c`), which takes the PSN path when the host's source is `VITA_HOST_SOURCE_PSN_REMOTE` (`host.c:218`).

Note on naming: the code and older notes call the login a "device flow" (`psn_auth_begin_device_login`, `VITARPS5_PSN_OAUTH_DEVICE_CODE_URL`). It is not Sony's device-code flow. The device-code URL is empty by default, and `psn_auth_poll_device_login` does nothing (`psn_auth.c:1078`). The real flow is a normal OAuth authorization-code grant: build an authorize URL, the user signs in elsewhere, the user pastes the result back (`psn_auth_submit_authorization_response`, `psn_auth.c:1182`).

## Requirements

Internet play needs all of these:

- **PSN login** on the Vita (tokens present and refreshable). Checked in `host_stream`: `host.c:325-343`.
- **A console registered on the LAN first (the "seed host").** Internet play does not register consoles itself. See below.
- **A PSN account id on the Vita**, read from the Vita system registry (`load_psn_id_from_registry`, `vita/src/ui.c:339`) and decoded at connect time (`host.c:390-405`). Without it the connect aborts.
- **The console must have Remote Play enabled** in its settings; devices without it are skipped (`psn_remote.c:181`).
- **A holepunch build.** The default build has it. If it is compiled out, `psn_remote_prepare_connect_host` fails with "stack is unavailable" (`psn_remote.c`, `#else` branch).
- **A network that allows UDP hole punching** (see Known limits).

### The seed-host requirement

The PSN device list from Sony only tells us that a console exists and gives its device id. It does not contain the console's registration keys (`rp_regist_key`, `rp_key`), which the Takion session needs. Those only come from LAN registration with the console's PIN. So:

- When building the PSN host list, each Sony device is matched to a locally registered PS5 by nickname, falling back to any registered PS5 (`find_registered_source_for_device`, `psn_remote.c:77`). A usable seed is a registered host that is a PS5 (`host_target_is_ps5_registered`, `psn_remote.c:72`).
- If no seed exists, the console is **skipped silently** (`PSN_HOST_ADD_RESULT_SKIPPED_NO_REGISTERED_SOURCE`, `psn_remote.c:188-192`). There is no on-screen message yet (issue #85).
- The PSN card is a copy of the seed (`copy_host(host, src, ...)`, `psn_remote.c:~220`) with the Sony device id added. It keeps the seed's `registered_state`.
- At connect time `host_stream` re-checks that registration keys exist (`host_try_hydrate_registered_state_from_config`, `host.c:53`, used at `host.c:234`). If not: "Missing console credentials. Re-pair may be required." The keys are then copied into the connect info (`host.c:413-415`).
- Only PS5 is supported for the device list (`chiaki_holepunch_list_devices(..., CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, ...)`, `psn_remote.c:~470`).

Practical consequence: register the console at home on the LAN once, sign in to PSN, then internet play works from anywhere.

## Connection flow

All of this is driven by `psn_remote_prepare_connect_host` (`psn_remote.c:260`) and then by the normal session thread in `lib/src/session.c`. The Vita side talks to `lib/src/remote/holepunch.c`, which is a port of chiaki-ng's holepunch client.

1. **Token check.** Reject a zero device id, refresh the access token if expired, honour the Sony retry gate (`psn_remote.c:~275-325`).
2. **UPnP.** `chiaki_holepunch_upnp_discover` asks the router for a port mapping when it can (`holepunch.c:942`).
3. **Push WebSocket.** `chiaki_holepunch_session_create` (`holepunch.c:977`) first asks Sony for the push server name (`get_websocket_fqdn`, `holepunch.c:~2040`, URL `holepunch.c:154`, host `mobile-pushcl.np.communication.playstation.net`). It then starts the thread "Chiaki Holepunch WS" (`holepunch.c:990-993`, body `websocket_thread_func`, `holepunch.c:2286`). The thread opens `wss://<fqdn>/np/pushNotification` with protocol `np-pushpacket` (`holepunch.c:2323`), sends keepalive pings (5 s pong timeout, `holepunch.c:78`), and delivers session messages from the console. `session_create` waits for the "session created" and "client joined" notifications.
4. **Session start.** `chiaki_holepunch_session_start` (`holepunch.c:1140`) tells Sony to start Remote Play on the console with the console's device id; the console wakes if needed and answers over the same WebSocket. Before that, `chiaki_holepunch_session_create_offer` builds our connection offer (`holepunch.c:2690`).
5. **Control hole punch.** `chiaki_holepunch_session_punch_hole(... CTRL)` exchanges candidates (local, UPnP, STUN-discovered) with the console and picks one that answers. This is the step that fails on bad NATs. The chosen console address is stored in `context.stream.psn_selected_addr` (`psn_remote.c`, end of the function).
6. **RUDP and session request.** `lib/src/session.c:536-562` wraps the control socket in an RUDP layer (`lib/src/remote/rudp.c`), runs a registration exchange over it with the Sony account id, and sends the normal Takion session request (`session.c:~564`). RUDP init uses `chiaki_rudp_send_recv` (`session.c:1048`).
7. **Data hole punch, then Takion.** A second punch opens the data port (`session.c:662`), the session switches from RUDP to the stream connection (`session.c:729-736`), and from then on it is the same Takion stream as on the LAN. The Vita caps the starting bitrate on this path (`PSN_REMOTE_BITRATE_CAP_KBPS`, `host.c:201-207`).

Auth and device-list calls use `web.np.playstation.com` and `auth.api.sonyentertainmentnetwork.com`. The device-list fetch has a 10 s timeout (`DEVICE_LIST_FETCH_TIMEOUT_SEC`, `holepunch.c:86`).

### Who runs where

| Step | Thread | Code |
|---|---|---|
| Login, startup and idle token refresh, host-list refresh | UI thread (blocking) | `psn_auth.c`, `psn_remote.c`, `ui.c` |
| Connect steps 1-5 (token check/refresh, retry gate, UPnP, `session_create`, `session_start`, control punch) | `VitaConnWorker`, which runs `host_stream` | `vita/src/ui/ui_state.c:179,186-193`, `host.c:346`, `holepunch.c:977,1140` |
| Push WebSocket: pings, console messages | "Chiaki Holepunch WS" | `holepunch.c:990`, `websocket_thread_func` at `:2286` |
| RUDP, session request, data punch, Takion | the normal session thread | `lib/src/session.c:536-736` |

## Tokens: storage and refresh

- Access and refresh tokens, expiry and the client duid live in `chiaki.toml` (`vita/src/config.c`).
- **At rest, tokens are encrypted** with AES-256-GCM. The key is derived from an app salt plus the Vita's hardware OpenPsID (`vita/src/token_crypto.c`, header comment). A blob that fails to decrypt is dropped and the user must log in again; there is no fallback to plaintext (`config.c:250-253`, `312-315`). Old plaintext tokens are migrated on first load (`config.c:298-330`).
- **Exception:** builds made with `--env testing` turn on `VITARPS5_PLAINTEXT_TOKEN_STORAGE` and also write plaintext (`tools/build.sh:~203-206`, `vita/CMakeLists.txt:150`). Never use that for release.
- **Refresh:** `psn_auth_refresh_token_if_needed` (`psn_auth.c:1266`) uses the refresh token. It runs at app start (`ui.c:~491`), once a minute while idle (`ui.c:~599-611`) and before each connect (`host.c:338`, `psn_remote.c`; this one runs on the `VitaConnWorker` thread). The startup and idle refreshes block the UI thread (issue #163).
- All Sony HTTPS calls (OAuth, device list, push server name, WebSocket) verify against a bundled CA file, `assets/psn-ca-bundle.pem`, loaded from `app0:/assets/` (`psn_auth.c:50`, `holepunch.c:107,166`).

## Invariants worth protecting

### 1. The duid must be in the authorize URL, in upstream's 48-character format

Sony ties the OAuth tokens to a client device id, the `duid`. Tokens minted without it are accepted by the OAuth and device-list endpoints but **rejected with HTTP 403 at the push WebSocket** (`/np/pushNotification`), so internet play dies at step 3 above. This was a three-week outage (issue #204).

- `psn_auth_begin_device_login` always appends `duid` to the authorize URL (`psn_auth.c:1049-1050`) and refuses to continue if the duid is empty (`psn_auth.c:1003`).
- The format is `DUID_PREFIX` ("0000000700410080") plus 16 random bytes in hex, 48 characters in total (`lib/include/chiaki/remote/holepunch.h:47,56`, generator `holepunch.c:836`). `is_valid_client_duid` enforces length, prefix and hex (`psn_auth.c:176`); a stored duid in any other shape is regenerated (`ensure_client_duid`, `psn_auth.c:193`).
- History: PR #184 removed `duid` because Sony's sign-in page showed "Something went wrong". The real cause was that the Vita had been generating a 64-character, console-style id. Removing `duid` fixed login and broke the WebSocket. PR #205 restored it in the 48-character format.
- Migration: configs with `psn_auth_provenance` below 1 have their tokens cleared once, so users sign in again through the corrected URL (`config.c:440-455`, `PSN_AUTH_PROVENANCE_CURRENT` in `vita/include/config.h:21`).
- The client duid is **not** the console's `deviceUniqueId`. The console id is the 64-hex id from the device list (`holepunch.c:~691`, stored as `psn_device_uid`). The two have different sizes and must never be mixed.
- If you change anything about the authorize URL or the duid, test both: the sign-in page must load, and the WebSocket must return 101, not 403.

### 2. The OAuth client id and secret are committed on purpose

`VITARPS5_PSN_OAUTH_CLIENT_ID` and `VITARPS5_PSN_OAUTH_CLIENT_SECRET` are in `.env.prod` and `.env.testing`. They are the publicly known credentials of Sony's own PlayStation app, found by the community (grill2010) and used by every Chiaki-derived project. They identify the *client application*, not a user, and grant no access to any account. Rotating or hiding them does not improve security and only breaks the build.

- Injection: `tools/build.sh:135-140` passes them to CMake; `vita/CMakeLists.txt:120-127` fails the build if they are missing; `psn_auth.c:31-36` is an `#error` without them. A user can override them from config keys (`psn_oauth_client_id` / `_secret`, `config.c:35`), but the build defaults are what ships.
- `.gitleaks.toml` allowlists them so secret scanning does not flag the files.
- If Sony ever rotates them, copy the new values from [chiaki-ng](https://github.com/streetpea/chiaki-ng) into both env files.
- Tokens and authorization codes are different: they are user secrets and are redacted in logs (PR #106).

### 3. The CA bundle must hold every certificate in Sony's chains, including intermediates

OpenSSL on the Vita does not fetch missing intermediates. In August 2026 Sony's push servers (`*-pushcl.np.communication.playstation.net`) stopped sending the intermediate "COMODO RSA Domain Validation Secure Server CA". The bundle held only the root, so every internet connect failed with CURL error 60 at the WebSocket. PR #268 added the intermediate (`assets/psn-ca-bundle.pem`, 7 certificates, listed in its header with fingerprints and expiry dates).

- The bundle covers Sony endpoints only. Do not add unrelated CAs (issue #105).
- Earliest expiry is the AAA root on 2028-12-31; the header says to refresh before 2028-11-01.
- It ships inside the VPK (`vita/CMakeLists.txt:266`). The new intermediate was verified with `openssl verify`; on-device confirmation from an off-LAN session was still listed as pending in PR #268.
- The fix is a patch, not a design: issue #269 asks to pre-bundle all intermediates so the next chain change cannot break TLS again.

### 4. Respect Sony's retry interval

After a 403 at the WebSocket, Sony sends `X-PSN-RETRY-INTERVAL-MIN/MAX`. The client records them (`holepunch.c:2400-2407`), arms an in-memory cooldown (`s_ws_retry_not_before_unix`, `psn_remote.c:53,352-370`, capped at 3600 s) and refuses new attempts with "PSN cooldown active" until it expires (`psn_remote.c:314-322`). A successful session create clears it (`psn_remote.c:393`), and a fresh login resets it (`psn_auth.c:~1238`). Do not retry in a loop; that only extends the ban.

### 5. Name resolution on the Vita goes through its own resolver

libcurl's `getaddrinfo` path is broken on Vita, so Sony hosts and STUN hosts are resolved by `vita/src/vita_dns.c` and passed to curl as fixed addresses (`vita_curl_add_resolve`, used at `holepunch.c:2065`). New network code that talks to PSN must do the same.

## Known limits and open issues

- **NAT.** Hole punching can fail on networks that do not allow it (phone hotspots are a known case): the log shows `Failed to find reachable candidate for control connection` and the user sees "Failed to establish PSN control channel" (`psn_remote.c`, punch ctrl step). Sony's own app fails the same way there. UPnP helps on home routers. Failure classification and hints are issue #89.
- **No message when a console has no seed host**; it just does not appear (issue #85).
- **PS5 only** in the device list; the PS4 list has a parity bug (issue #93).
- **Account id mismatch.** Registration can fail with PS5 error CE-110032-7 when the Vita's stored PSN id is stale or signed out (issue #270). Manual account id entry is requested in #201.
- **UI thread blocking.** Token refresh and the device-list fetch run synchronously on the UI thread (issue #163); the host list is not refreshed while idle (issue #162).
- **Dead-link detection** on the push WebSocket can take up to about 10 s (issue #164).
- **Display:** the Connection card shows the console name, not the IP, for PSN hosts (issue #94).
- **TLS fragility** until all intermediates are pre-bundled (issue #269).
- Umbrella tracker: issue #91.

## Validating off the LAN

1. Build with `./tools/build.sh --env testing` (verbose logging, tokens stored in plaintext).
2. At home on the LAN: register the console, enable the PSN toggle, sign in.
3. Leave the home network (a second Wi-Fi, not a hotspot that blocks UDP, is the best first test). Open the app and check the console card shows the internet badge.
4. Connect. Healthy log lines: WebSocket `101`, `CONSOLE_JOINED`, "Punched hole for data connection", then the stream starts. A 403 points at invariant 1; CURL error 60 at invariant 3; "Select timed out" candidate failures at the network.
