## PROGRESS · Roadmap & Epics

Last Updated: 2026-08-11 (Hardware A/B validation complete: PRs #213/#215/#223 VALIDATED, PR #227 pending soft-restart trigger, GH #221 inconclusive/benign; open follow-up issues #219/#220/#221/#222/#224/#225/#226 filed)

A living reference for larger initiatives. Update this document when we start or finish an epic, or when priorities shift.

---

### 1. UI Refactoring: Modularization *(COMPLETE ✅)*
- **Objective:** Split monolithic `vita/src/ui.c` (4,776 lines) into 8 focused modules with clear interfaces and reduced coupling.
- **Progress:** All 8 phases complete (100%)
- **Completed Work:**
  1. Phase 1: Directory structure and headers (260b163) - 2025-12-10
  2. Phase 2: Graphics and animation extraction (97c4033) - 2025-12-11
  3. Phase 3: Input and state management (ad89b6d, ~360 lines removed) - 2025-12-12
  4. Phase 4: Reusable components extraction (d28046e, ~550 lines removed) - 2025-12-12
  5. Phase 5: Navigation system extraction (74ce083, ~1,100 lines removed) - 2025-12-12
  6. Phase 6: Console cards extraction (fd5d1f5, ~650 lines removed) - 2025-12-12
  7. Phase 7: Screen implementations extraction (22bc26d, ~2,260 lines removed) - 2025-12-12
  8. Phase 8: Final cleanup and verification (533eccf, -50 lines) - 2025-12-12
- **Final ui_main.c Size:** ~580 lines (down from 4,776 - **88% reduction**)
- **Final Architecture:** 8 specialized modules + 1 coordinator
  - ui_graphics.c (~520 lines)
  - ui_animation.c (~420 lines)
  - ui_input.c (~300 lines)
  - ui_state.c (~200 lines)
  - ui_components.c (~650 lines)
  - ui_navigation.c (~1,200 lines)
  - ui_console_cards.c (~650 lines)
  - ui_screens.c (~2,260 lines)
  - ui_main.c (580 lines) - Coordinator
- **Modules Created:** 8 specialized modules + 1 coordinator with 11 header files
- **Quality Metrics:**
  - Zero regression in build or functionality
  - 100% functional equivalence with original monolithic ui.c
  - Clear, documented public interfaces in all headers
  - Near-zero global state dependencies in ui_main.c
  - Enabled clean, modular development for future enhancements
- **Status:** Ready for next development phase

### 2. Issue #122 - UI Text Rendering Modernization *(In Progress)*
- **Objective:** Modernize UI text rendering pipeline by integrating FreeType metrics, prewarm glyphs, and eliminate ad-hoc baseline offset math across the codebase.
- **Progress:** Phase 1 + blocking root cause fix complete (60%); Phase 2 now unblocked and ready to begin.
- **Completed Work:**
  1. Phase 1: FreeType glyph atlas prewarm + metric helpers (PR #135 merged, `feat/ui-freetype-atlases-127`)
     - Created `vita/include/ui/ui_text.h` + `vita/src/ui/ui_text.c` with prewarm infrastructure and metric-derived layout helpers
     - Integrated prewarm into UI initialization; all 114 existing call sites left untouched
     - Passed 8 code review rounds with all substantive feedback addressed
  2. Root cause fix: Text aliasing (jaggy edges, horizontal stripes, baseline drift) resolved (PR #145, `fix/ui-text-per-glyph-integer-snap`)
     - Identified root cause: vita2d's font glyph atlas was sampled with POINT (nearest-neighbor) filtering
     - Vendored `third-party/libvita2d/vita2d_font.c` + `texture_atlas.c` with private headers
     - Patched `texture_atlas.c` to use `SCE_GXM_TEXTURE_FILTER_LINEAR` for both min and mag on glyph atlas
     - Reverted dee6831 per-glyph integer-snap workaround; restored kerning in whole-string vita2d calls
     - Fixed CMakeLists.txt to include FreeType2 headers for vendored sources
     - Build clean at v0.1.732
- **Planned Work:**
  3. Phase 2: Migrate 114 call sites to `ui_text_*` helpers (~2-3 week task)
     - Replace `vita2d_font_draw_text(..., y+5)` with `ui_text_draw_centered_v()` + metric-derived math
     - Scope: 7 files (`ui_screens.c`, `ui_components.c`, `ui_console_cards.c`, `ui_navigation.c`, `ui_state.c`, `ui.c`, `video_overlay.c`)
     - Blocking condition: RESOLVED — aliasing root cause fixed; kerning preserved
     - Plan: tracked in internal planning notes (see PR #135 and #136 for in-repo context)
- **Related Subtasks (Parent Epic #122):**
  - #125 Mipmaps for downscaled icons
  - #126 Icons shipped at target sizes
  - #128 Pre-rendered antialiased circle/indicator textures
- **Status:** Phase 2 unblocked and ready to begin immediately.

### 3. Remote Play Smoothness Initiative *(In Progress 🔄)*
- **Objective:** Reduce latency, audio dropouts, and choppiness on both LAN and PSN-over-internet Remote Play through 17 Vita-feasible, userland-only improvements across three tiers.
- **Motivation:** PSN remote play was recently added and is the choppiest codepath; LAN also has headroom to improve.
- **Progress:** Research + feasibility validation complete (2026-05-01); implementation of T1-T6 starting now.
- **Tier 1 (Quick wins):** T1 CPU affinity, T2 SO_SNDBUF, T3 PSN bitrate, T4 Senkusha fix, T5 WS ping throttle, T6 NAT burst reduction
- **Tier 2 (chiaki-ng ports + architecture):** T7 AV reorder queue, T8 audio jitter buffer, T9 diagnostics overlay, T10 packet slab, T11 decode thread decoupling, T12 decoder flush
- **Tier 3 (deferred):** T13 IP_TOS, T14 NEON FEC, T15 frame ring, T16 STUN config, T17 PSN flag cleanup
- **Not planned (validated infeasible):** adaptive PS5 bitrate, TURN relay, async HW decoder API, >444MHz CPU, Wi-Fi 5GHz, 4th core, SO_PRIORITY
- **Spec:** `docs/ai/REMOTE_PLAY_SMOOTHNESS_PLAN.md`
- **Status:** T1-T6 in progress on worktrees.

### 4. Latency Reduction Initiative *(Hardware A/B 2026-08-11 — PRs #213/#215/#223 Confirmed; #227 Pending; #228/#221 Benign. Open follow-ups #219/#220/#221/#222/#224/#225/#226 filed.)*
- **Objective:** Bring Vita remote-play latency below 50 ms local / 100 ms remote by understanding and fixing root causes of lag spikes.
- **Completed (GH #188):** Decode-thread decoupling (PR #199, merged 2026-08-10). Implementation excellent (1.8ms avg/2.4ms max, zero queue drops), but **jitter theory disproven** — jitter remained 45–87ms avg after decoupling.
- **Completed (GH #208):** Transient ENOBUFS retry + mid-stream DISCONNECT propagation (PR #209 merged as 205eed56, v0.1.842). Session-freeze fix addresses stability regression. ENOBUFS-burst validation complete (log 18107437792); transport-death banner items pending.
- **Completed & Hardware Validated (PR #213):** Post-FEC effective loss reporting (4128b99a, v0.1.844+). Fixes PS5 bitrate floor-throttling via outcome-aware accounting; also fixes audio race, frameprocessor flushed flag, alloc_frame stale counters. ✅ Hardware A/B 2026-08-11: A arm bitrate dip 5825k→5691k recovers to 5825k; B arm 5825k flat. Pre-fix staircase (5825k→3579k) eliminated.
- **Completed & Hardware Validated (PR #215):** Reliable stop-to-reconnect (91b8c049, v0.1.844+). Fixes RP_IN_USE rejection via bounded DISCONNECT ack-wait + delivery-aware post-stop guard; single auto-retry on forced rejection. ✅ Hardware A/B 2026-08-11: A arm DISCONNECT acked 14ms + 2s guard + single auto-retry recovered RP_IN_USE; B arm fallback to 8s guard succeeded. Both delivery-aware paths exercised.
- **Completed & Hardware Validated (PR #223):** Corrupt-frame report coalescing (closes GH #218). Design target: burst-tail wire sends ~17 → ~3-4 via 500ms cooldown + 32-frame growth bypass; pure classifier in videoreceiver_gap.c. ✅ Hardware A/B 2026-08-11: per-session totals A arm 9 sends across 3 bursts (3 burst_retired); B arm 4 sends (1 retired) vs 17-21 pre-fix per single burst. Pre-fix staircase gone.
- **Completed but Hardware Validation Pending (PR #227):** Should_stop sticky latch fix (closes GH #214). Streamconnection now resets per-run via `chiaki_stream_connection_prepare_restart()` inside state_mutex; code path fixed; unverified on hardware. ⏳ Hardware A/B 2026-08-11: `fast_restart=0` in all logs; automatic soft-restart arc never exercised (all reconnects were full UI restarts). Feature untested; needs degraded-stream episode.
- **Completed (PR #228):** Diagnostics A/B toggle for GH #221. `VITARPS5_CONGESTION_PARITY_INCLUSIVE_RECEIVED` flag enables comparative parity accounting; wholly-missing-frame accounting audited + documented. Hardware A/B 2026-08-11 inconclusive (benign) — ON arm (parity_inclusive=1) zero adverse effects; 10% cap never bound; zero-loss step-downs not reproduced in either arm. Keep toggle default OFF, GH #221 open.
- **Current Focus:** Follow-up issues #219/#220/#221/#222/#224/#225/#226 prioritized. PR #227 fast_restart validation queued for next degraded-stream episode. Residual known lag = intrinsic Wi-Fi jitter (#219/#206 remnants) — unchanged.
- **Root-Cause Model (Validated 2026-08-11):**
  - **PS5 bitrate ratchet three-factor INPUT model:**
    1. Loss reports (input) — ✅ Fixed by PR #213 (post-FEC accounting)
    2. Corrupt-frame reports (input; dominant driver) — ✅ Fixed by PR #223 (500ms coalescing; upstream chiaki reports each gap once; fork regression from PR #63 caused 17 re-sends)
    3. PS5 delay measurement (input) — ⏳ UNFIXED/OPEN; proven by step-down with loss=0 and no corrupt reports (log 24402261711, line 6010); GH #206 remaining scope
  - **Subjective regression explained:** Monotonic per-session decay + GH #214 breaking soft restart (only ratchet reset, now fixed in PR #227) + post-#209 sessions surviving degraded instead of dying
  - **Original GH #206 Jitter Investigation (Narrowed 2026-08-10):**
    - Proximity A/B (log 13382891119, RSSI 84→100) refuted signal-strength hypothesis — jitter 50–90ms at strong RSSI confirms intrinsic Vita Wi-Fi burstiness (not client issue)
    - Receive queue pinned at 256 slots dripping 1046 single-packet drops despite drain cap already 256/wakeup — drain-rate deficit (GH #219 filed for investigation)
  - **Remaining scope:** GH #219/#220/#221/#222/#224/#225/#226 follow-up investigation

### 5. Controller Layout Redesign *(COMPLETE ✅)*
- **Objective:** Redesign the controller configuration screen with an immersive visual layout featuring procedurally rendered Vita controller diagrams and interactive button mapping views.
- **Progress:** All 3 phases complete (100%)
- **Completed Work:**
  1. Phase 1: Immersive layout implementation (2025-12-14) - Full 960×544 redesign with top bar, diagram, and preset system
  2. Phase 2: Code review and refinements (2025-12-14) - DRY elimination, utility extraction, input conflict fixes, color correction, texture cleanup
  3. Phase 3: Procedural diagram replacement (2025-12-14) - Replaced PNG assets with vita2d procedural rendering, 1.3MB VPK reduction
- **Implementation Details:**
  - Three-view system: Summary View (presets) + Front Mapping View + Back Mapping View
  - Procedural vita2d rendering with ratio-based coordinate system for pixel-perfect scaling
  - Full front view: device body, screen, D-pad, face buttons (△, ○, ×, □), analog sticks, shoulder buttons, system buttons
  - Full back view: device body, rear touchpad with 4 interactive zones (UL, UR, LL, LR)
  - Pulsing glow highlight system for active button/zone indication
  - Interactive callout system showing button mappings (△ → □, etc.)
  - D-pad navigation between controls in mapping views
  - Touch input support for front/back view switching
  - L/R preset cycling in Summary view
  - 6 controller presets with full mapping definitions (Default, FPS, Racing, Fighting, Remote Play Classic, Custom)
  - No external PNG dependencies - all graphics computed procedurally
- **Code Review & Optimization Fixes Applied:**
  - Color Definition Fixed: Changed `CTRL_PNG_TINT_COLOR` from `0xFF3490FF` to correct `0xFFFF9034` (PlayStation Blue)
  - PNG Asset Removal: Eliminated texture loading/cleanup code from ui.c, ui_screens.c, and main.c
  - Texture Cleanup: Removed `ui_diagram_free_textures()` call (no longer needed)
  - Button Highlight Complete: All 16 button IDs (DPAD, TRIANGLE, CIRCLE, CROSS, SQUARE, L, R, LSTICK, RSTICK, PS, START, SELECT, RTOUCH_UL, RTOUCH_UR, RTOUCH_LL, RTOUCH_LR) mapped in procedural rendering
  - Magic Numbers Extracted: Created ~97 ratio constants in ui_constants.h for automatic scaling
  - Touch Constants Integrated: TOUCH_PANEL_WIDTH and TOUCH_PANEL_HEIGHT preserved
  - Ratio-Based System: All coordinates use ratio values (0.0-1.0) for diagram dimensions, enabling arbitrary scaling
  - Animation System: Flip (220ms), color tween (300ms), and pulse (1s cycle) animations preserved with procedural rendering
  - VPK Size Optimization: Reduced from 3.9MB (PNG) to 2.6MB (procedural) - 1.3MB reduction, 33% smaller
- **Files Created:**
  - `vita/include/ui/ui_top_bar.h`, `vita/src/ui/ui_top_bar.c` - Top bar component (~240 lines)
  - `vita/include/ui/ui_controller_diagram.h`, `vita/src/ui/ui_controller_diagram.c` - Diagram renderer (770 lines, fully procedural)
- **Files Modified:**
  - `vita/include/ui/ui_constants.h` - Added ~97 ratio-based coordinate constants for automatic scaling
  - `vita/include/ui/ui_types.h`, `ui_focus.h`, `ui_graphics.h` - Support structures and utilities
  - `vita/src/controller.c` - Preset definitions and mappings
  - `vita/src/ui/ui_controller_diagram.c` - Complete rewrite to vita2d procedural rendering (no PNG textures)
  - `vita/src/ui/ui_screens.c` - Controller screen handler, removed PNG texture loading
  - `vita/src/ui/ui_graphics.c` - Added `ui_draw_rectangle_outline()` utility
  - `vita/src/main.c` - Removed texture cleanup code
  - `vita/CMakeLists.txt` - Removed PNG asset packaging, removed texture loading code
- **Build Status:** VPK generated successfully at `build/vita/VitaRPS5.vpk` (2.6MB)
- **Compiler Status:** Zero compiler warnings
- **Performance:** Draw call budget maintained; ≥58 FPS target maintained
- **VPK Size:** Reduced by 1.3MB (33% reduction) through PNG removal
- **Status:** Production-ready with pixel-perfect scaling at any diagram size
- **Future Enhancements:**
  - Custom per-button persistence (beyond preset selection)
  - Mapping popup full implementation (currently placeholder with show_mapping_popup field documented)

### 6. Build & Release Reliability *(Ongoing)*
- **Objective:** Keep Docker-based builds, automated releases, and deployment scripts stable.
- **Focus Areas:** Monitor GitHub Actions release workflow, ensure `./tools/build.sh` handles new assets/configs, keep documentation current.
- **Status:** Stable - new modular UI structure integrated into build system, assets pipeline validated

---

### Status Log
| Date | Update | Owner |
|------|--------|-------|
| 2026-08-11 | **Hardware A/B Validation Complete — PRs #213/#215/#223 VALIDATED; PR #227 Pending (no soft-restart trigger); GH #221 Inconclusive/Benign** — Logs 27052669255 (Build A parity_inclusive=0) & 27281373937 (Build B parity_inclusive=1, commit 78fe4e86). **PR #213 Validated:** A arm bitrate dip 5825k→5691k recovers full to 5825k (+25k crawl); B arm 5825k flat. Pre-fix staircase (5825k→3579k in 66s) eliminated. **PR #215 Validated:** A arm DISCONNECT acked 14ms + 2s guard, RP_IN_USE auto-retry (6s) succeeded; B arm 400ms timeout → 8s-guard fallback, reconnect succeeded. Both delivery paths exercised. **PR #223 Validated:** A arm 9 corrupt-report sends across 3 bursts (3 burst_retired); B arm 4 sends (1 retired) vs pre-fix 17-21/burst. **PR #227 Pending:** `fast_restart=0` in all logs; automatic soft-restart arc never exercised (all reconnects were full UI restarts). **GH #221 Inconclusive/Benign:** ON arm (parity_inclusive=1) zero adverse effects; neither arm reproduced zero-loss step-downs; 10% cap never bound; keep toggle OFF. **Explained (not regression):** Build B slow start = PS5 waking from rest (78× 620 Server Standby vs Build A 26×); first-session RTT 85ms (post-wake) vs 69ms, second sessions matched 76-77ms; GH #230 filed for silent-wake UX. **Next:** Follow-up prioritization (GH #218-#226 in TODO), PR #227 validation on degraded-stream episode, residual Wi-Fi jitter (#219/#206) unchanged. | @mauricio-gg |
| 2026-08-11 | **Root-Cause Investigation Complete + PRs #223, #227, #228 Merged** — Latency regression was NOT code regression; direct A/B (log 24402261711 vs pre-#213 log 18107437792) showed same bitrate staircase but worse (59 loss ticks vs 3; floor 2427k vs 3579k). Three-factor model: (1) loss reports fixed by PR #213, (2) corrupt-frame reports fixed by PR #223 (500ms coalescing; upstream chiaki reports each gap once, fork regression from PR #63 caused 17 re-sends), (3) PS5 delay measurement (proven by step-down log line 6010). Stalls are Wi-Fi radio bursts. Subjective regression = monotonic decay + GH #214 breaking soft restart (now fixed PR #227) + post-#209 degraded-session survivability. Follow-up issues filed: GH #218 (closes with PR #223), #219 (late reorder drops), #220 (RECV_MALLOC_BURST), #221 (parity A/B + missing-frame blindness), #222 (test suite), #224 (classifier PATHOLOGICAL), #225 (late stop), #226 (mutex errors). Next: hardware validation per TODO item 3 + follow-up prioritization. | @mauricio-gg |
| 2026-08-10 | **PRs #213, #215, #216 Merged — Three Major Stability Fixes** — Post-FEC loss reporting (PR #213, 4128b99a): fixes PS5 bitrate floor-throttling via outcome-aware accounting after FEC flush + 1Hz CONGESTION/LOSS diagnostics; also fixes audio push_seq race + frameprocessor never-set flushed flag + alloc_frame stale counters; 3 code-guardian rounds. Reliable stop-to-reconnect (PR #215, 91b8c049): fixes RP_IN_USE rejection after deliberate stop via bounded 400ms DISCONNECT ack-wait + delivery-aware post-stop guard (0s/2s/8s per state); single RP_IN_USE auto-retry on forced rejection; 3 rounds with dual independent reviewers. Truthful control-channel drop logging (PR #216, 287c6287): fixes misleading Takion queue-overflow evidence (control-channel-only queue artifact) + corrects stale "PS5 ignores congestion feedback" claims in docs (now ratchet-model accurate). All three merged to v0.1.844+. Hardware validation combined checklist added to TODO.md item 3. GH #214 (should_stop sticky latch bug) added as HIGH priority to TODO.md item 4. See DONE.md for full details. | @mauricio-gg |
| 2026-08-10 | **GH #206 Proximity A/B Complete + GH #208 ENOBUFS Fix Merged** — Proximity A/B test (log 13382891119, RSSI 84→100 next to router) confirmed signal-strength hypothesis REFUTED: jitter remained 50–90ms despite strong signal. Vita Wi-Fi burstiness is intrinsic to stack, not client issue. PS5 throttling milder (~5.4Mbps vs prior 1.6Mbps), 249 overflow drops in ~30s. **Concurrently:** GH #208 (PR #209) merged as 205eed56 (v0.1.842) — transient ENOBUFS now retries instead of killing recv thread; mid-stream DISCONNECT propagates cleanly; EBADF flood eliminated. Code-guardian reviewed (blocker + 6 findings, then approved). Proximity A/B evidence added to TODO.md GH #206 item; GH #208 validation checklist added to TODO.md item 2. **Next priority:** Loss-report capping (~10% chiaki-ng-style in lib/src/congestioncontrol.c) + client-side burst absorption. See DONE.md (GH #208 section) and TODO.md (items 1-2). | @mauricio-gg |
| 2026-08-10 | **GH #188 COMPLETE — Decode-thread decoupling (PR #199)** merged to main with excellent performance (1.8ms avg/2.4ms max, `decode_q_drops=0`, zero regressions on hardware A/B log 11782861312). **BUT:** Jitter theory disproven — jitter remained 45–87ms avg after decoupling. **New root cause (GH #206):** Three-factor model: (a) genuine Wi-Fi jitter ~60ms at RSSI ~50, (b) receive queue 1046 single-packet drops despite drain cap already 256/wakeup, (c) PS5 bitrate floor-throttling (4977k → 1597k) in response to loss reports (contradicts "PS5 ignores congestion control"). Proposed GH #206 work: drain-deficit instrumentation, loss-report capping, Wi-Fi proximity A/B. See TODO.md item 1 (reprioritized to GH #206) and DONE.md (GH #188 section). | @mauricio-gg |
| 2026-08-10 | GH #204 PSN WebSocket 403 fix hardware validated (build c476f4b3, v0.1.838): Core functionality confirmed — internet connect end-to-end ✅, WebSocket 101 (403 resolved) ✅, clean console-sleep shutdown ✅ (49s stream at 4ms RTT / 3.5Mbps / 30fps). Two additional commits validated (cafe92eb RUDP gap-report fix, c476f4b3 four upstream holepunch parity ports) after code-guardian review. Regression tests queued: LAN connectivity, off-network (hotspot) connect, one-shot token-migration re-test on old config, QR login re-confirmation post-flash. PR #205 pending merge; keep in "In Review" state until merge + regression suite closes. See `docs/ai/PSN_REMOTEPLAY_WEBSOCKET_403_STATUS.md` hardware validation section. | @mauricio-gg |
| 2026-06-27 | Wi-Fi power-save hint A/B COMPLETE: scePowerSetUsingWireless(1) applied and confirmed (return 0x0, log t=308ms), but 3 hardware runs showed no measurable jitter/RTT improvement. PR #198 closed without merge. LAN bitrate lowering (6000→3500 kbps) ruled out as red herring (PS5 self-throttles to 1.5 Mbps regardless). **Key finding:** sceAvcdecDecode inflates measured jitter by ~55ms (runs synchronously on Takion recv thread at `lib/src/takion.c:76-84`). GH #188 (decode-thread decoupling) confirmed as next actionable lever to unhide true network metrics. | @mauricio-gg |
| 2026-06-26 | **Hardware validation queue:** PR #195 (reorder queue HOL deadlock fix) merged to main at commit cbcb9dc — eliminated stalls during burst loss, validated with A/B testing. **PR #196 (dead-stream watchdog) closed without merge** due to critical regression: torn 64-bit read on ARMv7 (uint64_t `last_decoded_frame_us` accessed without lock from UI + decode threads) caused spurious stall detection at 18446744073709550 ms on healthy 30fps stream (log 20639381559). Secondary cause: RP_IN_USE reconnect rejection (~14s self-inflicted outage). **Forward direction:** lib-side suspend/resume detection via transport-layer socket monitoring (ENOBUFS/EBADF in Takion send, DISCONNECT in streamconnection.c) instead of app-level frame timing. See TODO.md and DEPRECATED.md for lib-side investigation task. | @mauricio-gg |
| 2026-06-26 | RWND experiment closure + metrics validation (PR #193, v0.1.784): A/B tested TAKION_A_RWND 512KB→256KB, found no latency benefit + 4× more freezes, reverted to 512KB (final). `vita/src/host_metrics.c` on-device validation confirms `windowed_bitrate_mbps` now prints honest bitrate every cycle (no more 0↔2 Mbps artifact). GH #188 (Wi-Fi/jitter-buffer bufferbloat) is the next domain focus. | @mauricio-gg |
| 2026-06-25 | Bitrate metrics hardening shipped (PR #183, v0.1.783): windowed Mbps time-based formula (3-slot ring window, fixes ~100 Mbps spikes on buffer realloc), NET_UNSTABLE overlay debounce (500ms, prevents rapid redundant activation during recv bursts). GH issue #178 (lib-layer gen/reconnect_gen tagging) deferred to future spike work. | @mauricio-gg |
| 2026-05-03 | PSN remote-play STUN DNS fix shipped (PR #158, v0.1.749) and header-hygiene followup PR submitted (v0.1.750): `vita_resolve_sin` / `vita_curl_add_resolve` forward-decl drift risk eliminated by splitting `vita_resolve.h` (curl-free) from `vita_dns.h` (curl-aware), wiring `vita/include/` as a PRIVATE chiaki-lib include dir for Vita, replacing hand-typed prototypes in `lib/src/remote/stun.h` and `holepunch.c` with real `#include` statements, and adding `<stdbool.h>` self-sufficiency + inet_ntop fallback logging. Root cause was getaddrinfo() EAI_NONAME regression in vitasdk libcurl 8.17 / GCC 15 toolchain rebuild (2026-05-02). | @mauricio-gg |
| 2026-05-01 | Issue #122 text aliasing root cause fixed (PR #145) — vita2d font glyph atlas was sampled with POINT filtering, causing jaggy edges and baseline drift. Vendored libvita2d (`vita2d_font.c`, `texture_atlas.c`), patched to use LINEAR filter on atlas, reverted dee6831 per-glyph integer-snap workaround. Kerning restored via whole-string vita2d calls. Phase 2 (114 call-site migration) now unblocked. Build clean at v0.1.732. | @mauricio-gg |
| 2026-04-14 | Issue #127 Phase 1 COMPLETE - FreeType glyph atlas prewarm module merged to main (PR #135, feature branch `feat/ui-freetype-atlases-127`). Created `ui_text.h/c` with metric helpers + prewarm infrastructure. All 114 existing `vita2d_font_draw_text()` call sites left untouched per Phase 1 scope. 8 code review rounds, all substantive feedback addressed. Pending: On-device smoke test of prewarm (first-frame pop-in check) before Phase 2 call-site migration. Phase 2 queued with plan file `prancy-sprouting-sunset.md`. | @mauricio-gg |
| 2026-02-16 | Advanced codebase cleanup wave 1 (Vitaki delta-guided): added file inflation analysis tooling (`tools/analysis/compare_vitaki_delta.sh`), published implementation plan (`docs/ai/CODEBASE_CLEANUP_PLAN.md`), landed table-driven bool migration parsing + manual-host ownership fix in `vita/src/config.c`, extracted stream HUD overlays to `vita/src/video_overlay.c`, extracted host/manual-host storage utilities to `vita/src/host_storage.c`, extracted input-thread/controller-touch mapping logic to `vita/src/host_input.c`, extracted disconnect/quit-reason banner helpers to `vita/src/host_disconnect.c`, extracted loss-profile/saturation helper logic to `vita/src/host_loss_profile.c`, extracted hint/decoder-resync/loss-event handlers to `vita/src/host_feedback.c`, extracted reconnect recovery coordinator logic to `vita/src/host_recovery.c`, extracted stream metrics reset/latency diagnostics paths to `vita/src/host_metrics.c`, extracted registration/wakeup flow to `vita/src/host_registration.c`, extracted stream lifecycle/finalization helpers to `vita/src/host_lifecycle.c`, extracted `CHIAKI_EVENT_QUIT` handling/retry orchestration to `vita/src/host_quit.c`, extracted session/video callback plumbing to `vita/src/host_callbacks.c`, extracted registered/manual host parse+serialize logic from `vita/src/config.c` to `vita/src/config_hosts.c` (plus `vitarps5_tests` linkage update in `test/CMakeLists.txt`), added focused config migration coverage in `test/config_vita_tests.c` (legacy/root bool + latency mode), simplified `vita/src/config.c` parse flow with defaults/custom-map and resolution/FPS/latency helper functions, and deflated `vita/src/video.c` to ~641 lines by removing dead frame-pacer scaffolding, obsolete SPS/header processing path, unused hexdump/FPS helpers, and stale commented decode/setup experiments | @mauricio-gg |
| 2026-02-11 | Started stability recovery initiative with split tracks: packet/reference-loss mitigation first on fresh `main` baseline, followed by decode/render architecture split on a separate branch; validation standardized on `./tools/build.sh --env testing` + log matrix | @mauricio-gg |
| 2026-02-10 | Clarified Vita resolution policy in UI/runtime: unsupported legacy 720p/1080p values now display as effective 540p in settings/profile, and stream-start fallback logging is debug-level defensive messaging | @mauricio-gg |
| 2026-02-10 | Settings UX simplified to a single scrollable list (removed extra settings tab, removed Settings-side Controller Map, moved Circle Button Confirm into main settings list) and nav menu close regressions fixed by making explicit collapse actions deterministic | @mauricio-gg |
| 2025-12-14 | CONTROLLER LAYOUT REDESIGN 100% COMPLETE - Final polish batch: stadium shape fixed with procedural rendering (stadium_fill/outline functions), menu input bug resolved (handle_global_nav_shortcuts), all 3 phases finished | Code Review |
| 2025-12-14 | CONTROLLER LAYOUT PHASE 3 COMPLETE - Procedural diagram implementation: PNG assets removed, ~97 ratio constants added, all 16 button IDs implemented, 1.3MB VPK reduction (3.9MB → 2.6MB), pixel-perfect scaling enabled | @mauricio-gg |
| 2025-12-14 | CONTROLLER LAYOUT PHASE 2 CODE REVIEW SIGN-OFF - All fixes verified: color correction (#FF9034), texture cleanup, magic numbers extracted, touch constants added, button highlights complete, zero warnings | Code Review |
| 2025-12-14 | CONTROLLER LAYOUT REDESIGN COMPLETE - Phase 1 (implementation) & Phase 2 (code review) finished with all deliverables | @mauricio-gg |
| 2025-12-14 | CONTROLLER LAYOUT PHASE 2 COMPLETE - Code review fixes applied (DRY elimination, dead code removal, constant naming, input conflict resolution) | @mauricio-gg |
| 2025-12-14 | Controller Layout Visual Fixes - Extracted ui_draw_rectangle_outline() utility, eliminated shoulder button DRY violation, fixed LEFT/RIGHT input conflict | Code Review |
| 2025-12-12 | UI REFACTORING COMPLETE - All 8 phases finished (5,040 lines extracted, 88% reduction) | @mauricio-gg |
| 2025-12-12 | Phase 5-8 complete - Navigation, cards, screens, and final cleanup | @mauricio-gg |
| 2025-12-12 | UI_REFACTOR_SCOPE.md updated to v3.0 with full completion summary | Documentation |
| 2025-12-12 | ROADMAP.md updated - All 8 phases marked complete, next phase defined | Documentation |
| 2025-12-12 | PROGRESS.md updated - UI refactoring marked complete with final metrics | Documentation |
| 2025-12-12 | Phase 4 complete - UI refactoring 50% done (910 lines extracted) | @mauricio-gg |
| 2025-12-12 | Phases 1-4 UI refactoring documented in UI_REFACTOR_SCOPE.md v2 | Documentation |
| 2025-12-11 | Phase 2 complete - Graphics and animation modules extracted | @mauricio-gg |
| 2025-12-10 | Phase 1 complete - UI modular structure established | @mauricio-gg |

Add entries whenever milestones move forward or new initiatives start.
