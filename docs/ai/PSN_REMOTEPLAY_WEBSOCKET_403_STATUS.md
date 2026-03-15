# PSN Remote Play WebSocket 403 Status

## Summary

As of `VitakiForkv0.1.602.vpk`, PSN internet remote play on VitaRPS5 is blocked at the
push-notification websocket bootstrap step.

The connection reaches:

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

## Current Working State

The following are confirmed working on Vita:

- PSN OAuth browser flow and auth-code exchange
- token refresh
- packaged CA bundle for Sony OAuth / device-list / websocket-FQDN endpoints
- holepunch-enabled testing builds
- PSN device-list refresh
- persisted registered-host seed matching for PSN hosts
- PSN device UID parsing
- non-crashing failure handling after Sony websocket rejection

Relevant code paths:

- [`vita/src/psn_auth.c`](/Users/mauriciogaldos/Developer/AndeanBear/vitarps5/vita/src/psn_auth.c)
- [`vita/src/psn_remote.c`](/Users/mauriciogaldos/Developer/AndeanBear/vitarps5/vita/src/psn_remote.c)
- [`lib/src/remote/holepunch.c`](/Users/mauriciogaldos/Developer/AndeanBear/vitarps5/lib/src/remote/holepunch.c)

## Current Blocker

The websocket handshake reaches Sony and is rejected before `http_create_session` continues.

Observed request shape on wire:

- `Authorization: Bearer <token>`
- `Sec-WebSocket-Protocol: np-pushpacket`
- `User-Agent: WebSocket++/0.8.2`
- `X-PSN-APP-TYPE: REMOTE_PLAY`
- `X-PSN-APP-VER: RemotePlay/1.0`
- `X-PSN-KEEP-ALIVE-STATUS-TYPE: 3`
- `X-PSN-OS-VER: Windows/10.0`
- `X-PSN-PROTOCOL-VERSION: 2.1`
- `X-PSN-RECONNECTION: false`

Relevant implementation:

- shared PSN client profile: [`lib/src/remote/holepunch.c:91`](/Users/mauriciogaldos/Developer/AndeanBear/vitarps5/lib/src/remote/holepunch.c#L91)
- websocket request + retry-header capture: [`lib/src/remote/holepunch.c:1997`](/Users/mauriciogaldos/Developer/AndeanBear/vitarps5/lib/src/remote/holepunch.c#L1997)
- Vita error surfacing for websocket rejection: [`vita/src/psn_remote.c:252`](/Users/mauriciogaldos/Developer/AndeanBear/vitarps5/vita/src/psn_remote.c#L252)

## Hypotheses Already Tested

These were all tested with fresh auth where required and did **not** fix the websocket `403`:

1. Wrong OAuth / token / redirect tuple
- fixed earlier
- auth now works reliably

2. TLS / CA chain problems
- fixed earlier
- all required Sony endpoints in the current path verify correctly

3. Missing seed host / bad PSN host refresh wiring
- fixed earlier
- PS5 now appears from PSN host refresh

4. Broken PSN device UID parsing
- fixed earlier
- PSN host now reaches connect with `uid_zero=0`

5. Client DUID mismatch
- tested long-form random DUID
- tested short-form random DUID
- tested fixed literal short-form DUID
- all still result in the same websocket `403`

Conclusion:

- the remaining blocker is **not** explained by the DUID experiments already tried
- the remaining gap is likely a non-obvious runtime behavior difference from a working client, or a Sony-side policy/eligibility nuance

## What Was Learned From Public References

### chiaki-ng source

The visible websocket/session identity values already match current `chiaki-ng` closely.

### chiaki-ng issues / releases

- working logs show that `chiaki-ng` gets past websocket open and then continues into session creation
- public materials do not explain the exact reason for VitaRPS5's websocket `403`

### Discord signal

From maintainer Discord conversation history:

- random DUID generation is treated as acceptable for client setup
- DUID did not appear to be treated as the likely explanation for remote-connect failure

This supports deprioritizing further DUID experiments.

## Latest Logs

Most recent confirming log:

- `/Volumes/Untitled/data/vita-chiaki/85254043403_vitarps5-testing.log`

Key points from that log:

- fixed literal experimental client DUID was used
- websocket still returned `403 Forbidden`
- retry interval remained `120-1200`

## Current Next Step

Direct maintainer outreach is now justified.

Draft message already prepared and sent to the `chiaki-ng` maintainer asking whether there is:

- any non-obvious websocket/session bootstrap behavior in `chiaki-ng`
- any token provenance nuance
- any runtime quirk not visible in static headers/payloads

## Recommended Next Action When Returning

When a maintainer response arrives:

1. Compare the response against:
   - [`lib/src/remote/holepunch.c`](/Users/mauriciogaldos/Developer/AndeanBear/vitarps5/lib/src/remote/holepunch.c)
   - [`vita/src/psn_auth.c`](/Users/mauriciogaldos/Developer/AndeanBear/vitarps5/vita/src/psn_auth.c)
   - [`vita/src/psn_remote.c`](/Users/mauriciogaldos/Developer/AndeanBear/vitarps5/vita/src/psn_remote.c)
2. Revert temporary DUID experiment code in [`vita/src/psn_auth.c`](/Users/mauriciogaldos/Developer/AndeanBear/vitarps5/vita/src/psn_auth.c) unless the response explicitly confirms a DUID requirement.
3. Keep websocket retry-header logging until the `403` is resolved.

## Validation Baseline

Latest successful build before waiting for maintainer guidance:

- `./tools/build.sh --env testing`
- artifact: `VitakiForkv0.1.602.vpk`
