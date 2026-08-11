# Completed Work

This document tracks completed work, organized by batch/date, preserving epic grouping and context for future reference.

---

## 2026-08-11 (Root-Cause Investigation Complete; PRs #223, #227, #228 Merged + Follow-Up Issues Filed)

### Root-Cause Analysis Completion
- [x] **Latency root-cause investigation complete (session log 24402261711, build 287c6287)**
  - **Findings:** User-reported "latency regressed" was NOT a code regression — direct A/B showed pre-#213 baseline (log 18107437792) was worse (59 nonzero loss ticks, floor 2427k) vs fresh run (log 24402261711) (3 ticks, floor 3579k).
  - **Three-factor model identified:**
    1. **Loss reports** — Fixed by PR #213 (post-FEC accounting)
    2. **Corrupt-frame reports** (dominant remaining driver) — Fixed by PR #223 (500ms cooldown + coalescing); upstream chiaki reports each gap once; VitaRPS5 fork regression from PR #63 caused 17 wire sends for same expanding range 175→222
    3. **PS5 delay measurement** — Proven by step-down with loss=0 and no corrupt reports (log line 6010)
  - **Stalls are network-side Wi-Fi radio bursts**, not client blockage
  - **Subjective regression explained by:** Monotonic per-session decay + GH #214 breaking soft restart (only ratchet reset) + post-#209 sessions surviving degraded instead of dying

### PR #223 - Corrupt-Frame Report Coalescing (MERGED)
- [x] **Reduce burst-tail corrupt-report wire sends via coalescing (GH #218)**
  - **Root Cause:** Upstream chiaki reports each gap once; VitaRPS5 fork regression (PR #63) re-reports expanding range, emitting 17 wire sends for same stall
  - **Fix:** 500ms cooldown + 32-frame growth bypass on same-start expansion re-reports; pure classifier `chiaki_video_corrupt_report_classify` with exactly 15 unit-test assertions
  - **Burst-tail completeness via retirement hook:** Span-sanity guard (4096) hoisted to cover all four report sites
  - **Expected:** ~17 → ~3-4 sends per burst
  - **Hardware Validation:** Pending
  - **Files:** `lib/src/videoreceiver_gap.{c,h}` (classifier), `lib/src/videoreceiver.c` (four report sites + constants), `lib/include/chiaki/videoreceiver.h`, `test/packet_path_tests.c` (unit tests)
  - **Code Review:** 3 rounds (code-guardian), all findings fixed; APPROVED

### PR #227 - Should_Stop Sticky Latch Fix (MERGED)
- [x] **Fix GH #214: streamconnection should_stop never resets, breaks soft restarts**
  - **Root Cause:** `should_stop` sticky latch was never reset per-run; soft restarts would bail at first CHECK_STOP on second+ restart attempts
  - **Fix:** `chiaki_stream_connection_prepare_restart()` inside the state_mutex critical section; restart refused (correctly reported as DISCONNECTED) when remote teardown raced in; request rollback prevents `host_recovery` retry deadlock
  - **Impact:** Soft restart (the user-accessible ratchet reset) works again
  - **Hardware Validation:** Pending
  - **Code Review:** 6 total rounds (code-guardian scrutiny), all findings fixed; APPROVED

### PR #228 - Diagnostics A/B Toggle (MERGED, Part of GH #221)
- [x] **Add VITARPS5_CONGESTION_PARITY_INCLUSIVE_RECEIVED compile A/B toggle**
  - **Purpose:** Comparative parity accounting diagnostic for GH #221 (structural missing-frame accounting audit)
  - **Default:** OFF (production path unchanged); ON arm: `VITARPS5_CONGESTION_PARITY_INCLUSIVE_RECEIVED=1 ./tools/build.sh --env testing`
  - **Telemetry:** CONGESTION/RECEIVED_MODE log line; testing log queue 256→512 (ceiling 1024, both clamps unified)
  - **Wholly-missing-frame accounting:** Audited + documented on GH #221 (structural, no code)
  - **Hardware Validation:** Pending (parity A/B run per GH #221)

### Follow-Up Issues Filed (GH #218-#226)
- [x] **GH #218** (closes with PR #223) — Corrupt-frame report coalescing
- [x] **GH #219** — Persistent control-channel late reorder drops
- [x] **GH #220** — RECV_MALLOC_BURST churn
- [x] **GH #221** — Parity denominator A/B + missing-frame blindness (open, hardware run pending)
- [x] **GH #222** — Test suite never executes (cross-compile only)
- [x] **GH #224** — Classifier PATHOLOGICAL disposition + fec_failed WARN latch follow-ups
- [x] **GH #225** — Late stop during senkusha gap
- [x] **GH #226** — Streamconnection mutex-held error returns

---

## 2026-08-10 (PRs #213, #215, #216 Merged + GH #188 Decode-Thread Decouple A/B Completion)

### GH #188 / PR #199 - H.264 Decode-Thread Decoupling (MERGED)
- [x] **Decouple sceAvcdecDecode from Takion recv thread (PR #199)**
  - Implementation: `sceAvcdecDecode` moved to dedicated VitaDecode thread (USER_1) via SPSC queue in `vita/src/video.c`
  - Hardware A/B validated on log 11782861312 (build 300719a0; PR #199 merged to main as commit 48c860e7)
  - **Key Results:**
    - Decode performance: 1.8ms avg / 2.4ms max (excellent, off critical path)
    - Queue health: `decode_q_drops=0` across all sessions (no overflow)
    - Frame integrity: Zero gap-skips, zero missing references, no regressions
    - Video playback: no decode-side corruption or regressions; display_fps 23–26 vs 30 was network-side (PS5 bitrate throttling, see GH #206)
  - **Architecture:** Single-writer recv thread → bounded SPSC queue → single-reader decode thread; safe handoff via `frame_ready_for_display` volatile bool + `vita2d_wait_rendering_done()`
  - Files: `vita/src/video.c` (SPSC ring + decode thread spawn/shutdown), `vita/include/video.h` (`vita_video_decode_queue_drops()` getter), `vita/src/host_metrics.c` (`decode_q_drops` in PIPE/FPS), `lib/src/takion.c` (stale comment update)

### GH #208 / PR #209 - ENOBUFS Session-Freeze Fix (MERGED 205eed56)
- [x] **Fix transient ENOBUFS handling in takion_recv (lib/src/takion.c)**
  - **Root Cause:** `recv()` on Takion UDP socket treated transient ENOBUFS (kernel buffer full, recoverable) as fatal error, killing the recv thread mid-stream. Session would freeze waiting on dead socket.
  - **Fix:** Retry recv with 5ms sleep (`TAKION_RECV_TRANSIENT_RETRY_DELAY_MS`), escalate to hard failure only after 400 consecutive errors (~2s of sustained buffer-full). Count-only escalation (not time-windowed) so long idle gaps between packets can't falsely trip threshold. Non-blocking drain-loop call stays zero-cost poll: counts error but never sleeps.
  - **Validation:** Prevents transient buffer-full errors from causing session-freezes; keeps recovery-restart loop active.

- [x] **Propagate mid-stream DISCONNECT events (lib/src/streamconnection.c)**
  - **Root Cause:** `stream_connection_takion_cb()` only handled DISCONNECT during handshake state; mid-stream DISCONNECT was silently ignored, so session remained frozen waiting on dead transport.
  - **Fix:** Mid-stream DISCONNECT now propagates via `remote_disconnected` → "Transport disconnected" banner within ~2s; session cleanly tears down instead of hanging.

- [x] **Eliminate EBADF flood on socket close (lib/src/takion.c, session.c)**
  - **Root Cause:** Sender threads racing teardown would attempt writes to already-closed fd, flooding logs with 1,363+ EBADF lines per failed session. Additionally, `chiaki_congestion_control_stop()` was commented-out in upstream teardown, leaving sender threads running.
  - **Fix:** Invalidate `takion->sock` before closing fd so sender threads hit closed-socket check (not EBADF); rate-limit send logging to ~1 line/s; made `session.c` quit mapping NULL-safe (closed pre-existing latent crash on cleanup).

- [x] **Code Review (code-guardian, 2 rounds)**
  - Round 1: 1 blocker (stale latched disconnect state broke recovery restarts) + 6 required findings (edge cases, missing validation)
  - Round 2: All findings fixed; APPROVED

- [x] **Build & Evidence**
  - Merged as commit 205eed56 (2026-08-10)
  - Build version: v0.1.842
  - Evidence: Proximity A/B test log 13382891119 (same session that triggered ENOBUFS freeze; now continues streaming with single WARN)

### PR #213 - Post-FEC Effective Loss Reporting (MERGED 4128b99a)
- [x] **Fix PS5 bitrate floor-throttling via post-FEC loss accounting**
  - **Root Cause:** Loss reported to PS5 pre-FEC + PS5's downward-only bitrate ratchet (5825k→2591k over 75s from 0.8–5% loss reports, under inert 10% cap)
  - **Fix:** Post-flush FEC-outcome-aware accounting (source-only), recovered-loss safety valve (divisor 4), 1Hz CONGESTION/LOSS diagnostic logging
  - **Additional Bugs Fixed:** Audio push_seq race, seq-math integer promotion + inverted max-loss branch, never-set frameprocessor `flushed` flag (late units corrupted compacted buffer + false duplicate errors), alloc_frame stale counters (underflow hazard)
  - **Code Review:** 3 rounds (code-guardian), all findings fixed
  - **Hardware A/B:** PENDING (checklist: CONGESTION/LOSS shows raw_lost>0/fec_recovered≈raw_lost/reported≈0; no staircase, target holds ~5.8Mbps 10+min; forced loss still reports + IDR)

### PR #215 - Reliable Stop-to-Reconnect Path (MERGED 91b8c049)
- [x] **Fix RP_IN_USE reconnect rejection after deliberate stop**
  - **Root Cause:** Fire-and-forget DISCONNECT (19ms teardown) + user stop clearing its own cooldown, PS5 releasing sessions asynchronously 4–9s
  - **Fix:** Bounded 400ms DISCONNECT ack-wait (existing DATA_ACK machinery), delivery-aware post-stop guard (0s never-streamed / 2s acked / 8s unacked) with visible countdown hint at all three gate sites, "Streaming stopped" banner suppressed on deliberate stops, single RP_IN_USE auto-retry (UI-loop-fired, overlay visible, PSN/LAN route preserved)
  - **Code Review:** 3 rounds (dual independent reviewers on round one), all findings fixed
  - **Hardware Validation:** PENDING (checklist: "DISCONNECT acked after N ms" line on user stop; stop→immediate reconnect no RP_IN_USE ×10; forced RP_IN_USE → one auto-retry; cancel-during-connect instant re-press)

### PR #216 - Truthful Control-Channel Drop Logging + Docs Corrections (MERGED 287c6287)
- [x] **Fix misleading Takion receive queue overflow evidence + correct stale PS5 congestion control claims**
  - **Root Cause of Log Artifact:** Control-channel-only queue (video bypasses), count-pinned-at-1 logging artifact, benign dup/late retransmit drops
  - **Fix:** Rate-limited logging with per-reason buckets (dup/late/overflow/gap_skip/flush) for accurate telemetry; updated docs to reflect ratchet model (PS5 **does** throttle in response to loss reports)

### New Follow-Up Issues
- **GH #212 (low priority):** videoreceiver alloc_frame error discarded → stale unit_slots geometry
- **GH #214 (HIGH priority):** streamconnection `should_stop` sticky latch never reset per-run — soft restarts bail at first CHECK_STOP; explains `classified=handshake_init_ack` restart-failure telemetry

### Root-Cause Discovery: Jitter Theory DISPROVEN
- [x] **Original hypothesis: Decode-on-recv-thread coupling inflates jitter by ~55ms — REJECTED**
  - Baseline jitter before PR #199: 45–87ms avg (207ms max)
  - Jitter after PR #199 (decode now off recv): 45–87ms avg (207ms max) — **NO CHANGE**
  - **Conclusion:** Decode-recv coupling was NOT the source of inflated jitter
  - **Decision:** Merged as neutral-positive architectural improvement (cleaner code, honest thread budgeting) despite disproven jitter theory
  - Actual root cause tracked separately in GH #206

### New Root-Cause Understanding (GH #206 - "Wi-Fi burst jitter + receive-queue overflow drive PS5 bitrate floor-throttling")
- [x] **Identified: Three-factor lag driver**
  - (a) **Genuine Wi-Fi arrival-time jitter ~60ms at RSSI ~50** (base RTT 6–10ms, actual 63–84ms; one session had zero client drops yet 64ms jitter)
  - (b) **Receive queue pinned at 256 slots dripping 1046 single-packet drops despite drain cap already being 256/wakeup** (constant `TAKION_RECV_DRAIN_MAX` at `lib/src/takion.c:71`, raised from 64 by commit a9b61cb1; drain loop at `lib/src/takion.c:1261-1276`). Drain cap is not the bottleneck; deficit is elsewhere (recv-thread scheduling during bursts / sustained arrival rate exceeds drain rate).
  - (c) **PS5 congestion control throttling target_bitrate 4977k → 1597k floor** within each session in response to reported loss
    - **Note:** This CONTRADICTS earlier belief that "PS5 ignores congestion control feedback"
    - Loss-report rate ~200ms interval, PS5 reacts progressively within each session

### GH Issue #188 Literal Proposal (Carried Forward to GH #206)
- GH #188 issue body proposed client-side loss/bitrate adaptation (~10% loss-report cap); PR #199 auto-closed issue without implementing that proposal. The adaptation strategy is carried forward in GH #206 investigation as one of the proposed work items (chiaki-ng-style loss-report capping).

### Next Priority: GH #206 Investigation
- Proposed work: Drain-deficit instrumentation, chiaki-ng-style loss-report capping (~10%), controlled Wi-Fi proximity A/B (RSSI > 80)
- Rationale: Understanding true source of lag enables targeted fixes vs. continued architectural thrashing

---

## 2026-06-27 (Wi-Fi Power-Save Hint A/B Investigation)

### Wi-Fi Jitter Reduction Investigation: A/B Testing Round
- [x] **Wi-Fi power-save hint A/B (PR #198, scePowerSetUsingWireless(1))**
  - Applied hint on branch `fix/wifi-using-wireless-hint`; confirmed applied (return `0x0`, log-verified at t=308ms)
  - Ran 3 hardware A/B runs: measured network jitter (median ~60ms before and after), RTT (unchanged)
  - **Result:** No measurable jitter or RTT improvement. PR #198 closed without merge.

- [x] **LAN bitrate lowering ruled out (6000→3500 kbps)**
  - Investigated as secondary candidate; determined to be a red herring
  - Root cause: PS5 self-throttles to its 1.5 Mbps floor under congestion regardless of requested ceiling
  - Lower bitrate ceiling cannot raise delivered throughput; dropped from active investigation

- [x] **Real next lever identified: decode-thread decoupling (GH #188)**
  - A/B testing revealed sceAvcdecDecode runs synchronously on Takion recv thread
  - **Superseded 2026-08-10** — the claimed jitter inflation of ~55ms was later disproven by GH #188 hardware A/B testing; jitter remained unchanged after decoupling. See GH #188 A/B section above for corrected analysis.
  - This appeared to be self-inflicted jitter inflation; not a network problem
  - Network jitter metrics were suspected to be dishonest; honest measurement requires decode off recv thread
  - **Next actionable step:** Decouple decode to dedicated thread → SPSC queue → honest network jitter visibility

---

## 2026-06-26 (Dead-Stream Watchdog PR Reverted; Reorder Queue Fix Merged)

### Hardware Validation Queue – Streaming Robustness Improvements
- [x] PR #195: Reorder queue head-of-line deadlock fix – Merged to main at commit `cbcb9dc` (2026-06-26)
  * Eliminated HOL deadlock when packet reorder queue reaches capacity
  * Prevents Takion receive thread stall on healthy streams during burst loss
  * Validated with hardware A/B testing; stable improvement confirmed
  * Status: COMPLETE and MERGED

- [x] PR #196: Dead-stream watchdog (frame stall detection) – Closed without merge (reverted)
  * Root cause: Torn 64-bit read on ARMv7 (uint64_t last_decoded_frame_us accessed without lock from UI + decode threads)
  * Secondary cause: RP_IN_USE reconnect rejection (PS5 hadn't released old session yet)
  * Result: False positive frame stall detection → spurious teardown → ~14s self-inflicted outage on healthy stream (log 20639381559)
  * Decision: Deprecated app-level watchdog approach in favor of lib-side transport-layer detection
  * Status: REVERTED; see DEPRECATED.md for forward direction (Takion socket monitoring, streamconnection.c DISCONNECT handling)

**Context:**
Two independent streaming robustness improvements scheduled for hardware validation. PR #195 (reorder queue fix) validated and merged successfully. PR #196 (watchdog) identified a critical regression during A/B testing: app-level frame stall detection is incompatible with 32-bit ARM's inability to safely read 64-bit timestamps across threads without atomics. Root-cause documented; next direction is to move detection to transport layer (lib-side) where socket state is observable.

**Files Modified:**
- PR #195: `lib/src/takion.c`, `vita/src/video.c`
- PR #196: (Reverted; branch `fix/dead-stream-watchdog` kept on remote for reference)

**Note:** Content salvaged from PR #197 (closed without merge; targeted wrong-location tracker copies at docs/ instead of repo root).

---

## 2026-06-25 (PSN Login Fix & Bitrate Metrics)

### PSN QR Login Fix
- [x] **Remove `duid` parameter from OAuth authorize URL (Fix #202, PR #184)**
  - Removed the `duid` query parameter from the PSN OAuth authorize endpoint in `vita/src/psn_auth.c`
  - The `duid` param caused Sony's authorize endpoint to return "Something went wrong" before the login page loaded
  - URL now matches chiaki-ng's confirmed-working flow exactly
  - On-device validation: PSN QR login now shows the login page and completes successfully
  - Merged to main in PR #184 (v0.1.784)

- [x] **Motion-Triggered Macroblocking Investigation Documentation**
  - Root-cause analysis document created at `docs/ai/MOTION_MACROBLOCKING_INVESTIGATION.md`
  - Identifies motion compensation failure and decode pressure as contributing factors
  - Queued for future PR once code changes are ready
  - Includes evidence from testing logs and recommended mitigation strategies

### Stream Metrics & Network Stability
- [x] **Windowed Mbps Time-Based Formula (Fix #181, PR #183)**
  - Replaced fps/frames-based bitrate calculation with 3-slot time-based ring window (≥100ms per slot)
  - Added byte-counter reset detection to prevent uint64 underflow when frame-processor reallocs its buffer
  - Previously caused ~100 Mbps spikes on buffer reallocation
  - Removed dead `bitrate_window_delta_frames` field from stream_state.h
  - Files modified: `vita/src/host_metrics.c`, `vita/include/stream_state.h`
  - Merged to main in PR #183 (v0.1.783)

- [x] **NET_UNSTABLE Indicator Debounce (Fix #182, PR #183)**
  - Added 500ms debounce to `vitavideo_overlay_show_poor_net_indicator()` via `net_unstable_last_activated_us` field
  - Prevented multiple redundant overlay activation calls during recv bursts (previously fired multiple times per millisecond)
  - Files modified: `vita/src/video_overlay.c`, `vita/include/stream_state.h`
  - Shipped together with Fix #181 in PR #183 (v0.1.783)

- [x] **Bitrate Metrics Hardening & TAKION_A_RWND Experiment Closure (Fix #181 & #182 + RWND A/B, PR #193)**
  - Verified `windowed_bitrate_mbps` calculation (time-based 3-slot ring) printing correct honest bitrate in metrics log
  - On-device validation: measured bitrate now matches windowed_mbps= every cycle (fixes previous 0↔2 Mbps seesawing artifact from frame-count method)
  - A/B tested TAKION_A_RWND reduction: 512KB→256KB, found no latency benefit + 4× more freezes, reverted
  - `lib/src/takion.c` net diff = zero; value remains `0x80000` (512KB final)
  - Bufferbloat investigation concluded; future work tracked under GH #188 (Wi-Fi/jitter-buffer domain)
  - Files modified: (no net changes to source; experimentation validated)
  - Merged to main in PR #193 (v0.1.784)

### Deferred Work
- GH issue #178 (lib-layer gen/reconnect_gen tagging for PIPE/ logs) — moved to backlog after #183; spike Chiaki generation tracking first

---

## 2026-04-14 (UI Text Rendering: Phase 1 Complete)

### Issue #127 Phase 1 - FreeType Glyph Atlas Prewarm
- [x] **FreeType Integration & Glyph Atlas Prewarm**
  - Created modular `vita/include/ui/ui_text.h` and `vita/src/ui/ui_text.c` with prewarm infrastructure
  - Implemented `ui_text_prewarm_glyphs()` to load common ASCII/Latin-1 glyphs during startup
  - Added FreeType metric helper functions (`ui_text_get_metrics()`, baseline calculation, kerning)
  - All 114 existing `vita2d_font_draw_text()` call sites left untouched (Phase 1 scope)
  - Integrated prewarm call into UI initialization path
  - Build Status: Docker build successful, zero compiler warnings
  - Code Review: 8 rounds of substantive feedback, all items addressed and approved
  - Merged: PR #135 landed on main via feature branch `feat/ui-freetype-atlases-127`
  - Module Files: `vita/include/ui/ui_text.h` (~150 lines), `vita/src/ui/ui_text.c` (~250 lines, test-friendly exports)
  - Design: Metric-derived layout foundations enable Phase 2 call-site migration without breaking Phase 1

### Parent Epic #122 - UI Text Rendering Modernization
- Subtask #127 Phase 1 complete; remaining subtasks (#125 mipmaps, #126 target-size icons, #128 antialiased shapes) are in pipeline
- Phase 2 (migrate call sites) queued and depends on Phase 1 prewarm validation

### Pending Validation
- On-device smoke test of Phase 1 prewarm: first-frame glyph pop-in check (never run; Docker build + format-only CI validation completed)

---

## 2025-12-14 (Controller Diagram Visual Fixes)

### Controller Layout Redesign: Final Polish Batch
- [x] **Controller Diagram Shape Fixed**
  - Problem: PS Vita controller drawn as rounded rectangle with 4% corner radius instead of authentic stadium/pill shape
  - Solution: Added procedural stadium shape functions (`draw_stadium_fill()` and `draw_stadium_outline()`)
  - Implementation: Lines 151-231 in `vita/src/ui/ui_controller_diagram.c`
  - Details: Filled body with center rectangle + semicircle ends; outline with 24-segment arcs per semicircle; bounds validation for degenerate shapes; fixed off-by-one pixel error in bottom outline
  - Result: Visually accurate controller shape matching PlayStation design standards

- [x] **Menu Input Bug Fixed**
  - Problem: Triangle button and menu pill touch unresponsive on controller settings screen
  - Solution: Added missing `handle_global_nav_shortcuts()` call in settings screen handler
  - Files Modified: `vita/src/ui/ui_screens.c` (lines 1257-1260)
  - Result: Triangle button and menu pill now properly route input navigation events

### Code Quality & Testing
- All changes build successfully with zero compiler warnings
- Code review completed and approved
- Maintains ≥58 FPS target and draw call budget
- No regression in existing functionality

---

## 2025-12-12 (UI Refactoring All 8 Phases Complete)

### UI Refactoring: Modularization
- [x] **Phase 1: Directory Structure & Headers (260b163)**
  - Created `vita/src/ui/` and `vita/include/ui/` modular architecture
  - Established `ui_constants.h`, `ui_types.h`, `ui_internal.h` for shared state
  - Set up foundation for 8-module refactoring plan
  - Build verification completed

- [x] **Phase 2: Graphics & Animation Extraction (97c4033)**
  - Extracted `ui_graphics.c/h` - drawing primitives (rounded rectangles, circles, shadows, spinner)
  - Extracted `ui_animation.c/h` - easing functions, timing utilities, particle system
  - Updated `ui.c` includes and verified compilation
  - Enabled reuse of graphics primitives across modules

- [x] **Phase 3: Input & State Management (ad89b6d)**
  - Extracted `ui_input.c/h` - button press detection, touch handling, hit testing
  - Extracted `ui_state.c/h` - connection overlay state, cooldowns, text cache, connection thread
  - Reduced `ui.c` by ~360 lines
  - Established clear separation between input handling and state management

- [x] **Phase 4: Reusable Components (d28046e)**
  - Extracted `ui_components.c/h` - toggles, dropdowns, tabs, status dots
  - Extracted error popup, hints popup, debug menu into component module
  - Implemented toggle animation with cubic easing
  - Reduced `ui.c` by ~550 lines
  - Completed 50% of refactoring (4 of 8 phases)

- [x] **Phase 5: Navigation System Extraction (74ce083)**
  - Extracted `ui_navigation.c/h` - wave sidebar, collapse state machine, navigation pills
  - Implemented nav_collapse state management with complete accessor API
  - Created public navigation API in `ui_navigation.h`
  - Built and tested - wave animation and collapse/expand transitions verified
  - Reduced `ui.c` by ~1,100 lines

- [x] **Phase 6: Console Cards Extraction (fd5d1f5)**
  - Extracted `ui_console_cards.c/h` - card rendering, caching, focus animation, host mapping
  - Implemented card selection and scaling with accessor functions
  - Created public cards API in `ui_console_cards.h`
  - Built and tested - focus animation and scaling effects verified
  - Reduced `ui.c` by ~650 lines

- [x] **Phase 7: Screen Implementations Extraction (22bc26d)**
  - Extracted `ui_screens.c/h` - all 9 screen rendering functions
  - Implemented screen dispatch logic with complete interface (main menu, settings, profile, controller config, registration, stream, waking, reconnecting, messages)
  - Created public screens API in `ui_screens.h`
  - Built and tested - all 9 screens render correctly with no regression
  - Reduced `ui.c` by ~2,260 lines (largest extraction)

- [x] **Phase 8: Final Cleanup & Verification (533eccf)**
  - Renamed remaining `ui.c` to `ui_main.c` (580 lines - 88% reduction from original 4,776)
  - Updated CMakeLists.txt with all new source files (8 modules + coordinator)
  - Removed deprecated code and consolidated headers
  - Final verification and regression testing - all functionality intact
  - Completed 100% of refactoring (8 of 8 phases)

### Code Metrics (as of 2025-12-12 - All 8 Phases Complete)
- Original `ui.c`: 4,776 lines
- Final `ui_main.c`: ~580 lines
- **Total lines extracted: ~5,040 lines**
- **Reduction: 88% of original file size in coordinator**
- Modules created: 8 specialized modules + 1 coordinator
  - ui_graphics.c (~520 lines)
  - ui_animation.c (~420 lines)
  - ui_input.c (~300 lines)
  - ui_state.c (~200 lines)
  - ui_components.c (~650 lines)
  - ui_navigation.c (~1,200 lines)
  - ui_console_cards.c (~650 lines)
  - ui_screens.c (~2,260 lines)
  - ui_main.c (580 lines) - Coordinator
- Header files created: 11 total (8 module headers + 3 shared: ui_constants.h, ui_types.h, ui_internal.h)

### Quality Improvements Achieved
- Complete separation of concerns into 8 highly focused modules
- Each module between 200-2,260 lines with clear single responsibility
- Clear, documented public interfaces defined in all header files
- Near-zero global state dependencies in ui_main.c coordinator
- Bottom-up extraction (primitives → utilities → components → higher-level modules) maintained throughout
- 100% functional equivalence with original monolithic ui.c - zero regression
- Clean, maintainable architecture ready for future UI enhancements
- All phase testing completed - all 9 screens verified functional

---

## 2025-11-10 (Latency & Performance Foundation)

### Latency Reduction Initiative
- [x] **Runtime Bitrate & RTT Instrumentation**
  - Added `chiaki_stream_stats_bitrate` sampling in `vita/src/host.c`
  - Implemented gated logging to avoid performance impact
  - Display metrics on profile card in `vita/src/ui.c`
  - Metrics reset on stream stop, update whenever frames arrive
  - Status: In review for code quality and race condition verification

- [x] **Latency Mode Presets (1.2-3.8 Mbps)**
  - Introduced `latency_mode` config setting with UI dropdown
  - Implemented bitrate overrides in `vita/src/host.c` for preset targets
  - Added presets to config serialization
  - Documented options in README
  - Allows users to select Ultra Low → Max bandwidth targets
  - Status: In review for Chiaki profile validation

- [x] **FPS Instrumentation & Client-Side Clamp**
  - Added per-second frame cadence logging in `vita/src/video.c`
  - Stored negotiated FPS in `vita/src/host.c`
  - Implemented "Force 30 FPS Output" toggle for UI and config
  - Drops frames deterministically when PS5 streams 60 fps
  - Keeps 30 fps streams untouched
  - Status: In review for pacing logic verification

- [x] **RP-StartBitrate Handling Update**
  - Modified `lib/src/ctrl.c` to encrypt requested bitrate (not zeros)
  - Gated by new `send_actual_start_bitrate` config flag
  - Enables A/B testing of real StartBitrate payloads
  - Documented in README
  - Status: In review for PS5 handshake compatibility

---

## Earlier Work

### Stream Reliability
- [x] **Stream Retry Cooldown & Input Readiness Gate**
  - Added 3-second cooldown after `CHIAKI_EVENT_QUIT`
  - Require cooldown expiration before `host_stream()` can run again
  - Introduced `inputs_ready` gate: input thread starts on `CHIAKI_EVENT_CONNECTED` (not first video frame)
  - Files: `vita/src/host.c:110-134`, `vita/include/context.h:46-60`, `vita/src/host.c:470-520`

- [x] **Discovery Grace Window**
  - Track `last_discovery_seen_us` for each host
  - Keep entries alive for 3 seconds before pruning
  - Prevents thrashing when consoles momentarily disappear
  - Files: `vita/include/host.h:17-26`, `vita/src/discovery.c:16-191`

- [x] **Takion Queue Monitoring**
  - Restored reorder queue to 64 entries
  - Log high-water usage for validation
  - Supports future adaptive sizing considerations
  - Files: `lib/src/takion.c:46-120`, `lib/src/takion.c:919-1024`

---

## Documentation Updates

- [x] Updated `docs/ai/UI_REFACTOR_SCOPE.md` to v2.0 with Phase 1-4 completion details
- [x] Updated `docs/ai/ROADMAP.md` with new phase structure and completion status
- [x] Updated `PROGRESS.md` with UI refactoring as primary initiative
- [x] Updated `TODO.md` with Phase 5-8 as pending work
- [x] Created this `DONE.md` with completion history

---

## Notes

- All Phase 1-4 work completed without regression in build or functionality
- UI refactoring follows bottom-up approach: primitives → utilities → components → higher-level modules
- Each phase maintains 100% functional equivalence with original code
- Clear interfaces established between modules enable Phase 5-8 to proceed in parallel
- Risk level for Phases 5-8 increases (Medium-High) due to state management complexity
- See `docs/ai/UI_REFACTOR_SCOPE.md` for detailed completion summary and remaining work
