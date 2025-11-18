# Incomplete Features & TODOs

This document tracks all incomplete features, TODOs, stubs, and planned improvements found in the VitaRPS5 codebase.

**Last Updated:** 2025-10-01
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

### 4. Input Handling Configuration
**File:** `vita/src/main.c:151`
**Status:** Not implemented
**Priority:** Medium
**Description:** Input handling configuration is incomplete.

```c
// TODO: configure input handling
```

**Impact:** Input configuration options unavailable to users.

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
**File:** `vita/src/context.c:45`
**Status:** Not implemented
**Priority:** Low
**Description:** Log level should be configurable from config file.

```c
// TODO: Load log level from config
```

**Impact:** Users cannot adjust logging verbosity.

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

### Low Priority (14 items)
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

### Out of Scope (1 item)
- PSTV Touch Support

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
