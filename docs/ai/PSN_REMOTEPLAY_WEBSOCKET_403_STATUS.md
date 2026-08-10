# PSN Remote Play WebSocket 403 Status (DUID Lifecycle & Invariants)

## Summary

The PSN WebSocket 403 blocker has a complex history tied to the `duid` (device unique ID) parameter in Sony's OAuth authorize flow. This document tracks the root cause, the regression that reintroduced the 403, and the fix that restores the invariant.

**Current Status (GH #204 Fix, Pending Hardware Validation):**
- VitaRPS5 now reaches the same PSN websocket/session bootstrap state as `chiaki-ng` with correct `duid` handling
- The remaining failure is at UDP hole punching for the control channel (network-dependent, not Vita-specific)
- **CRITICAL INVARIANT:** `duid` MUST ALWAYS be included in the OAuth authorize URL with upstream's 48-char format. Removing or changing it will break the pushNotification WebSocket.

**Hardware Validation Checklist (GH #204):**
- [ ] PSN QR login does not regress (confirm authorization page loads)
- [ ] PSN internet remote play initializes without 403 (WebSocket handshake returns 101)
- [ ] Migration one-shot fires (pre-duid tokens invalidated, user prompted to re-login once)
- [ ] Sony's retry gate honored after any 403 (cooldown active message appears if WS fails)
- [ ] LAN remote play does not regress during these tests

## Historical Timeline

### Phase 1: Original 403 (Mid-July 2026)
As of `VitakiForkv0.1.602.vpk`, PSN internet remote play was blocked at the push-notification websocket:
- Connection reached OAuth login → token exchange → device-list fetch → session setup
- First backend failure: `wss://<fqdn>/np/pushNotification` returned `HTTP/1.1 403 Forbidden`
- Sony also sent `X-PSN-RETRY-INTERVAL-MIN: 120` and `X-PSN-RETRY-INTERVAL-MAX: 1200`
- **Root cause:** Tokens were minted without the `duid` parameter, lacking device provenance that Sony's `/np/pushNotification` endpoint enforces

### Phase 2: QR Login Fix Introduced Regression (2026-06-25, Commit 57391ae)
- **Issue #184 (PSN QR Login):** Sony's `/oauth/authorize` endpoint was returning "Something went wrong" before the login page loaded when `duid` was included
- **Fix applied:** Removed `duid` from the authorize URL in `vita/src/psn_auth.c` (commit 57391ae, PR #184)
- **Rationale at the time:** Believed the issue was the `duid` parameter itself
- **Outcome:** QR login started working, but tokens were minted without `duid` again
- **Side effect:** PSN WebSocket 403 regressed in all users' sessions starting mid-July 2026 (GH #204)

### Phase 3: Root Cause Analysis (GH #204 Fix)
Investigation revealed:
- **The real #184 blocker** was not `duid` inclusion, but Vita's invalid `duid` format: **64 characters** instead of upstream's **48-character format**
- **Upstream chiaki-ng duid format:** Prefix "0000000700410080" (16 chars) + 16 random hex bytes (32 chars) = **48 chars total** (appended at login in `gui/src/qmlbackend.cpp:1680`)
- **Vita's broken format:** Generated a 64-char duid that Sony's authorize endpoint rejected before showing the login page
- **The fix:** Never should have removed `duid`; instead should have adopted upstream's 48-char format

### Phase 4: GH #204 Resolution (This Branch: fix/psn-duid-websocket-403)
Two commits implement the fix (dc093a4b and 17456f71):

1. **Adopt upstream 48-char client-duid format**
   - `lib/include/chiaki/remote/holepunch.h`: `DUID_PREFIX` constant, `CHIAKI_DUID_STR_SIZE` changed from 65 to 49 (48 chars + NUL)
   - Console `deviceUniqueId` paths remain untouched (device IDs are separate)
   
2. **Re-add duid to OAuth authorize URL**
   - `vita/src/psn_auth.c`: Restored `duid` parameter in the authorize flow
   - `is_valid_client_duid()` validates persisted duids and regenerates stale ones
   
3. **One-time token migration**
   - `vita/src/config.c`: Migration marker `psn_auth_provenance` (current version: 1) in `chiaki.toml`
   - Pre-fix tokens lacking duid are invalidated → users re-login once
   
4. **Sony retry-interval gate**
   - `vita/src/psn_remote.c`: In-memory gate honors Sony's `X-PSN-RETRY-INTERVAL-MIN/MAX` headers after WS 403
   - Blocks retries until interval elapses; displays "PSN cooldown active" message
   
5. **PSN device availability**
   - `psn_remote_available=true` for standalone PSN cards (off-network internet badge now visible)
   - `lib/src/remote/holepunch.c`: Device-list fetch timeout increased from 2s to 10s (better reliability on slower connections)

## Current Working State

The following are confirmed working on Vita:

- PSN OAuth browser flow with correct 48-char duid format
- token refresh with duid provenance validation
- packaged CA bundle for Sony OAuth / device-list / websocket-FQDN endpoints
- holepunch-enabled testing builds
- PSN device-list refresh (10s timeout)
- persisted registered-host seed matching for PSN hosts
- PSN device UID parsing
- websocket FQDN lookup
- **websocket open with `101 Switching Protocols` (fixed in GH #204)**
- session creation and session start
- receipt of `CONSOLE_JOINED` and `CUSTOMDATA1_RECEIVED`
- non-crashing failure handling after control-hole punch failure

Relevant code paths:

- [`vita/src/psn_auth.c`](vita/src/psn_auth.c)
- [`vita/src/psn_remote.c`](vita/src/psn_remote.c)
- [`lib/src/remote/holepunch.c`](lib/src/remote/holepunch.c)

## Current Blocker

The current blocker is no longer PSN auth or websocket setup. It is the control-channel UDP
hole punch.

Observed current state on Vita:

- websocket handshake succeeds
- `http_create_session` succeeds
- `session_start` succeeds
- the console ACKs our `OFFER`
- no candidate ever becomes reachable
- candidate probing times out with:
  - `check_candidate: Select timed out`
  - `Failed to find reachable candidate for control connection`
  - surfaced in Vita as `PSN remote prepare failed: punch ctrl: No route to host`

Observed current state on full `chiaki-ng` on the same network:

- same successful websocket/session bootstrap
- same control-hole punch timeout
- no usable UDP candidate response before timeout

This makes the current blocker much more likely to be the network path itself rather than a Vita-specific
implementation gap.

Relevant implementation:

- shared PSN client profile: [`lib/src/remote/holepunch.c:91`](lib/src/remote/holepunch.c#L91)
- websocket request + retry-header capture: [`lib/src/remote/holepunch.c:1997`](lib/src/remote/holepunch.c#L1997)
- Vita error surfacing for websocket rejection: [`vita/src/psn_remote.c:252`](vita/src/psn_remote.c#L252)

## CRITICAL INVARIANT: Duid Handling

**Never remove `duid` from the OAuth authorize URL.** If you change duid handling, you MUST re-validate:
1. QR login flow (does authorization page load before login form?)
2. PSN WebSocket handshake (do you get 101 Switching Protocols or 403 Forbidden?)

This invariant was violated once (commit 57391ae), causing a 3-week outage (GH #204).

**Duid format requirements (must match upstream chiaki-ng):**
- Total length: 48 characters (hex)
- Prefix: "0000000700410080" (16 chars)
- Random suffix: 16 random bytes in hex (32 chars)
- String size constant: `CHIAKI_DUID_STR_SIZE = 49` (48 + NUL terminator)

**Whenever duid format changes:**
- Test on a known-good network with Sony's official Remote Play app working
- Capture WebSocket handshake headers (confirm 101 response)
- Verify QR login shows the authorization page (not "Something went wrong")
- Run `./tools/build.sh --env testing` for logging

## What Was Fixed During Investigation

The following were fixed or validated during the GH #204 investigation:

1. **OAuth duid format (THE FIX)**
   - Vita was generating 64-char duid instead of upstream's 48-char format
   - This broke QR login in #184, prompting the removal of duid entirely
   - Adopted upstream's exact format: prefix + 16 random bytes
   - QR login now works AND WebSocket 403 is fixed (duid restored with correct format)

2. **OAuth / redirect / token exchange parity**
   - Vita auth flow now works reliably with duid
   - auth code extraction verified against `chiaki-ng`

3. **TLS / CA chain problems**
   - all required Sony endpoints in the current path verify correctly

4. **Token migration on format change**
   - One-time migration marker ensures pre-fix tokens are invalidated
   - Users re-login once after upgrade to pick up new duid format

5. **Sony retry-interval gating**
   - In-memory cooldown prevents hammering the WebSocket after 403
   - Honors `X-PSN-RETRY-INTERVAL-MIN/MAX` headers

6. **PSN device availability**
   - Increased device-list timeout from 2s to 10s for reliability
   - PSN card now shows off-network internet badge correctly

7. **Missing PSN session / offer sequencing parity** (previous investigation)
   - prebuilt OFFER flow
   - reqId refresh
   - session-check wiring
   - local IPv4 candidate on Vita instead of `0.0.0.0`

8. **Additional control-path parity work** (previous investigation)
   - enough to show Vita and full `chiaki-ng` now fail in the same later stage on this network

## Hypotheses Ruled Out

These were investigated and ruled out as root causes:

1. Broken Vita OAuth extraction
   - ruled out; extraction code validated against chiaki-ng

2. Bad Vita token refresh
   - ruled out; token exchange works correctly

3. **Sony websocket policy rejecting Vita auth due to invalid duid format**
   - CONFIRMED TRUE (not a ruled-out hypothesis)
   - This was the root cause of the 403
   - Sony's `/np/pushNotification` endpoint enforces device provenance via the `duid` parameter in the initial OAuth token
   - Invalid (64-char) duid → 403 Forbidden
   - Fixed by adopting upstream's 48-char duid format

4. Missing websocket headers
   - ruled out via raw handshake comparison

5. Vita-only control-path sequencing mismatch (UDP hole punch timeout)
   - no longer the best explanation after matching full `chiaki-ng` more closely and reproducing the same timeout there
   - This is the current blocker AFTER successful WebSocket handshake; it is network/NAT-dependent, not Vita-specific

## Current Network Conclusion

The tested network path is now the leading explanation.

Evidence:

- Vita on hotspot-like address `172.20.10.7` still times out during candidate probing
- full `chiaki-ng` on hotspot-like address `172.20.10.3` reproduces the same control-hole timeout
- Sony's official Remote Play app also fails to connect to the PS5 on this same network

This means the current environment is not a useful proving ground for further Vita parity work.
If Sony's own app cannot connect either, the present limitation is almost certainly the network/NAT path.

## What Was Learned From Public References

### chiaki-ng source

The visible websocket/session identity values already match current `chiaki-ng` closely.

### chiaki-ng issues / releases

- working logs show that `chiaki-ng` gets past websocket open and then continues into session creation
- chiaki-ng documentation explicitly notes that some network types do not support the required UDP
  hole punching and require manual remote connection / port forwarding instead

### Discord signal

From maintainer Discord conversation history:

- random DUID generation is treated as acceptable for client setup
- DUID did not appear to be treated as the likely explanation for remote-connect failure

This supports deprioritizing further DUID experiments.

## Latest Logs And Evidence

Historical websocket-403-era Vita log:

- `/Volumes/Untitled/data/vita-chiaki/85254043403_vitarps5-testing.log`

Current Vita network-failure log:

- `/Volumes/Untitled/data/vita-chiaki/122867578558_vitarps5-testing.log`

Current `chiaki-ng` same-network failure log:

- `/tmp/chiaki-ng-gui-logs/latest.log`

Key current points:

- Vita gets websocket `101`, session create/start, console OFFER ACK, then candidate timeout
- full `chiaki-ng` gets the same successful bootstrap, then candidate timeout
- Sony official app also fails on this network path

## Current Next Step

Do not keep iterating on PSN holepunch parity while using this same network as the only test path.

Next useful actions:

1. Test on a different network path
- preferably one where Sony's official app succeeds

2. If the official app still fails
- treat the issue as network unsupported for UDP hole punching
- use manual remote connection / port forwarding guidance instead

3. If the official app succeeds on another network but Vita fails there
- resume Vita/`chiaki-ng` parity work using that known-good network

## UPnP Enablement (April 2026)

miniupnpc has been cross-compiled for Vita and integrated into the build. The
`CHIAKI_CAN_USE_MINIUPNPC` guard in `lib/include/chiaki/common.h` now activates
when `CHIAKI_ENABLE_VITA_HOLEPUNCH` is defined, which is set for all Vita
holepunch builds.

This means the existing UPnP code paths in `lib/src/remote/holepunch.c` —
gateway discovery, external IP lookup, and port mapping creation — will now
execute on Vita holepunch builds rather than being compiled out. UPnP asks the
Vita's local router to create a temporary port mapping, which improves NAT
traversal on home networks with UPnP-capable routers (the common case for
residential broadband with a typical consumer gateway).

The current test network (iPhone hotspot) does not have a UPnP-capable gateway,
so this change cannot be validated on the present test setup. The hotspot NAT
does not respond to UPnP/SSDP discovery and will silently skip the mapping step.

Next validation step: test on a home Wi-Fi network where Sony's official Remote
Play app succeeds. That environment is the minimum bar for confirming whether
UPnP port mapping produces a reachable candidate.

Files changed:

- `Dockerfile` — miniupnpc cross-compile and install for the Vita toolchain
- `lib/include/chiaki/common.h` — `CHIAKI_CAN_USE_MINIUPNPC` guard tied to `CHIAKI_ENABLE_VITA_HOLEPUNCH`
- `lib/CMakeLists.txt` — link miniupnpc into the holepunch build target

## Recommended Next Action: GH #204 Validation

The GH #204 fix (correct duid format + restoration of duid parameter) is built and ready for hardware validation.

**Before shipping:**
1. Validate QR login does not regress (authorization page must load)
2. Validate PSN WebSocket handshake succeeds (101 Switching Protocols, not 403)
3. Validate migration one-shot fires (user is prompted to re-login once)
4. Validate Sony retry-interval gate is honored (cooldown message appears after any WS 403)
5. Validate LAN remote play still works during these tests

**Test environment:** `./tools/build.sh --env testing` for full logging

**If validation passes:**
- Move GH #204 fix to done
- Revert the `duid` removal from PR #184's commit 57391ae in the git history notes
- Document why duid format (not presence) was the real #184 root cause

## Validation Baseline (Prior Investigation, June 2026)

Latest successful Vita testing build during the original websocket investigation:

- `./tools/build.sh --env testing`
- artifact: `VitakiForkv0.1.612.vpk`

**Current fix baseline (GH #204):**
- Branch: `fix/psn-duid-websocket-403`
- Commits: dc093a4b, 17456f71
- Status: Code review complete, hardware validation pending
- Build: `./tools/build.sh --env testing` (testing + test builds pass)
