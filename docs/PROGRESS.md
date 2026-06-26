# Project Progress

Last Updated: 2026-06-27

## Current Focus
Active investigation into streaming robustness following PR #196 (dead-stream watchdog) reversion due to critical regression on hardware (torn 64-bit read on ARMv7, RP_IN_USE reconnect race). PR #195 (reorder queue HOL fix) merged successfully and stable. Next steps: (1) lib-side transport-layer suspend detection to replace reverted app-level watchdog, (2) investigate 20% sustained packet-loss → 1500 kbps PS5 bitrate throttle observed in field logs. Both investigations active; controller layout hardware validation deferred until streaming quality stabilizes.

## Project: VitaRPS5 FPS Health Investigation

### Epic: Motion Macroblocking Fix (Reference Array Alignment + IDR Recovery) – Complete
**Goal:** Eliminate P-frame reference chain collapse artifacts reported by Reddit user during network instability

**Status:** Complete (v0.1.787) - Both tasks merged, on-device validation confirmed

#### Completed Tasks
- [x] **Task 1: Reference Array Size Alignment** (PR #186)
  * Reduced `ChiakiVideoReceiver.reference_frames[]` from 16 → 8 slots to match Vita H.264 decoder DPB capacity
  * Prevents reference tracking thrashing during cascade events
  * Merged, validated

- [x] **Task 2: IDR Request on First Missing Ref** (PR #187)
  * Added `video_receiver_maybe_request_idr()` call in !recovered block of `chiaki_video_receiver_flush_frame()`
  * Changed trigger from cascade-depth-3 to immediate request on first unrecoverable missing P-frame ref
  * Reduces cascade recovery time from ~400ms to ~200ms (saves ~6 frames per event)
  * On-device validation: "Requesting IDR (missing_ref)" logs fire at cascade=1 correctly
  * No IDR flooding observed
  * Cascade backstop (depth 3) working as fallback for rapid bursts
  * Merged, validated

**Version:** v0.1.787 (both PRs included)

**Branch:** motion-macroblocking-fix (merged to main)

---

### Epic: Network-Driven P-Frame Reference Chain Collapse (FPS Drops Root Cause) – In Progress
**Goal:** Reduce FPS drop severity and recovery time during network packet loss cascade events (P0/P1a/P1b framework); investigate sustained loss scenarios and session freeze recovery

**Status:** In Progress - Motion macroblocking fix merged, P0 & P1a Implemented and Working, P1b Disabled, New Investigations Opened (PR #196 reversion, sustained-loss bitrate floor)

#### Root Cause Analysis (Completed)
- **Symptom:** FPS drops from 30 to 18-22 during Wi-Fi instability
- **Frequency:** Cascade events triggered by network packet loss
- **Investigation:** Decode time 1.7ms avg (not bottleneck); issue is H.264 inter-prediction dependency cascade
- **Root Cause:** Wi-Fi packet loss → FEC failure → missing I-frame → all subsequent P-frames fail (missing reference)
- **Evidence:** Session 3 (smooth 30fps, 0 missing_ref) vs Session 1 (degraded 21fps, 36 missing_ref)
- **PS5 Limitation:** Encoder ignores congestion control feedback; does not adaptively reduce bitrate

#### Completed Tasks
- [x] **P0: Faster IDR Recovery** (`lib/src/videoreceiver.c:18-19`)
  * Reduced `IDR_REQUEST_COOLDOWN_MS` from 200ms to 100ms
  * Reduced `IDR_REQUEST_TIMEOUT_MS` from 2000ms to 1000ms
  * Reduces cascade recovery time from ~400ms to ~200ms (saves ~6 frames per cascade)

- [x] **P1a: Cascade Decode Skip** (`lib/src/videoreceiver.c:320-342`)
  * Skip HW decode on 3+ consecutive missing-ref frames
  * Saves ~55ms per skipped frame (decoder processes garbage during cascade)
  * Display last good frame until next I-frame arrives
  * Prevents Takion thread blocking on wasted decoder work

- [x] **P1b: CASCADE_ALARM Soft Restart (DISABLED)** (`vita/src/host.c:962`)
  * Feature implemented but disabled via comment-out
  * Hardware testing showed consistent Takion v12 handshake failures
  * Killed playable 21 FPS sessions instead of recovering them
  * All diagnostic infrastructure preserved for future investigation

#### Completed Tasks (Step 2: Compile Flag Instrumentation)
- [x] **Instrumented lib/ files (9 source + 6 headers)** - All streaming modifications wrapped with conditional compilation
  * `lib/src/takion.c` – Adaptive jitter buffer, reorder queue (256→16), A_RWND (512KB→100KB)
  * `lib/src/videoreceiver.c` – IDR cooldown (200→100ms), cascade skip, gap reporting, cadence measurement
  * `lib/src/videoreceiver_gap.c/h` – New gap measurement module
  * `lib/src/streamconnection.c`, `session.c`, `frameprocessor.c`, `feedbacksender.c`, `takionsendbuffer.c`, `ctrl.c`
- [x] **Instrumented vita/src/host.c (lines 254-3014)** – All streaming functions + 40+ helpers behind flag
- [x] **Updated CMakeLists.txt** – Added `option(VITARPS5_ENHANCED_RECOVERY ... OFF)` for baseline control
- [x] **Updated tools/build.sh** – Added `--enhanced-recovery` flag support
- [x] **Build verification** – Both builds compile clean (28KB delta, no errors)

#### Pending Tasks (Step 3: Hardware Testing)
- [ ] **Deploy baseline build** – Build with flag OFF, deploy to PS Vita via FTP
- [ ] **Baseline test:** Stream at normal distance, verify stable 30fps (capture baseline metrics)
- [ ] **Deploy enhanced build** – Build with flag ON (--enhanced-recovery), deploy to same Vita
- [ ] **Degraded test:** Move 2x distance from router, measure cascade recovery time improvement (compare metrics)
- [ ] **Measurement:** Compare time from first missing_ref to FPS recovery (before/after)
- [ ] **Verify:** No excessive soft restarts or bitrate oscillation
- [ ] **Document:** Final FPS improvement metrics and cascade event frequency reduction

#### Deferred Tasks
- [ ] **P1b: Investigate Takion v12 Handshake Failure** (pending root cause analysis)
  * CASCADE_ALARM soft restarts fail at handshake stage
  * Need to determine if issue is network, timing, or state management
  * Re-enable feature once handshake reliability improved

- [ ] **P2: Congestion Control Self-Adaptation** (deferred pending P0+P1a validation)
  * PS5 ignores congestion feedback; Vita must self-adapt
  * Trigger soft restart from congestion control thread on sustained loss
  * More responsive than waiting for video-level metric degradation

- [ ] **P3: RSSI Trend Tracking + Pre-emptive Action** (diagnostic enhancement)
  * Track Wi-Fi RSSI 3-point moving average
  * Pre-emptively request IDR + reduce bitrate on RSSI drop >10% in 2s
  * Predictive rather than reactive; prevent cascade before it happens

**Branch:** `feat/fps-health-investigation`

**Build Verification:**
- [x] `./tools/build.sh --env testing` succeeded (logging enabled for diagnostics)
- [x] P0 and P1a improvements implemented and active
- [x] P1b CASCADE_ALARM disabled cleanly (single-line comment-out)
- [ ] Full hardware validation pending (baseline + degraded Wi-Fi tests)

**Commit Status:**
- Latest commit: c0630eb "Add FPS health investigation: root cause analysis and improvement plan"
- P0/P1a/P1b changes committed and documented
- P1b disabled in follow-up change based on hardware test results

**Recent Changes (2026-06-26):**
- PR #195 (reorder queue HOL fix) merged successfully; stable improvement confirmed
- PR #196 (dead-stream watchdog) reverted due to critical ARMv7 regression (torn 64-bit read, RP_IN_USE race)
- New investigations opened: lib-side transport detection, sustained-loss bitrate floor analysis

**Reference Documents:**
- `docs/ai/FPS_HEALTH_INVESTIGATION.md` – Root cause analysis, improvement plan, verification steps
- `docs/LATENCY_ANALYSIS.md` – Historical latency investigation context
- `docs/WIFI_OPTIMIZATION.md` – User-facing Wi-Fi guidance (consistent with findings)
- `docs/DEPRECATED.md` – Detailed context on PR #196 reversion and forward direction

---

### Epic: Streaming Robustness & Recovery – Investigations Open
**Goal:** Improve session freeze recovery and packet-loss resilience; move from app-level monitoring to transport-layer detection

**Status:** Investigation Phase – Two parallel tracks

#### Investigation 1: Lib-Side Suspend/Resume Detection
**Context:** PR #196 (app-level watchdog) reverted due to ARMv7 64-bit read safety issue. Need to move detection to transport layer.

**Pending Tasks:**
- [ ] Analyze Takion send path for ENOBUFS/EBADF escalation (`lib/src/takion.c`)
- [ ] Review streamconnection.c DISCONNECT-during-streaming path (currently line ~435, STATE_TAKION_CONNECT gate)
- [ ] Design clean session teardown before PS5 releases old session
- [ ] Implement transport-layer socket monitoring

**Evidence:**
- Log 20639381559 (PR #196 regression: torn 64-bit read, 18446744073709550 ms false stall)
- Hardware A/B test confirmed false positive on healthy 30 fps stream

#### Investigation 2: Sustained Loss → PS5 Bitrate Floor
**Context:** Field logs show 20% sustained packet loss → 1500 kbps encoder floor (user reports lag onset at "Network unstable" threshold).

**Pending Tasks:**
- [ ] Evaluate Vita jitter-buffer ceiling (40ms, commit a826396) — is it too tight for bursty loss?
- [ ] Verify loss-rate calculation (PR #191) — is capped-loss reporting masking true loss?
- [ ] Profile recv path improvements (Takion RX threading, buffer alignment)
- [ ] Determine if graceful degradation messaging is possible when bitrate floor hits

**Evidence:**
- Log 20361349999, 20639381559: 66+ capped-loss events, PS5 encoder sustained at 1500 kbps
- User impact: Noticeable lag as soon as loss sustained at ~20%

**Branch(es):** Investigation phase; no branch yet (waiting for findings)

---

## Project: VitaRPS5 Main Screen Modernization

### Epic: Production Build Logging Security – Complete
**Goal:** Ensure production builds fail-safe with minimal logging configuration; prevent insecure defaults when build system configuration chain fails

**Status:** Completed (2025-12-28)

#### Completed Tasks
- [x] Fixed fallback defaults in logging.c - Changed from DEBUG-friendly (logging enabled, standard profile) to production-safe (logging disabled, errors-only)
- [x] Implemented fail-safe security model - Production builds now default to minimal logging instead of verbose debugging
- [x] Added compile-time warnings - Warns when CMake logging flags missing from build system
- [x] Added runtime configuration logging - Logs active configuration during initialization for auditability
- [x] Fixed initialization bug - Line 336 condition prevents unnecessary resource allocation in production mode
- [x] Verified build system chain - Confirmed CMake integration correctly applies production vs testing configurations
- [x] Tested both build modes - Production (`./tools/build.sh`) and testing (`./tools/build.sh --env testing`) verified working

**Key Benefits:**
- Security improved: Production builds can no longer silently fall back to verbose logging
- Configuration risk eliminated: Fallback defaults are now production-safe (fail-safe not fail-open)
- Build chain transparency: Compile-time warnings alert developers if CMake flags missing
- Auditability: Runtime logging shows which configuration profile is active

**Files Modified:**
- `vita/src/logging.c` – Fallback defaults and initialization

**Reference:** `docs/ai/LOGGING_DEFAULTS_FIX.md` (detailed technical analysis and rationale)

---

### Epic: Graceful PS5 Sleep/Shutdown Handling – Complete
**Goal:** Handle PS5 sleep/shutdown events gracefully with clean thread management and appropriate user messaging

**Status:** Completed (2025-12-28) with Critical Bug Fix

#### Completed Tasks
- [x] Graceful PS5 sleep mode handling - Console sleep displays "Console entered sleep mode" instead of error
- [x] Input thread exit mechanism - Added `volatile bool input_thread_should_exit` flag to enable clean thread shutdown
- [x] Session finalization function - Created `finalize_session_resources()` for proper cleanup and thread joining
- [x] Thread safety fixes - Resolved race condition (unsynchronized flag access) and TOCTOU vulnerability
- [x] Improved disconnect messages - Distinguished graceful shutdowns from errors using `chiaki_quit_reason_is_error()`
- [x] Finalization on all exit paths - Applied cleanup to quit handler, retry failure, and init failure paths
- [x] Session thread self-join bug fix - Removed invalid self-join from finalize_session_resources(); session thread now exits naturally (Critical Bug: error code 3)

**Key Benefits:**
- PS5 sleep/shutdown now shows friendly message instead of error
- Input thread properly exits instead of running forever
- No more log spam after disconnect (threads properly joined)
- Memory leaks prevented through proper session finalization
- Thread safety improved (race condition and TOCTOU vulnerability fixed)
- All disconnect scenarios handled gracefully

**Problems Solved:**
- User Issue: PS5 sleep showed "PS5 is unreachable" error
- Resource Leak: Input thread never exited (infinite `while(true)` loop)
- Resource Leak: Sessions never finalized preventing proper cleanup
- Thread Safety: Unsynchronized flag access caused race conditions
- Thread Safety: TOCTOU vulnerability in thread join logic

**Files Modified:**
- `vita/include/context.h` – Added input_thread_should_exit flag
- `vita/src/host.c` – Finalization function, improved messages, thread exit mechanism

**Reference:** `docs/INCOMPLETE_FEATURES.md` (Thread Management & Resource Cleanup section)

---

### Epic: Controller Layout Redesign – Complete
**Goal:** Deliver immersive fullscreen controller configuration screen with top bar, preset system, and interactive Vita diagram

**Status:** Completed (All 8 implementation tasks done)

#### Completed Tasks
- [x] Immersive layout framework - Navigation system now supports overlay mode with custom top bars
- [x] Top bar component - 52px bar with Menu button, preset selector, view toggle (front/back/both)
- [x] Preset model and cycling - 6 presets with ControllerPresetDef struct; D-pad/touch switching with instant updates
- [x] Vita diagram renderer - Programmatic 720×360 controller diagram with front/back/both views and button callouts
- [x] Legend and mapping modal - Right-column mapping display with pulse animation on button highlights
- [x] Menu overlay system - 250px slide-in wave navigation from left edge with modal focus trapping
- [x] Animation implementations - Front/back flip (220ms), button callout pulse (0.75↔1.0 alpha, 1s cycle)
- [x] Focus manager integration - Full touch + controller parity with clean navigation loops (Menu → Preset → Toggle → Legend)

**Key Benefits:**
- Maximizes canvas space (960×544 vs 830×544 constrained layout)
- Provides intuitive preset cycling for different play styles
- Maintains brand consistency with top bar + overlay wave navigation
- Clean focus zone architecture reuses centralized manager
- Supports future immersive screens via reusable top bar component

**Performance:**
- Draw calls: ~55 total (well within budget, ≥58 FPS target)
- Animation smoothness verified (220ms flip, 1s breathing cycle)
- No regression from previous implementations

**Files Created:**
- `vita/include/ui/ui_top_bar.h`
- `vita/src/ui/ui_top_bar.c`
- `vita/include/ui/ui_controller_diagram.h`
- `vita/src/ui/ui_controller_diagram.c`

**Files Modified:**
- `vita/include/ui/ui_constants.h`, `ui_types.h`, `ui_focus.h`, `ui_navigation.h`
- `vita/src/ui/ui_navigation.c`, `ui_screens.c`
- `vita/include/controller.h`, `vita/src/controller.c`
- `vita/CMakeLists.txt`

**Reference:** `docs/ai/SCOPING_CONTROLLER_LAYOUT_REDO.md`

**Remaining Tasks:**
- [ ] Hardware validation (focus loops, animation performance, overlay smoothness)
- [ ] Preset persistence across app restarts
- [ ] Edge case testing (deep focus stacking with other modals)

---

### Epic: Centralized Focus Manager – Complete
**Goal:** Implement single-owner focus state system to eliminate d-pad input double-processing bugs and provide clean input routing architecture

**Status:** Completed

#### Completed Tasks
- [x] Created focus manager foundation (`ui_focus.h`, `ui_focus.c`)
- [x] Defined 6 distinct focus zones (NAV_BAR, MAIN_CONTENT, SETTINGS_ITEMS, PROFILE_CARDS, CONTROLLER_CONTENT, MODAL)
- [x] Migrated navigation module to report focus through manager
- [x] Migrated all screen handlers (main, settings, profile, controller) for zone-aware input
- [x] Removed legacy `current_focus` global variable
- [x] Implemented modal focus stack (max depth 4) for dialog focus trapping
- [x] Integrated zone-crossing (LEFT/RIGHT) input handling in main loop

**Key Benefits:**
- Eliminates d-pad input double-processing that caused erratic navigation
- Single owner of focus state prevents conflicts
- Modal stack prevents input leakage when dialogs are active
- Clean separation: nav owns nav state, screens own content state, manager owns transitions
- Each input processed exactly once per frame

**Architecture Notes:**
- `ui_focus_handle_zone_crossing()` called first in input handling pipeline
- Screen handlers only process intra-zone UP/DOWN input
- Modal push/pop operations manage focus context for dialogs
- Legacy `FOCUS_COMPAT_*` macros available for gradual migration

**Files Created:**
- `vita/include/ui/ui_focus.h`
- `vita/src/ui/ui_focus.c`

---

### Epic: Wave Navigation & Icon System – Complete
**Goal:** Replace legacy sidebar with modern wave-animated navigation matching PlayStation design language

**Status:** Completed

#### Completed Tasks
- [x] Wave texture animation (top/bottom, sinusoidal ±3px movement)
- [x] Icon bobbing synchronized with wave phase
- [x] Procedural icon fallbacks (play, settings, controller, profile)
- [x] Texture-based icon rendering (~229 draw call reduction)
- [x] Navigation separator line (1px solid)

**Performance:** 4 draw calls total (wave_top, wave_bottom, icons, highlight) vs previous 600+ procedural calls

---

### Epic: Collapsible Navigation Bar – Complete
**Goal:** Deliver a collapsible pill-style navigation sidebar with proper state management, visual polish, and input parity

**Status:** Completed with Full Polish

#### Completed Tasks
- [x] Nav shortcuts on main menu (Triangle button and D-pad navigation)
- [x] Default collapsed state (pill shown by default vs expanded menu)
- [x] Proper screen reset behavior (nav_reset_to_collapsed() implementation)
- [x] Full-screen gradient background coverage (extends behind nav area)
- [x] Enlarged pill sizing (140x44px to eliminate rounded corner jagging)
- [x] Dynamic horizontal centering - Content centers based on full screen width during collapse/expand transitions
- [x] Pill content centering - Hamburger icon and "Menu" text perfectly centered with equal padding
- [x] Collapse trigger refinement - Nav icons no longer accidentally trigger collapse; proper focus detection
- [x] Full-screen gradient completion - Background gradient covers entire screen, eliminating visual gaps
- [x] Focus overlay render order - Overlay positioned after content for correct visual layering; increased opacity
- [x] Nav icon touch guard - Added touch_block to prevent icon taps from triggering collapse
- [x] Page titles full-screen centering - Settings screen titles center on full 960px width, not content area
- [x] Hints popup system - Select-triggered modal hints that fade after 7 seconds; "(Select) Hints" indicator added

**Context:** Originally implemented in commit 932e63f. Fully polished through 13 total refinements across three update passes (2025-12-11 through 2025-12-12), addressing UX flow, visual artifacts, layout precision, input detection, state consistency, and accessibility.

**Files Modified:**
- `vita/src/ui.c` – Nav pill state, screen reset, gradient rendering, pill dimensions, content centering, overlay rendering

---

### Epic: Console Card System – Complete
**Goal:** Modernize console selection cards with rounded corners, focus animations, and proper typography

**Status:** Completed

#### Completed Tasks
- [x] Card size increase to 300x250px with 12px corner radius
- [x] Focus scale animation (0.95→1.0, 180ms ease-out)
- [x] Outline glow effect (#3490FF, 2px stroke when focused)
- [x] Card centering within 830px content area
- [x] PS5 logo sizing (native aspect ratio, max 180px width, 24px top padding)

**Typography:** 32px bold title, 16px secondary text, baseline grid aligned

---

### Epic: Particle Background System – Complete
**Goal:** Replace static gradient background with layered PlayStation symbol particles

**Status:** Completed

#### Completed Tasks
- [x] Parallax layer implementation (0.7x background, 1.0x foreground)
- [x] Horizontal sway animation (±2px sinusoidal)
- [x] Particle struct extended (layer field, sway offset)
- [x] Optimized count (8 particles, 30fps update frequency)

**Performance:** Maintains ≥60 FPS during idle screen

---

### Epic: Micro-Animations & Polish – In Progress
**Goal:** Add low-cost visual feedback animations (breathing, transitions, panel slides)

**Status:** Partially Complete

#### Completed Tasks
- [x] Status dot breathing animation (0.7→1.0 alpha, 1.5s cycle)
- [x] Deferred panel transitions (awaiting next phase)

#### Pending Tasks
- [ ] Panel transition animations (120ms slide + fade)
- [ ] Instruction bar with DualShock glyphs
- [ ] Touch input parity verification

---

### Epic: Remaining Tasks – Backlog
**Goal:** Complete feature set for main screen and subsequent screens

**Status:** Planned

#### Tasks
- [ ] Power menu implementation
- [ ] Input configuration UI
- [ ] Manual host deletion flow
- [ ] Hardware validation on PS Vita device
- [ ] Video capture for FPS verification

---

## Architecture Notes

**Draw Call Budget:** Target <80 total; current usage ~40 (sidebar 4, cards 3, particles 12, text ~10) leaves headroom for additional UI elements

**Animation Baseline:** Delta-based frame-independent timing via `chiaki_time_now()` for all tweens

**File References:**
- Implementation: `vita/src/ui.c`
- Specifications: `docs/ai/UI_FINAL_SPECIFICATION.md`
- Scoping: `docs/ai/SCOPING_UI_POLISH.md`
