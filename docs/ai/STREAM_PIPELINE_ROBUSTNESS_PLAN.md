# Stream Pipeline Robustness Plan

Last Updated: 2026-02-15
Owner: Streaming/Latency track

## Why This Exists
Recent testing shows a change in failure mode: fewer hard disconnects, but persistent low-FPS degraded sessions after reconnect/re-entry. We need architecture-level robustness, not threshold-only tuning.

## Key Evidence
1. `86888155925_vitarps5-testing.log`
- Startup still sees severe burst loss and overflow pressure (`:1419`, `:1658`).
- Mid-session avoids quit loops but keeps accumulating Takion drops (up to total 289 at `:75011334`).
- Frequent low throughput windows despite low base RTT (`:61830845`, `:74090478`, `:75090500`).

2. `87116066464_vitarps5-testing.log`
- User stop/re-entry sequence is explicit:
  - stop requested: `:2920`
  - quit: `:2934`
  - reconnect attempt: `:3043`
  - `RP_IN_USE`: `:3082`
  - later successful re-entry: `:3357`
- After second successful entry, FPS repeatedly falls to 19-24 (`:3918`, `:4019`, `:4483`, `:5169`, `:5361`).
- In this degraded period, missing/corrupt reference events climb continuously (`:3575` onward AV diag growth), indicating dependency-chain instability rather than simple queue overflow.

## Working Hypothesis
1. Startup burst transport pressure can still poison early frame dependency state.
2. Re-entry path can land in a degraded decode-dependency regime that remains "alive" but low-FPS.
3. Current recovery gates suppress harmful restarts, but they do not fully restore cadence once this degraded regime starts.

## Recent Mitigation Update (2026-02-12)
- Landed a startup warmup absorb window (1.2s) that:
  - suppresses aggressive overflow escalation during initial ramp,
  - triggers a one-shot Takion reorder-queue drain + IDR request when early overflow pressure crosses a threshold.
- Increased Takion reorder queue depth from 128 to 256 packets for startup burst headroom.
- Removed cross-session static state in video first-frame logging so reconnect diagnostics are per-session.
- Implementation paths:
  - `vita/src/host.c`
  - `vita/src/video.c`
  - `vita/include/context.h`
  - `lib/src/takion.c`

## Stable Recovery Path (2026-02-15)
- Collapsed runtime recovery strategy to a single stable default path (removed `stability_profile` toggle).
- Stable default keeps Vitaki-like startup bitrate policy (preset bitrate from `chiaki_connect_video_profile_preset`) and avoids aggressive restart escalation.
- Stable default keeps relaxed video-gap escalation thresholds (hold/force windows = `24/12`) to reduce over-eager corrupt/missing-ref cascades on jittery links.
- Wiring paths:
  - `vita/src/host.c` (stable host-side recovery + preset bitrate policy)
  - `lib/src/videoreceiver.c` (stable missing-ref / IDR and gap behavior)
  - `vita/src/config.c`, `vita/include/config.h` (removed `stability_profile` config surface)
  - `lib/include/chiaki/session.h`, `lib/src/session.c` (removed profile transport flag)

## A/B Validation Snapshot (2026-02-15)
Reference run: `31595612449_vitarps5-testing.log` (user-reported "perfect", matching Vitaki baseline feel).

- Baseline markers present:
  - `Bitrate policy: preset_default (6000 kbps @ 960x540)` (`:407`)
  - `Recovery profile: stable_default`
  - `Video gap profile: stable_default (hold_ms=24 force_span=12)` (`:515`)
- Stream cadence stabilized at target:
  - Repeated `PIPE/FPS` windows around incoming/display ~30 FPS (`:752`, `:826`, `:1102`, etc.).
- Visual-corruption counters stayed clean:
  - `AV diag — missing_ref=0, corrupt_bursts=0, fec_fail=0` persisted across windows (`:615`, `:964`, `:1302`, `:1977`).

Interpretation:
- The biggest practical deltas against degraded runs were startup bitrate policy and video-gap aggressiveness, not raw decode speed.
- Stable default is now the only recovery path to reduce config ambiguity and race/regression surface area.

## Investigation + Mitigation Tracks

### Track A: Pipeline Observability (Required First)
- Add generation-tagged diagnostics so each stream/reconnect can be analyzed independently.
- Add explicit low-FPS window counters, including post-reconnect windows.
- Keep logs machine-parseable (`PIPE/SESSION`, `PIPE/FPS`, `PIPE/RECOVER`, `PIPE/REORDER`).

### Track B: Transport/Reorder Robustness
- Reduce dependency-breaking drops under jitter bursts.
- Avoid aggressive queue flush behavior that creates long corrupt-frame cascades.
- Use adaptive gap waiting and bounded reorder aging rather than fixed thresholds.

### Track C: Session Transition Correctness
- Ensure stop/re-entry transitions cannot reuse stale dependency state.
- Enforce lifecycle states (`STOPPING -> QUIESCENT -> STARTING -> ACTIVE`) and generation guards.
- Treat `RP_IN_USE` as a state transition race to be sequenced, not retried immediately.

### Track D: Decode/Present Separation
- Split decode and present to avoid decode stalls on render wait/swap.
- Use bounded decoded-frame ring buffer with deterministic ordering and generation reset.
- Prevent visual artifacts with strict monotonic frame presentation and stale-frame rejection.

## Acceptance Targets
1. Post-reconnect 30s rolling average FPS >= 27 at 540p.
2. No sustained (>20s) periods below 24 FPS without automatic staged recovery.
3. Missing-ref/corrupt burst frequency reduced by >= 50% from current degraded baseline.
4. No increase in disconnect frequency compared to current branch.

## Rollout Strategy
1. Instrumentation-only PR first.
2. Transport/reorder hardening PR second.
3. Session transition hardening PR third.
4. Decode/present split PR fourth.
5. Recovery policy refinement PR last.
