## TODO · Active Work Queue

This document tracks the short, actionable tasks currently in flight. Update it whenever the plan shifts so every agent knows what to do next.

Last Updated: 2026-08-10 (GH #204 PSN WebSocket 403 fix merged; GH #188 decode-decouple COMPLETE, merged to main on 2026-08-10)

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
   - *Related:* Motion-triggered macroblocking investigation (`docs/ai/MOTION_MACROBLOCKING_INVESTIGATION.md`) — queued for future PR once code changes are ready.
5. **Decode-vs-present timing optimization (low priority after GH #206)**
   - *Owner:* Implementation agent
   - *Goal:* GH #188 (PR #199) already decoupled decode to dedicated thread (1.8ms avg/2.4ms max). Only remaining question is whether `vita2d_wait_rendering_done()` in present path blocks decode thread. Current data suggests low priority.
   - *Status:* Hardware A/B (log 11782861312) shows decode is clean; no corruption regressions, no queue drops (`decode_q_drops=0`). If future testing shows GPU stalls affecting overall cadence, revisit with 2-texture ring buffer.
   - *Next Step:* Defer until GH #206 investigation completes; re-evaluate based on whether present path is a bottleneck.
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
10. **Internet Remote Play ship-blocker closure (PSN + holepunch)**
   - *Owner:* Investigation + Implementation agents
   - *Goal:* Move internet remote play from backend-wired prototype to shippable feature.
   - *Current Status (2026-02-19):* PSN host source, token config scaffold, settings toggle, device-list refresh, holepunch connect handoff, holepunch-enabled Release packaging, plus profile-side PSN auth lifecycle controls (device-login start/poll/cancel, token refresh attempt, logout, status UI) are implemented on `feat/psn-internet-remoteplay-mvp`.
   - *Blocking Gap:* Feature is still not ship-ready due to missing production OAuth provider configuration on Vita builds (device/token endpoint + client settings), plaintext token-at-rest storage in `ux0:data/vita-chiaki/chiaki.toml`, plus WAN/NAT hardware validation.
   - *Next Step:* Wire production OAuth app credentials/config into build pipeline, add encrypted storage for persisted PSN OAuth tokens, then run NAT matrix validation with `./tools/build.sh --env testing`.
11. **Remote Play Smoothness Initiative (see `docs/ai/REMOTE_PLAY_SMOOTHNESS_PLAN.md`)**
   - *Owner:* Implementation agents
   - *Goal:* Reduce latency and choppiness on both LAN and PSN-over-internet streams through a prioritized set of Vita-feasible, userland-only improvements.
   - *Spec:* `docs/ai/REMOTE_PLAY_SMOOTHNESS_PLAN.md`
   - *Sub-tasks (check when done):*
     - [ ] **T1: CPU affinity split** — audio→USER_2, Takion→USER_1, decode+render→USER_0 (`vita/src/audio.c:154-155`, `vita/src/video.c:505-507`, Takion thread)
     - [ ] **T2: Symmetric SO_SNDBUF** — set SCE_NET_SO_SNDBUF to match TAKION_A_RWND (512 KB on Vita) (`lib/src/takion.c:289-401`)
     - [ ] **T3: Lower PSN bitrate default** — clamp to ~3500 kbps when holepunch_session != NULL (`vita/src/host.c:206-219`)
     - [ ] **T4: Fix Senkusha 1ms RTT fallback** — change to 30000 µs PSN / 5000 µs LAN (`lib/src/session.c:670-672`)
     - [ ] **T5: Throttle WS ping during stream** — WEBSOCKET_PING_INTERVAL_SEC 5→30 after DATA_ESTABLISHED (`lib/src/remote/holepunch.c:73,968`)
     - [ ] **T6: Reduce NAT burst socket count** — RANDOM_ALLOCATION_SOCKS_NUMBER 48→24 on Vita (`lib/src/remote/holepunch.c:86-90`)
     - [ ] **T7: Port AV reorder queue + timeout flush** — chiaki-ng ea32368: per-stream queues, 16ms stall timeout (`lib/src/takion.c`)
     - [ ] **T8: Port audio jitter buffer + PLC** — chiaki-ng 9d9a9cc + 3e481a9: prefill=3, buf=8, unlock before callback (`lib/src/audioreceiver.c`)
     - [ ] **T9: Diagnostics overlay (LAN+PSN)** — RTT, jitter, bitrate, drop count, PS5 target_bitrate, NAT candidate type (`vita/src/video_overlay.c`, `vita/src/host_metrics.c`)
     - [ ] **T10: Packet slab allocator** — replace malloc(1500)/free per packet with fixed-size pool (`lib/src/takion.c:1143,1163`)
     - [ ] **T11: Decouple HW decode to own thread** — SPSC handoff: Takion assembles frame, dedicated "Vita Decoder" thread calls sceAvcdecDecode (`vita/src/video.c:490-549`)
     - [ ] **T12: Port decoder flush + IDR on loss** — chiaki-ng 855de76: flush + IDR on detected packet loss (`lib/src/videoreceiver.c:381-485`)
     - [ ] **T13: Test IP_TOS=0xB8 (DSCP EF)** — set on Takion socket; validate WMM AC_VO tagging with Wi-Fi capture (`lib/src/takion.c:289-401`)
     - [ ] **T14: NEON-accelerated FEC** — ARMv7 NEON vmull.p8 for Jerasure GF8 inner loop; 3-5× speedup on burst recovery (`lib/src/fec.c`)
     - [ ] **T15: Two-slot decoded-frame ring** — pair with T11; eliminate silent overwrites via 2-texture CDRAM ring (`vita/src/video.c:63,74,286-295`)
     - [ ] **T16: Configurable STUN port-guessing** — chiaki-ng 6e778f7: expose force_stun_port_guessing as per-host config (`lib/src/remote/holepunch.c`)
     - [ ] **T17: Replace force_psn_holepunch global** — pass bool force_internet as stream-scoped arg, not global flag (`vita/src/host.c:144-146`)
   - *Execution order:* T1-T6 first (Tier 1, small), then T9 (observability), then T7-T8-T10-T11-T15-T12 (Tier 2), then T13-T16-T17, then T14 last if still needed.
   - *Last Updated:* 2026-05-01 (research + plan phase complete)
12. **PSN OAuth token encryption** (security hardening)
   - *Owner:* Implementation agent
   - *Goal:* `chiaki.toml` currently stores PSN OAuth access + refresh tokens in plaintext. Design and implement at-rest encryption using a device-derived or user-provided key so tokens aren't recoverable from a plaintext copy of the config file. Blocks enabling PSN internet mode by default.
   - *Tracked from:* PR #95 review, finding #6.
   - *Next Step:* Evaluate Vita keystore / device-derived key options, draft encryption scheme, implement in `vita/src/config.c` token read/write paths.
13. **Thread-safe STUN server shuffle**
   - *Owner:* Implementation agent
   - *Goal:* `lib/src/remote/stun.h`'s Fisher-Yates shuffle mutates the file-scope `STUN_SERVERS` array in place. Concurrent callers (e.g., parallel test harnesses, or future concurrent hole-punch attempts) would race. Switch to a local copy-and-shuffle idiom so the global array is never modified.
   - *Tracked from:* PR #95 review, finding #7.
   - *Next Step:* Replace the in-place shuffle in `stun_get_external_address` and `stun_port_allocation_test` with a stack-allocated copy before shuffling; verify no callers depend on the mutated global order.

---

### 🎨 UI Text Migration (Issue #127 - Phase 2)
1. **Migrate 114 call sites to `ui_text_*` helpers**
   - *Epic:* Issue #127 Phase 2 - Complete migration of all `vita2d_font_draw_text` / `vita2d_font_text_width` call sites to new `ui_text` module helpers
   - *Goal:* Replace ad-hoc baseline offset math (`+5`/`+6` pixel adjustments) with metric-derived layout helpers (`ui_text_draw_centered_v()`, etc.)
   - *Scope:* 7 files with ~114 call sites: `vita/src/ui/ui_screens.c`, `vita/src/ui/ui_components.c`, `vita/src/ui/ui_console_cards.c`, `vita/src/ui/ui_navigation.c`, `vita/src/ui/ui_state.c`, `vita/src/ui.c:176-193`, `vita/src/video_overlay.c:93`
   - *Example Target:* Replace `vita2d_font_draw_text(f, x, y + h/2 + 5, col, sz, s)` with `ui_text_draw_centered_v(f, x, y, h, col, sz, s)` — baseline derived from cached ascent instead of literal offsets.
   - *Plan:* tracked in internal planning notes (see PR #135 and #136 for in-repo context)
   - *Status:* Phase 1 blocking condition resolved (see #145) — text aliasing root cause (POINT atlas filter) fixed via vendored libvita2d patch + LINEAR filter. dee6831 per-glyph integer-snap workaround reverted. Ready to begin Phase 2.
   - *Parent Epic:* #122 UI text rendering modernization
   - *Related Subtasks:* #125 (mipmaps for downscaled icons), #126 (icons at target sizes), #128 (pre-rendered antialiased shapes)
   - *Next Step:* Begin Phase 2 call-site migration across 7 files

---

### 📝 Latency & Performance
1. **Investigate lib-side suspend/resume detection for PS-button-suspend freeze recovery**
   - *Goal:* Detect socket death at transport layer instead of app-level frame stall detection (reverted PR #196)
   - *Lever 1:* ENOBUFS/EBADF escalation in `lib/src/takion.c` send path
   - *Lever 2:* DISCONNECT-during-streaming path in `lib/src/streamconnection.c` (currently gated to STATE_TAKION_CONNECT, line ~435)
   - *Deliverable:* Clean session teardown before PS5 releases old session (avoid RP_IN_USE reconnect rejection)
   - *Context:* Replaces reverted app-level watchdog approach (PR #196); see DEPRECATED.md for root-cause analysis
   - *Status:* Salvaged from PR #197 (closed without merge; documentation originally placed in wrong location)
   - *Next Step:* Investigate transport-layer socket monitoring pattern and plan lib-side instrumentation

2. **Investigate Wi-Fi burst jitter + receive-queue overflow (GH #206)** ⭐ **NEXT PRIORITY**
   - *Goal:* Root-cause lag spikes: genuine Wi-Fi jitter ~60ms at RSSI ~50, receive/reorder queue pinned at 256 slots (1046 single-packet drops with drain cap 64/wakeup), PS5 bitrate throttle 4977k → 1597k floor in response to reported loss.
   - *Evidence:* GH #188 hardware A/B (log 11782861312) disproved jitter theory — decode is now 1.8ms avg/2.4ms max on dedicated thread, but jitter remained 45–87ms avg (207ms max). Congestion control feedback contradicts earlier "PS5 ignores congestion control" belief. **First reported phenomenon (2026-06-26, PR #197):** 20% sustained packet-loss → 1500 kbps PS5 bitrate floor observed in logs 20361349999 and 20639381559.
   - *Proposed Work:* Drain-deficit instrumentation, chiaki-ng-style loss-report capping (~10% in `lib/src/congestioncontrol.c`), and controlled Wi-Fi proximity A/B (RSSI > 80).
   - *Impact:* Understanding true source of lag enables targeted fixes vs. continued architectural thrashing.
   - *Next Step:* Design instrumentation for receive queue drain deficit and loss-report capping, A/B test with proper Wi-Fi isolation.

2. **Implement adaptive jitter buffer** 
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
1. **PSN WebSocket 403 Fix (GH #204)**
   - *Owner:* Implementation agents (dc093a4b, 17456f71, cafe92eb, c476f4b3)
   - *Summary:* Fixed PSN internet remote play 403 Forbidden by re-adopting upstream's 48-char duid format (prefix + 16 random bytes) and restoring duid to OAuth authorize URL. PR #184 had removed duid to fix QR login (#202), but the real issue was Vita's invalid 64-char duid format. Includes one-time token migration marker and Sony retry-interval gating. Additional commits: cafe92eb (RUDP session-request 0x30 gap-report fix), c476f4b3 (four upstream holepunch parity ports).
   - *Changes:*
     - `lib/include/chiaki/remote/holepunch.h`: DUID_PREFIX, CHIAKI_DUID_STR_SIZE 65→49
     - `vita/src/psn_auth.c`: Restored duid parameter, added is_valid_client_duid() validation
     - `vita/src/config.c`: One-time migration marker (psn_auth_provenance=1)
     - `vita/src/psn_remote.c`: Sony retry-interval gate honors X-PSN-RETRY-INTERVAL-MIN/MAX headers
     - `lib/src/remote/holepunch.c`: Device-list fetch timeout 2s→10s, four upstream fixes (c0b7170b clear_notification, df4d6e929 TERMINATE, 02d673eb7 free parity, 254d37ae decode tolerance)
   - *Status:* Code review complete (two guardian rounds), builds pass (testing + test), formatted
   - *Hardware Validation (Build c476f4b3, v0.1.838):*
     - [x] PSN internet connect end-to-end — ✅ Confirmed
     - [x] PSN WebSocket 101 Switching Protocols (not 403) — ✅ Confirmed
     - [x] Clean console-sleep shutdown — ✅ Confirmed (49s stream at 4ms RTT / 3.5Mbps / 30fps)
     - [ ] LAN remote play regression check — ⏳ Open
     - [ ] Off-network (hotspot) connect — ⏳ Open
     - [ ] One-shot token-migration verification — ⏳ Open (old config re-test)
     - [ ] QR login regression — ⏳ Presumed passed (confirm post-flash)
   - *Status:* Awaiting PR #205 merge and completion of regression test suite before moving to Done

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

5. **lib-layer gen/reconnect_gen tagging for PIPE/logs (GH issue #178)**
   - *Owner:* Investigation agent
   - *Goal:* Instrument Chiaki's internal generation/stream ID propagation so all `PIPE/` log lines include a machine-readable tag identifying which generation (reconnect or initial) they belong to.
   - *Status:* Deferred after PR #183 to focus on other priority work.
   - *Next Step:* Spike Chiaki's generation tracking in `lib/src/takion.c` and `lib/src/session.c` to understand how gen IDs flow through log macro callsites.

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
  - See `docs/ai/UI_REFACTOR_SCOPE.md` for detailed completion summary
