# PSN Remote Play WebSocket 403 Status

## Summary

This document started as the tracking note for the original PSN websocket `403`.
That blocker is now resolved. The current conclusion is different:

- VitaRPS5 now reaches the same PSN websocket/session bootstrap state as `chiaki-ng`
- the remaining failure is at UDP hole punching for the control channel
- the same failure reproduces in full `chiaki-ng` on the same network path
- Sony's official Remote Play app also fails on the same network path

Current likely root cause:

- unsupported network / NAT behavior for UDP hole punching on the tested connection

Practical consequence:

- further parity work is not the highest-value path on this network
- manual remote connection / port forwarding is likely required here
- code investigation should resume only after testing on a different network path where Sony's official app succeeds

## Historical Blocker: Websocket 403

As of `VitakiForkv0.1.602.vpk`, PSN internet remote play on VitaRPS5 was blocked at the
push-notification websocket bootstrap step.

At that time the connection reached:

- PSN OAuth login
- token exchange / refresh
- PSN host refresh
- PS5 device list fetch
- PSN host card creation
- session setup up to websocket open

The first remaining backend failure is:

- `wss://<fqdn>/np/pushNotification`
- Sony replies with `HTTP/1.1 403 Forbidden`
- Sony also sends:
  - `X-PSN-RETRY-INTERVAL-MIN: 120`
  - `X-PSN-RETRY-INTERVAL-MAX: 1200`

That websocket `403` was eventually fixed by bringing Vita auth/token provenance in line with
`chiaki-ng`, including the `duid` parameter in the authorize flow.

## Current Working State

The following are now confirmed working on Vita:

- PSN OAuth browser flow and auth-code exchange
- token refresh
- packaged CA bundle for Sony OAuth / device-list / websocket-FQDN endpoints
- holepunch-enabled testing builds
- PSN device-list refresh
- persisted registered-host seed matching for PSN hosts
- PSN device UID parsing
- websocket FQDN lookup
- websocket open with `101 Switching Protocols`
- session creation and session start
- receipt of `CONSOLE_JOINED` and `CUSTOMDATA1_RECEIVED`
- non-crashing failure handling after control-hole punch failure

Relevant code paths:

- [`vita/src/psn_auth.c`](/Users/mauriciogaldos/Developer/AndeanBear/vitarps5/vita/src/psn_auth.c)
- [`vita/src/psn_remote.c`](/Users/mauriciogaldos/Developer/AndeanBear/vitarps5/vita/src/psn_remote.c)
- [`lib/src/remote/holepunch.c`](/Users/mauriciogaldos/Developer/AndeanBear/vitarps5/lib/src/remote/holepunch.c)

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

- shared PSN client profile: [`lib/src/remote/holepunch.c:91`](/Users/mauriciogaldos/Developer/AndeanBear/vitarps5/lib/src/remote/holepunch.c#L91)
- websocket request + retry-header capture: [`lib/src/remote/holepunch.c:1997`](/Users/mauriciogaldos/Developer/AndeanBear/vitarps5/lib/src/remote/holepunch.c#L1997)
- Vita error surfacing for websocket rejection: [`vita/src/psn_remote.c:252`](/Users/mauriciogaldos/Developer/AndeanBear/vitarps5/vita/src/psn_remote.c#L252)

## What Was Fixed During Investigation

The following were fixed or aligned during this investigation:

1. OAuth / redirect / token exchange parity
- Vita auth flow now works reliably
- auth code extraction was verified against `chiaki-ng`

2. TLS / CA chain problems
- all required Sony endpoints in the current path verify correctly

3. Missing `duid` in authorize URL
- fixed
- this was the key auth-provenance issue behind the original websocket `403`

4. Missing PSN session / offer sequencing parity
- prebuilt OFFER flow
- reqId refresh
- session-check wiring
- local IPv4 candidate on Vita instead of `0.0.0.0`

5. Additional control-path parity work
- enough to show Vita and full `chiaki-ng` now fail in the same later stage on this network

## Hypotheses Ruled Out

These are no longer the leading explanation for the current failure:

1. Broken Vita OAuth extraction
- ruled out

2. Bad Vita token refresh
- ruled out

3. Sony websocket policy rejecting Vita auth outright
- ruled out after `duid` fix

4. Missing websocket headers
- ruled out via raw handshake comparison

5. Vita-only control-path sequencing mismatch
- no longer the best explanation after matching full `chiaki-ng` more closely and reproducing the same timeout there

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

## Recommended Next Action When Returning

When returning to PSN internet remote-play work:

1. Start from a known-good network path
2. Reuse the existing Vita and `chiaki-ng` logging
3. Only reopen websocket/auth investigation if `101` regresses
4. Otherwise focus on NAT traversal only if the official app works on that alternate network

## Validation Baseline

Latest successful Vita testing build during this investigation:

- `./tools/build.sh --env testing`
- artifact: `VitakiForkv0.1.612.vpk`
