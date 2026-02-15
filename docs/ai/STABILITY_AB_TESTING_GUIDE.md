# Stability A/B Testing Guide (Vitaki vs VitaRPS5)

Last Updated: 2026-02-15
Owner: Streaming/Latency track

## Purpose
Provide a repeatable process to compare VitaRPS5 streaming stability against Vitaki baseline and explain why one run may look grainy/choppy while another looks clean and stable.

## Critical Distinction: Build Commands
- `./tools/build.sh --env testing` builds a testing-profile VPK and should be used for on-device streaming validation.
- `./tools/build.sh` builds a production-profile VPK.
- `./tools/build.sh debug` builds a debug VPK.
- `./tools/build.sh test` does not build a VPK; it only compiles `vitarps5_tests`.

If a "new" test appears to change behavior after running `test`, verify that a different VPK was actually installed.

## Stable Recovery Controls
In `ux0:data/vita-chiaki/chiaki.toml`:

```toml
[settings]
latency_mode = "balanced"
```

Behavior:
- Startup bitrate follows the preset profile path (`preset_default` log marker).
- Recovery path is fixed to the stable default (no runtime `stability_profile` switch).

## A/B Procedure
1. Build/install comparison candidate:
- For reproducible logging: `./tools/build.sh --env testing`
- Install generated VPK to Vita.

2. Run identical test scenario for both apps:
- Same console, same network, same game/scene, similar duration (at least 5 minutes).
- Avoid changing router or Wi-Fi position between runs.

3. Pull log file:
- `ux0:data/vita-chiaki/vitarps5-testing.log` (testing profile)
- Rename with timestamp after each run.

4. Verify profile markers:
- `Config loaded: latency_mode=...`
- `Bitrate policy: ...`
- `Recovery profile: ...`
- `Video gap profile: ...`

5. Compare health indicators:
- `PIPE/FPS`: incoming/display FPS behavior over time.
- `AV diag`: `missing_ref`, `corrupt_bursts`, `fec_fail`, `sendbuf_overflow`.
- Disconnect/restart events: `PIPE/RECOVER`, `request_stream_stop`, reconnect loops.

## Pass/Fail Heuristics
Stable/good run (expected from stable default):
- Markers:
  - `Bitrate policy: preset_default (...)`
  - `Recovery profile: stable_default`
  - `Video gap profile: stable_default (hold_ms=24 force_span=12)`
- `PIPE/FPS` stays around 30 target with no sustained low-FPS windows.
- `AV diag` repeatedly shows zero for `missing_ref`, `corrupt_bursts`, `fec_fail`.
- No repeated disconnects/reconnects.

Degraded run signature:
- Rising `missing_ref` and/or `corrupt_bursts` across windows.
- Frequent low `PIPE/FPS` windows (<24 for long periods).
- Recovery escalation loops or stream stops.

## Known Good Reference
`31595612449_vitarps5-testing.log` showed a good baseline-compatible run:
- `Bitrate policy: preset_default (6000 kbps @ 960x540)`
- `Recovery profile: stable_default`
- `Video gap profile: stable_default (hold_ms=24 force_span=12)`
- Repeated `AV diag` windows with `missing_ref=0, corrupt_bursts=0, fec_fail=0`.

## Interpretation Guidance
- If visuals are grainy/choppy but connection does not drop, prioritize packet/dependency diagnostics (`missing_ref`, `corrupt_bursts`) before decode-speed assumptions.
- Recovery behavior is fixed to the stable default path; focus A/B on network conditions/build profile instead of config toggles.
- If both branches look identical despite config edits, confirm log file path and active build profile first.
