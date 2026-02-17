## TODO · Active Work Queue

This document tracks the short, actionable tasks currently in flight. Update it whenever the plan shifts so every agent knows what to do next.

Last Updated: 2026-02-16 (Codebase cleanup wave 1 checkpoint: video monolith deflation)

### 🔄 Workflow Snapshot
1. **Investigation Agent** – research, spike, or scoping work; records findings below.
2. **Implementation Agent** – picks up the next ready task, writes code, notes validation.
3. **Review Agent** – senior-level review; either approves or kicks it back with required fixes.

Only move a task to "Done" after the reviewer signs off.

---

### 🟡 In Progress
1. **Robust reconnect + frame-dependency recovery track (architecture-first)**
   - *Owner:* Investigation + Implementation agents
   - *Goal:* Eliminate \"alive but degraded\" post-reconnect sessions (19-24 FPS with persistent missing reference bursts) by hardening transport/reorder + session transition + decode/present pipeline boundaries.
   - *Evidence:* `87116066464_vitarps5-testing.log:2920`, `87116066464_vitarps5-testing.log:3082`, `87116066464_vitarps5-testing.log:3918`, `87116066464_vitarps5-testing.log:5169`; `86888155925_vitarps5-testing.log:1658`, `86888155925_vitarps5-testing.log:75011334`.
   - *Spec:* `docs/ai/STREAM_PIPELINE_ROBUSTNESS_PLAN.md`
   - *Next Step:* Land instrumentation-first PR with stream generation tagging and post-reconnect low-FPS counters in `vita/src/host.c` + `vita/include/context.h`.
2. **Clarify PS5 bitrate negotiation**
   - *Owner:* Investigation agent
   - *Goal:* Confirm whether the PS5 honors `RP-StartBitrate` and LaunchSpec fields by instrumenting the ctrl request (`lib/src/ctrl.c:1136-1245`) and comparing against the LaunchSpec payload (`lib/src/streamconnection.c:843-887`).
   - *Next Step:* Capture control-plane packets (logs) before/after instrumentation.
3. **Stability recovery baseline from main (packet-loss first)**
   - *Owner:* Investigation agent
   - *Goal:* Reproduce 360p/540p behavior on fresh `main` using `./tools/build.sh --env testing` and capture baseline metrics (pixelation onset, missing/corrupt frame bursts, reconnect count).
   - *Next Step:* Run a controlled 5-10 minute session matrix at 360p and 540p with Automatic fallback and store logs for A/B comparison.
4. **Packet/reference-loss mitigation track**
   - *Owner:* Implementation agent
   - *Goal:* Instrument receive/reorder/fallback reason paths and tune loss gates/cooldowns so transient bursts request IDR first instead of entering reconnect oscillation.
   - *Next Step:* Add compact counters and reason tagging around `handle_loss_event()` and stream restart scheduling in `vita/src/host.c`.
5. **Decode/render split prototype (separate branch after packet track)**
   - *Owner:* Implementation agent
   - *Goal:* Decouple decode from present path so `sceAvcdecDecode` is not blocked by `vita2d_wait_rendering_done()`, then validate cadence gains without introducing corruption regressions.
   - *Next Step:* Create `feat/decode-render-split` from updated `main` after packet-track validation and implement a bounded decoded-frame handoff queue.
6. **Startup transport hardening (separate PR/branch)**
   - *Owner:* Implementation agent
   - *Goal:* Isolate initial-session transport failures (early Takion queue overflow and reconnect churn into `RP_IN_USE`) without mixing this work into active-session decode stability tuning.
   - *Evidence:* `84165791498_vitarps5-testing.log:775-823`, `84165791498_vitarps5-testing.log:917-923`, `84165791498_vitarps5-testing.log:1256-1261`
   - *Scope:* Startup-only receive/reorder pressure handling, reconnect sequencing, and cooldown/holdoff tuning.
   - *Out of scope for current PR:* Mid-session decode/reference-loss recovery loop.
   - *Progress (2026-02-13):* Landed startup warmup absorb window + one-shot reorder-queue drain/IDR request, increased Takion reorder queue depth to 256 packets, split startup suppression into soft/hard windows, added startup distress scoring, added restart-handshake cooloff gating, and added deterministic startup bootstrap (`~1s` decode-only window + flush+IDR + clean-frame release) to align first-attempt startup behavior with stable runs (`vita/src/host.c`, `vita/src/video.c`, `vita/include/context.h`, `lib/src/takion.c`).
   - *Instrumentation update (2026-02-12):* Added `PIPE/BUILD` metadata log line (commit/branch/dirty/timestamp) so every test log can be tied to exact binary provenance (`tools/build.sh`, `vita/CMakeLists.txt`, `vita/src/logging.c`).
   - *Next Step:* Run startup-only A/B validation with `./tools/build.sh --env testing` (cold connect + 3x reconnect without app quit) and compare:
     - counts for `Takion receive queue overflow`, `missing reference`, and `send buffer overflow`
     - suppression markers `restart_suppressed_startup_soft_grace` vs `restart_suppressed_startup_hard_grace`
     - startup distress marker `Takion overflow startup distress score=...`
     - startup bootstrap markers `PIPE/BOOTSTRAP action=flush_and_idr` and `PIPE/BOOTSTRAP action=ready ...`
     - restart-churn markers `PIPE/RESTART_FAIL ... handshake_init_ack`, `PIPE/RESTART ... action=blocked_cooloff`, `PIPE/RECOVER ... action=stage2_suppressed`
     - stability windows (`PIPE/FPS`, reconnect generations) against the 2026-02-11 and 2026-02-12 baselines.
7. **Follow-up robustness pass (post-merge cleanups)**
   - *Owner:* Implementation agent
   - *Goal:* Close remaining non-blocking review debt without destabilizing the active packet-path baseline PR.
   - *Scope:*
     - Reorder queue fallback optimization/profile pass (replace O(n) fallback if hotspot confirmed).
     - Split `vita/src/host.c` recovery and diagnostics logic into smaller modules/functions.
     - Add focused tests for recovery timing/state transitions (stale diagnostics windows, reconnect stage transitions, span edge cases).
   - *Next Step:* Open dedicated follow-up PR immediately after baseline merge and land test-first where possible.
8. **Codebase cleanup wave 1 (Vitaki delta-guided)**
   - *Owner:* Implementation agent
   - *Goal:* Reduce runtime complexity in inflated non-UI files while preserving all VitaRPS5 enhancements and behavior.
   - *Spec:* `docs/ai/CODEBASE_CLEANUP_PLAN.md`
   - *Progress (2026-02-16):* Added delta tooling (`tools/analysis/compare_vitaki_delta.sh`), created cleanup plan doc, refactored repetitive config migration bool parsing into a table-driven path in `vita/src/config.c`, extracted stream HUD/indicator rendering from `vita/src/video.c` to `vita/src/video_overlay.c`, extracted host/manual-host storage utilities from `vita/src/host.c` to `vita/src/host_storage.c`, extracted input-thread/controller-touch mapping logic from `vita/src/host.c` to `vita/src/host_input.c` with a narrow stop-request bridge API, extracted disconnect/quit-reason banner helpers from `vita/src/host.c` to `vita/src/host_disconnect.c`, extracted loss-profile calculation/saturation helpers from `vita/src/host.c` to `vita/src/host_loss_profile.c`, extracted hint/decoder-resync/loss-event handlers from `vita/src/host.c` to `vita/src/host_feedback.c`, extracted reconnect recovery coordinator logic from `vita/src/host.c` to `vita/src/host_recovery.c`, extracted stream metrics reset/latency diagnostics paths from `vita/src/host.c` to `vita/src/host_metrics.c`, extracted registration/wakeup flow (`host_register`/`host_wakeup` and callback handling) from `vita/src/host.c` to `vita/src/host_registration.c`, extracted stream lifecycle/finalization helpers from `vita/src/host.c` to `vita/src/host_lifecycle.c`, extracted `CHIAKI_EVENT_QUIT` handling/retry orchestration from `vita/src/host.c` to `vita/src/host_quit.c`, extracted session/video callback plumbing (`event_cb`/`video_cb`) from `vita/src/host.c` to `vita/src/host_callbacks.c`, extracted registered/manual host parse+serialize logic from `vita/src/config.c` to `vita/src/config_hosts.c` (including `test/CMakeLists.txt` linkage update for `vitarps5_tests`), added focused config migration tests (`legacy`/`root` bool + `latency_mode`) in `test/config_vita_tests.c`, helperized `config.c` defaults/custom-map parsing plus resolution/FPS/latency migration parsing/serialization loops, and further deflated `vita/src/video.c` by removing dead legacy paths (`vita_h264_process_header`, stale SPS/header experiments, obsolete frame-pacer init status stage, and large commented setup/decode scaffolding), bringing the file down to ~641 lines.
   - *Next Step:* Complete strict non-UI Vitaki-delta cleanup in this order: (1) finalize `vita/src/controller.c` simplification, (2) finalize `vita/src/discovery.c` simplification + warning cleanup, (3) finalize `vita/src/audio.c` simplification, then run full validation (`./tools/build.sh test`, `./tools/build.sh debug`, `./tools/build.sh --env testing`) and close Wave 1. Defer large UI monolith refactors (for example `vita/src/ui/ui_screens.c`) to a separate phase after Wave 1.
9. **Freeze-recovery hard fallback (separate branch post-cleanup)**
   - *Owner:* Implementation agent
   - *Goal:* Address rare “video freeze while session remains connected” cases by escalating to a hard fallback when unrecovered/corrupt-frame held streaks persist after non-blocking IDR resync attempts.
   - *Evidence:* `35786104837_vitarps5-testing.log:2329`, `35786104837_vitarps5-testing.log:2334`, `35786104837_vitarps5-testing.log:4907`
   - *Scope:* Add a guarded escalation path in `vita/src/host.c` loss/recovery flow (around decoder resync and unrecovered-frame counters), with cooldown and single-use safeguards to avoid reconnect oscillation.
   - *Branching:* Implement on a dedicated follow-up branch after current codebase-cleanup objective is complete.
   - *Next Step:* Draft branch-level acceptance criteria and log markers before coding.

---

### 📝 Latency & Performance
1. **Implement adaptive jitter buffer** ⭐ **HIGH PRIORITY**
   - *Goal:* Replace static reorder queue timeout with adaptive algorithm that adjusts playout delay based on measured network jitter
   - *Files:* `lib/src/reorderqueue.c`, `lib/include/chiaki/reorderqueue.h`, `lib/src/takion.c`
   - *Algorithm:* Measure inter-arrival jitter using EWMA (α=0.125), calculate dynamic threshold (2.5× jitter + safety margin), skip gaps after adaptive timeout instead of blocking indefinitely
   - *Expected:* 10-25ms latency reduction on good WiFi (3-5ms threshold), fewer drops on bad networks (auto-scales to 30-50ms), eliminates head-of-line blocking
   - *References:* RFC 5764 (RTP jitter buffer), WebRTC NetEq, `docs/LATENCY_ANALYSIS.md`
   - *Next Step:* Add `JitterStats` struct, implement measurement in `push()`, modify `pull()` with adaptive timeout

2. **Expose low-bandwidth profile in config/UI**
   - Allow selecting 360p / <2 Mbps preset through the modern settings once backend supports it.

3. **Graceful mid-stream packet-loss fallback**
   - Automatically lower bitrate without tearing down the whole UI when Ultra Low still drops frames.
   - Keep discovery paused, show a "reconnecting" overlay, and restart video/audio while preserving ctrl state.

4. **Preserve controller responsiveness through fallbacks**
   - Instrument `input_thread_func()` to log when pad packets stall, then cache/synchronize pad state so restarts don't add extra lag.
   - Investigate keeping ctrl alive while video/audio reconnect to avoid input gaps.
   - Latest telemetry (`vitarps5.log:11302-11324`) shows the controller gate stays closed for ~6.3 s during packet-loss retries despite gameplay resuming, so we need to re-arm `inputs_ready` (or keep ctrl alive) much earlier in the reconnect flow.

5. **Calibrate loss-detection thresholds**
   - Tune `LOSS_EVENT_MIN_FRAMES`, `LOSS_RETRY_DELAY_US`, and related constants in `vita/src/host.c:34-210` so the soft reconnect only fires after sustained loss bursts, preventing extra latency from single-frame hiccups.

6. **Keep controller thread alive during soft restarts**
   - Augment `request_stream_restart()`/Chiaki restart handling so controller packets continue flowing while the stream connection rebuilds, preventing the brief input pause currently logged around `context.stream.fast_restart_active` in `vita/src/host.c:129-234`.

7. **Instrument soft-reconnect metrics**
   - Add log hooks or UI indicators around the new soft restart path and packet-loss counters (`vita/src/host.c:373-520`, `vita/src/video.c`) to correlate lag spikes with the fallback path, supporting the ongoing investigation in `docs/INCOMPLETE_FEATURES.md`.

8. **Upstream protocol support for dynamic bitrate**
   - Spike Chiaki/PS5 changes required to renegotiate bitrate mid-session (ctrl RPC or LaunchSpec update).
   - Document needed evidence so we can eventually reconfigure without a teardown.
9. **Classify pixelation root cause from testing logs**
   - Compare packet-loss indicators (missing references, corrupt frame bursts) against decode pressure indicators (queue depth/drops, decode anomalies) to avoid tuning the wrong subsystem.
   - Current evidence points to packet/reference loss dominance in `72630530292_vitarps5-testing.log`.

### 📥 In Review
1. **Instrument PS5 bitrate/latency metrics**
   - *Owner:* Implementation agent (latency instrumentation)
   - *Summary:* Added runtime bitrate + RTT sampling via `vita/src/host.c` (using `chiaki_stream_stats_bitrate`) with gated logging and profile card display in `vita/src/ui.c`. Metrics reset on stream stop and update whenever frames arrive.
   - *Needs:* Reviewer to verify code quality, ensure no race conditions with Chiaki structs, and confirm UI integration looks correct on hardware.
2. **Add latency mode presets (1.2–3.8 Mbps)**
   - *Owner:* Implementation agent
   - *Summary:* Introduced `latency_mode` config/UI dropdown plus bitrate overrides in `vita/src/host.c` so users can pick Ultra Low → Max bandwidth targets. Added presets to config serialization and documented options in README.
   - *Needs:* Reviewer to validate Chiaki profile overrides, ensure config migration works, and smoke-test the new dropdown on hardware.

3. **FPS instrumentation + client-side clamp**
   - *Owner:* Implementation agent
   - *Summary:* Added per-second frame cadence logging in `vita/src/video.c` + stored the negotiated fps in `vita/src/host.c`. Implemented a "Force 30 FPS Output" toggle (UI + config) that drops frames deterministically when the PS5 still streams 60 fps, while keeping 30 fps streams untouched. README now documents the new option.
   - *Needs:* Reviewer to verify the pacing logic on hardware (ensure toggling works while streaming, overlay logs make sense, and the Vita maintains 30 unique frames when the clamp is enabled).

4. **Update `RP-StartBitrate` handling**
   - *Owner:* Implementation agent
   - *Summary:* `lib/src/ctrl.c` now encrypts the requested bitrate instead of zeros when `ChiakiConnectInfo.send_actual_start_bitrate` (driven by the new `send_actual_start_bitrate` config flag + README docs) is enabled, letting Vita A/B test real StartBitrate payloads.
   - *Needs:* Reviewer to ensure the PS5 handshake still accepts the StartBitrate header with non-zero payloads and that the new config default behaves on hardware.

---

### ✅ Done
- **Settings simplification + navigation collapse behavior fix**
  - Removed redundant second Settings view and deleted the Settings-side `Controller Map` control; controller mapping now remains only in the dedicated Controller screen (`vita/src/ui/ui_screens.c`).
  - Moved `Circle Button Confirm` to the bottom of the main scrollable Settings list and removed the `Keep Navigation Pinned` setting/config field (`vita/include/config.h`, `vita/src/config.c`).
  - Restored deterministic menu close behavior for explicit actions (nav exit to content, tapping content area, and selecting nav items) by removing pin-gating from collapse paths (`vita/src/ui/ui_focus.c`, `vita/src/ui/ui_navigation.c`).

- **Stream retry cooldown + input readiness gate**
  - Added a 3 s cooldown after `CHIAKI_EVENT_QUIT` and require it to expire before `host_stream()` can run again (`vita/src/host.c:110-134`, `vita/include/context.h:46-60`, `vita/src/host.c:470-520`).
  - Introduced `inputs_ready` so the input thread starts sending pad data on `CHIAKI_EVENT_CONNECTED`, not the first video frame (`vita/src/host.c:297-474`).

- **Discovery grace window**
  - Track `last_discovery_seen_us` for each host and keep entries alive for 3 s before pruning so consoles that momentarily disappear don't thrash (`vita/include/host.h:17-26`, `vita/src/discovery.c:16-191`).

- **Takion queue monitoring**
  - Restored the reorder queue to 64 entries and log high-water usage to validate headroom while we consider adaptive sizing later (`lib/src/takion.c:46-120`, `lib/src/takion.c:919-1024`).

- **UI Refactoring Phase 1-4**
  - Phase 1: Created modular UI directory structure with headers (260b163)
  - Phase 2: Extracted graphics and animation modules (97c4033)
  - Phase 3: Extracted input and state management modules (ad89b6d)
  - Phase 4: Extracted reusable components module (d28046e)
  - Total code reduction: ~910 lines from ui.c
  - See `/Users/mauriciogaldos/Developer/AndeanBear/vitarps5/docs/ai/UI_REFACTOR_SCOPE.md` for detailed completion summary
