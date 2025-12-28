# Incomplete Features & TODOs

This document tracks all incomplete features, TODOs, stubs, and planned improvements found in the VitaRPS5 codebase.

**Last Updated:** 2025-12-27 (Settings Audit Documentation)
**Status:** Generated from codebase analysis

---

## Critical TODOs

### 1. Latency Optimization
**File:** Multiple (entire streaming pipeline)
**Status:** Known issue - needs improvement
**Priority:** High
**Description:** Current streaming latency is higher than desired, especially on remote connections. Need to optimize the entire video/audio/input pipeline to reduce end-to-end latency.

**Related Issues:**
- Remote connection latency is especially high
- See: https://github.com/ywnico/vitaki-fork/issues/12

**Impact:** Poor user experience, especially for fast-paced games. Critical for remote play viability.

**Areas to investigate:**
- Video decoder optimization
- Audio buffer tuning
- Network protocol efficiency
- Frame pacing improvements
- Input lag reduction

---

### 2. ~~Power Control Thread Initialization~~ ✅ RESOLVED
**File:** `vita/src/main.c:52-55`
**Status:** ✅ Already implemented
**Priority:** N/A
**Description:** Power management is properly configured in `vita_init()` using `scePowerSet*` calls. The TODO was misleading - no separate thread is needed.

```c
scePowerSetArmClockFrequency(444);
scePowerSetGpuClockFrequency(222);
scePowerSetBusClockFrequency(222);
scePowerSetGpuXbarClockFrequency(166);
```

**Resolution:** Removed misleading TODO. Power is configured at startup and works correctly.

---

### 3. ~~Input Thread Initialization~~ ✅ RESOLVED
**File:** `vita/src/host.c:447`
**Status:** ✅ Already implemented
**Priority:** N/A
**Description:** Input thread is properly created when streaming starts using `chiaki_thread_create()`. The TODO was misleading.

```c
err = chiaki_thread_create(&context.stream.input_thread, input_thread_func, &context.stream);
```

**Resolution:** Removed misleading TODO. Input thread is created per-stream and works correctly.

---

### 3. Power Control Configuration
**File:** `vita/src/main.c:149`
**Status:** Not implemented
**Priority:** Medium
**Description:** Power control configuration needs to be implemented.

```c
// TODO: configure power control
```

**Impact:** Power-saving features may not work optimally.

---

### 4. Input Handling Configuration – Immersive Controller Screen
**File:** `vita/src/ui/ui_screens.c`, `vita/src/ui/ui_controller_diagram.c`, `vita/src/ui/ui_top_bar.c`
**Status:** ✅ Completed (2025-12-14, Phase 1, 2, & 3 Complete)
**Priority:** N/A
**Description:** Input handling configuration now available through immersive fullscreen controller layout redesign with procedurally rendered controller diagrams. Users can select from 6 presets (Default, FPS, Racing, Fighting, Remote Play Classic, Custom), view button mappings on interactive Vita controller diagram rendered with vita2d primitives, and prepare for per-button customization.

**Completed Features (Phase 1 - Implementation):**
- Immersive 960×544 fullscreen layout with top bar navigation
- 6 controller presets with full mapping definitions and descriptions
- Interactive Vita controller diagram with front/back outlines
- Three-view system: Summary View (presets) + Front Mapping View + Back Mapping View
- PlayStation Blue tint applied to controller outlines
- Preset cycling via D-pad left/right or touch on preset label
- Front/back view switching via D-pad up/down or touch
- Button mapping callouts with label badges and pulse animation (0.75↔1.0, 1s cycle)
- Legend panel showing current preset mappings
- Menu overlay system with wave navigation from left edge
- Focus manager integration with full touch + controller parity
- 220ms ease-in-out flip animation for diagram view switching
- Color-coded callout chips for unique button features

**Completed Features (Phase 2 - Code Review & Refinements):**
- Color Definition Fixed: Changed color constant to correct `0xFFFF9034` (PlayStation Blue)
- Texture Cleanup: Removed texture loading/cleanup code for PNG assets
- Button Highlight Complete: All button positions properly highlighted
- Magic Numbers Extracted: Created named constants for button positions
- Touch Constants Added: Added TOUCH_PANEL_WIDTH and TOUCH_PANEL_HEIGHT to ui_constants.h for touch input calculations
- TODO Comment Added: Documented show_mapping_popup field for future implementation
- Utility Extraction: `ui_draw_rectangle_outline()` extracted to ui_graphics module for DRY principle compliance
- Eliminated DRY Violation: Refactored shoulder button rendering to use common logic
- Fixed LEFT/RIGHT d-pad input conflict in view toggle to prevent duplicate input processing
- Standardized color constant naming

**Completed Features (Phase 3 - Procedural Rendering):**
- PNG Asset Removal: Completely removed PNG texture dependencies (vita_front_outline.png, vita_back_outline.png)
- Procedural vita2d Rendering: Implemented full procedural drawing system using vita2d primitives
- Ratio-Based Coordinate System: Created ~97 ratio constants in ui_constants.h for pixel-perfect scaling at any diagram size
- Front View Implementation: Device body, screen, D-pad, face buttons (△, ○, ×, □), analog sticks, shoulder buttons, system buttons
- Back View Implementation: Device body, rear touchpad with 4 interactive zones (UL, UR, LL, LR)
- Button ID Mapping: All 16 button IDs fully implemented (DPAD, TRIANGLE, CIRCLE, CROSS, SQUARE, L, R, LSTICK, RSTICK, PS, START, SELECT, RTOUCH_UL, RTOUCH_UR, RTOUCH_LL, RTOUCH_LR)
- Highlight System: Pulsing glow effects for active button/zone indication
- Animation Preservation: Flip (220ms), color tween (300ms), and pulse (1s cycle) animations maintained in procedural system

**Architecture:**
- Modules: `ui_top_bar.c` (~240 lines), `ui_controller_diagram.c` (770 lines, fully procedural)
- No PNG assets required - all graphics computed procedurally from ratio constants
- Constants: ~97 ratio-based values in `ui_constants.h` for automatic scaling
- Modified: `controller.c` (preset definitions), `ui_screens.c` (screen handler), `ui_graphics.c` (utility functions), `main.c` (removed cleanup code), `CMakeLists.txt` (removed PNG packaging)

**Build & Performance:**
- VPK size: 2.6MB (down from 3.9MB with PNG) - 1.3MB reduction (33% smaller)
- Draw call budget: Maintained (diagram still ~10, labels ~15, legend ~10, top bar ~6)
- Target FPS ≥58 maintained
- Zero compiler warnings
- No regression from previous phases
- No external PNG dependencies

**Status:** ✅ Production-ready with pixel-perfect scaling, smaller VPK, and no asset dependencies - Hardware validation pending

---

### 5. Main Cleanup
**File:** `vita/src/main.c:167`
**Status:** Incomplete
**Priority:** Low
**Description:** Cleanup routine may be incomplete.

```c
// TODO: Cleanup
```

**Impact:** Potential memory leaks on exit.

---

## Memory & Performance

### 6. Stack Size Optimization
**File:** `vita/src/main.c:77`
**Status:** Needs review
**Priority:** Medium
**Description:** Stack size may be too large and needs optimization.

```c
// TODO: this is probably way too large
```

**Impact:** Inefficient memory usage.

---

## Configuration & Settings

### 7. Dynamic Version Configuration
**File:** `vita/CMakeLists.txt:5`
**Status:** Hardcoded
**Priority:** Low
**Description:** Version number is hardcoded, should be dynamic.

```cmake
# TODO: Make it dynamic
set(VITA_VERSION "00.06")
```

**Impact:** Manual version updates required for each release.

---

### 8. Registered Host Storage Optimization
**File:** `vita/src/config.c:249`
**Status:** Needs refactoring
**Priority:** Low
**Description:** Consider using pointers instead of copying registered host data.

```c
// copy registered host (TODO for the registered_state, should we use a pointer instead?)
```

**Impact:** Memory efficiency could be improved.

---

### 9. PSTV Touch Support
**File:** `vita/src/config.c:319`
**Status:** Out of scope
**Priority:** N/A
**Description:** Touch support for PlayStation TV needs clarification.

```c
// TODO: Should we? For PSTV?
```

**Impact:** PSTV users may have different experience.
**Note:** ⚠️ PSTV support is **OUT OF SCOPE** for this project. This TODO will remain but not be implemented.

---

## Logging & Debugging

### 10. Configurable Log Level
**File:** `vita/src/config.c:194`, `vita/src/logging.c:12`
**Status:** ✅ Completed (Nov 2025)
**Priority:** Low
**Description:** Logging profiles can now be set via `.env` build profiles and the `[logging]` table in `chiaki.toml`. Defaults live in `vita/src/logging.c`, and runtime parsing happens in `vita/src/config.c`.

**Impact:** Developers and testers can toggle verbose/crash-only logging without editing code.

---

### 11. File Logging
**File:** `vita/src/context.c:30-120`
**Status:** ✅ Implemented (Nov 2025)
**Priority:** Low
**Description:** All runtime logs (LOGD/LOGE + Chiaki callbacks) now stream to `ux0:data/vita-chiaki/vitarps5.log` via a shared helper, so testers can pull a file instead of scraping console output.

---

## Controller & Input

### 12. L2/R2 Trigger Mapping
**File:** `vita/src/host.c:209`
**Status:** Not implemented
**Priority:** Medium
**Description:** Enable using triggers as L2, R2 buttons.

```c
// TODO enable using triggers as L2, R2
```

**Impact:** Limited controller mapping options.

---

### 13. Home Button Handling
**File:** `vita/src/host.c:210`
**Status:** Not implemented
**Priority:** Medium
**Description:** Home button support with long hold sent back to Vita.

```c
// TODO enable home button, with long hold sent back to Vita?
```

**Impact:** Home button functionality limited.

---

### 14. Fully Configurable Controller Mapping
**File:** `vita/src/controller.c:6`
**Status:** Partially implemented
**Priority:** Medium
**Description:** Controller should be fully configurable instead of using controller_map_id.

```c
// TODO make fully configurable instead of using controller_map_id
```

**Impact:** Users cannot customize all button mappings.

---

### 15. Motion Controls (STUB)
**File:** `vita/src/ui.c:1408`
**Status:** Stub only
**Priority:** Low
**Description:** Motion controls UI exists but functionality not implemented.

```c
// TODO(PHASE2-STUB): Motion Controls - Not implemented
```

**Impact:** Motion control features unavailable.

---

## Network & Discovery

### 16. Discovery Error User Feedback
**File:** `vita/src/discovery.c:59`
**Status:** Not implemented
**Priority:** Medium
**Description:** Discovery errors should be indicated to the user.

```c
// TODO: Indicate to user
```

**Impact:** Users unaware of discovery failures.

---

### 17. Manual Host Limit Refinement
**File:** `vita/src/host.c:499`
**Status:** Uses MAX_NUM_HOSTS
**Priority:** Low
**Description:** Manual host limit should have separate constant.

```c
if (context.config.num_manual_hosts >= MAX_NUM_HOSTS) { // TODO change to manual max
```

**Impact:** Manual and discovered hosts share same limit.

---

## UI & Graphics

### 18. Wave Background Animation
**File:** `vita/src/ui.c:512`
**Status:** Removed/Future
**Priority:** Low
**Description:** Wave background animation placeholder for future update.

```c
// TODO: Add wave background animation in future update
```

**Impact:** Static background only.

---

### 19. Coordinate System Validation
**File:** `vita/src/ui.c:814`
**Status:** Needs verification
**Priority:** Low
**Description:** Coordinate systems between vita2d and drawing may not match.

```c
// TODO: Do the coordinate systems really match?
```

**Impact:** Potential rendering misalignment.

---

### 20. Console Icon Tinting
**File:** `vita/src/ui.c:978`
**Status:** Uses separate textures
**Priority:** Low
**Description:** Should use tinting instead of separate textures for console states.

```c
// TODO: Don't use separate textures for off/on/rest, use tinting instead
```

**Impact:** Increased memory usage and asset count.

---

### 21. Manual Host Deletion
**File:** `vita/src/ui.c:1058`
**Status:** Not implemented
**Priority:** Medium
**Description:** Delete from manual hosts functionality missing.

```c
// TODO delete from manual hosts
```

**Impact:** Users cannot remove manual hosts from UI.

---

### 22. Profile Screen Scrolling
**File:** `vita/src/ui.c:2465`
**Status:** Not implemented
**Priority:** Low
**Description:** Profile screen should support scrolling for long content.

```c
// TODO enable scrolling etc
```

**Impact:** Content may be cut off on profile screen.

---

### 23. Connection Abort
**File:** `vita/src/ui.c:2555`
**Status:** Not implemented
**Priority:** Medium
**Description:** Ability to abort connection while connecting.

```c
// TODO abort connection if connecting
```

**Impact:** Users cannot cancel during connection attempt.

---

## Placeholders & Coming Soon

### 24. PSN Profile Picture
**File:** `vita/src/ui.c:1537`
**Status:** Placeholder
**Priority:** Low
**Description:** Profile icon is placeholder until PSN login retrieves actual user avatar.

```c
// Profile icon (placeholder until PSN login retrieves actual user avatar)
```

**Impact:** Generic icon shown instead of user avatar.

---

### 25. Settings Features (Coming Soon)
**File:** `vita/src/ui.c:2025`
**Status:** Placeholder
**Priority:** Low
**Description:** Some settings features marked as "Coming Soon".

```c
UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL, "(Coming Soon)");
```

**Impact:** Limited settings customization.

---

### 26. Rest Mode Assets (Unused)
**File:** `vita/src/ui.c`
**Status:** Cosmetic incomplete
**Priority:** Low
**Description:** `img_ps4_rest` and `img_ps5_rest` textures are loaded at startup but never displayed in the UI. These were intended to show a visual indicator when a console is in Rest Mode state.

**Impact:** No functional impact - purely cosmetic. The textures use memory but serve no purpose until the Rest Mode visual indicator is implemented.

**Notes:**
- Related to console state display in the home screen console cards
- Would complement the existing `img_ps4_on`/`img_ps5_on` and `img_ps4_off`/`img_ps5_off` textures

---

## Thread Management & Resource Cleanup

### Input Thread Finalization
**File:** `vita/src/host.c:1274`, `vita/include/context.h:60`
**Status:** ✅ RESOLVED (2025-12-28)
**Priority:** N/A
**Description:** Input thread now properly exits and is joined when streaming disconnects. Previously, the input thread ran forever with `while(true)`, causing thread leaks and log spam.

**Resolution:** Added `volatile bool input_thread_should_exit` flag to context.h. Input thread now checks this flag in its loop (`while(!stream->input_thread_should_exit)`), allowing graceful shutdown when sessions finalize.

**Impact:** Resolves resource leak (threads never joined) and eliminates log spam after disconnect.

---

### Session Finalization & Resource Cleanup
**File:** `vita/src/host.c:400-431`
**Status:** ✅ RESOLVED (2025-12-28)
**Priority:** N/A
**Description:** Sessions were not being properly finalized when streams disconnect, especially after PS5 sleep mode. Added `finalize_session_resources()` function to handle cleanup.

**Resolution:** New function properly sets exit flag, waits for thread join, and cleans up session state. Called in:
- Quit handler (preserves fast restart/retry capability)
- Retry failure path (Line 316)
- Init failure path (Lines 1669-1674)

**Impact:** Prevents memory leaks from sessions never being finalized. Proper thread synchronization prevents race conditions.

---

### PS5 Sleep/Shutdown Handling
**File:** `vita/src/host.c:240-259`
**Status:** ✅ RESOLVED (2025-12-28)
**Priority:** N/A
**Description:** When PS5 entered sleep mode, the VitaRPS5 app showed "PS5 is unreachable" error instead of a friendly message.

**Resolution:** Improved disconnect messages to distinguish graceful shutdowns from errors using `chiaki_quit_reason_is_error()`. PS5 sleep now shows "Console entered sleep mode" instead of error message.

**Impact:** Better user experience - users understand that sleep is intentional, not a failure. Also improved input thread cleanup during this scenario.

---

## Code Cleanup Notes

This section documents dead code that has been removed and the rationale behind its removal.

### disconnect_action Config Field (Removed)
**Status:** ✅ Removed (2025-12-27)
**Original Files:** `vita/src/config.c`, `vita/include/config.h`
**Description:** The `disconnect_action` configuration field was designed to control console behavior when a Remote Play stream disconnects. Supported values were:
- `DISCONNECT_ACTION_REST` - Put console in Rest Mode
- `DISCONNECT_ACTION_NOTHING` - Leave console running
- `DISCONNECT_ACTION_ASK` - Prompt user for action

**Reason for Removal:** This feature was never implemented beyond the config infrastructure:
- No runtime code checked or used the value
- No UI controls existed to configure it
- The PS5/PS4 Remote Play protocol handles disconnect behavior server-side

**Impact:** Reduces code complexity and config file clutter. No functionality was lost since the feature never worked.

---

## Design Notes & Clarifications

This section clarifies intentional design decisions that may appear as limitations.

### FPS Behavior (Intentional)
**Status:** By Design
**Related Files:** `vita/src/video.c`, `vita/src/ui.c` (Settings)
**Description:** The "Force 30 FPS Output" setting in Video Settings may cause confusion. Here's how FPS handling actually works:

**Technical Details:**
- **PS5 always streams at 60fps** regardless of what the client requests in the initial handshake
- The `fps_30` config option does NOT request 30fps from the console
- Instead, the Vita client uses **local frame pacing** to drop every other frame, achieving 30fps display
- This is controlled by the "Force 30 FPS Output" toggle in Settings > Video

**Why this exists:**
- The PS Vita hardware benefits from 30fps rendering due to CPU/GPU constraints
- Lower frame rate reduces decode workload and can improve streaming stability
- Users can disable this to receive full 60fps if their network/hardware supports it

**This is NOT a bug** - it's an intentional performance optimization for the Vita platform.

---

## Summary by Priority

### High Priority (1 item)
1. **Latency Optimization** - Critical for user experience

### Medium Priority (8 items)
4. Power Control Configuration
5. Input Handling Configuration
6. Stack Size Optimization
7. L2/R2 Trigger Mapping
8. Home Button Handling
9. Fully Configurable Controller Mapping
10. Discovery Error User Feedback
11. Manual Host Deletion
12. Connection Abort

### Low Priority (15 items)
13. Main Cleanup
14. Dynamic Version Configuration
15. Registered Host Storage Optimization
16. Configurable Log Level
17. File Logging
18. Motion Controls
19. Manual Host Limit Refinement
20. Wave Background Animation
21. Coordinate System Validation
22. Console Icon Tinting
23. Profile Screen Scrolling
24. PSN Profile Picture
25. Settings Features (Coming Soon)
26. Rest Mode Assets (Unused)

### Out of Scope (1 item)
- PSTV Touch Support

---

## Future Enhancements

These are ideas for future development that would enhance the user experience but are not currently planned for immediate implementation.

### In-Stream Overlay Menu
**Priority:** Medium
**Description:** Add an overlay menu accessible during streaming that allows users to adjust settings without disconnecting.

**Current Behavior:**
- During streaming, all controller inputs go directly to the PS5/PS4
- The only way to access the VitaRPS5 menu is to hold **L + R + Start** for ~1 second to stop the stream
- No on-screen HUD or overlay exists during streaming

**Proposed Enhancement:**
- Add a button combo (e.g., **L + R + Triangle**) to toggle an overlay menu during streaming
- Overlay would pause input forwarding while open
- Quick access to commonly changed settings:
  - Fill Screen toggle
  - Latency Mode adjustment
  - Show/Hide Latency stats
  - Disconnect option
- Semi-transparent overlay that doesn't fully obscure the stream

**Implementation Notes:**
- Would require changes to `vita/src/host.c` input thread to intercept combo
- New overlay rendering in `vita/src/video.c` or separate overlay module
- Need to handle input state carefully to avoid sending menu inputs to console

---

## Notes

- This document is auto-generated from code analysis
- Each item links to specific file locations for easy reference
- Priority levels are estimates based on impact and user-facing visibility
- Some TODOs may be resolved in active branches not yet merged

---

**Next Steps:**
1. Review each item and validate priority
2. Create GitHub issues for trackable items
3. Assign to milestones/releases
4. Update this document as items are completed
