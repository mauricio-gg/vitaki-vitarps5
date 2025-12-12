## PROGRESS · Roadmap & Epics

Last Updated: 2025-12-12

A living reference for larger initiatives. Update this document when we start or finish an epic, or when priorities shift.

---

### 1. UI Refactoring: Modularization *(In Progress - 50% Complete)*
- **Objective:** Split monolithic `vita/src/ui.c` (4,776 lines) into 8 focused modules with clear interfaces and reduced coupling.
- **Progress:** Phases 1-4 complete; Phases 5-8 pending
- **Completed Work:**
  1. Phase 1: Directory structure and headers (260b163)
  2. Phase 2: Graphics and animation extraction (97c4033)
  3. Phase 3: Input and state management (ad89b6d, ~360 lines removed)
  4. Phase 4: Reusable components extraction (d28046e, ~550 lines removed)
- **Current ui.c Size:** ~3,500 lines (down from 4,776)
- **Modules Created:** 5 (graphics, animation, input, state, components) + 5 headers
- **Remaining Phases:**
  1. Phase 5: Navigation system extraction (wave sidebar, collapse state)
  2. Phase 6: Console cards extraction (rendering, caching, focus animation)
  3. Phase 7: Screen implementations extraction (all 9 screens)
  4. Phase 8: Final cleanup and verification
- **Estimated Remaining Effort:** 4-5 work sessions
- **Risk Level:** Medium (navigation and screens require careful state wiring)

### 2. Latency Reduction Initiative *(Active)*
- **Objective:** Bring Vita remote-play latency below 50 ms local / 100 ms remote by improving PS5 negotiation, Vita scheduling, and Wi-Fi guidance.
- **Current Focus:** Instrumentation + bitrate handshake research (`docs/LATENCY_ANALYSIS.md`).
- **Status:** In review - multiple latency improvements pending review (bitrate metrics, FPS instrumentation, StartBitrate handling)
- **Upcoming Milestones:**
  1. Capture requested vs. actual bitrate and timing metrics on hardware.
  2. Patch `RP-StartBitrate`/LaunchSpec to request sub-3 Mbps streams reliably.
  3. Surface user-facing controls/documentation for low-bandwidth mode.

### 3. Build & Release Reliability *(Ongoing)*
- **Objective:** Keep Docker-based builds, automated releases, and deployment scripts stable.
- **Focus Areas:** Monitor GitHub Actions release workflow, ensure `./tools/build.sh` handles new assets/configs, keep documentation current.
- **Status:** Stable - new modular UI structure integrated into build system

---

### Status Log
| Date | Update | Owner |
|------|--------|-------|
| 2025-12-12 | Phase 4 complete - UI refactoring 50% done (910 lines extracted) | @mauricio-gg |
| 2025-12-12 | Phases 1-4 UI refactoring documented in UI_REFACTOR_SCOPE.md v2 | Documentation |
| 2025-12-12 | ROADMAP.md updated - Phase structure reorganized around modularization | Documentation |
| 2025-12-11 | Phase 2 complete - Graphics and animation modules extracted | @mauricio-gg |
| 2025-12-10 | Phase 1 complete - UI modular structure established | @mauricio-gg |
| 2025-11-10 | Latency metrics and presets implementation in review | Codex agent |
| 2025-11-10 | Created roadmap structure (Latency, UI Phase 3, Build reliability) | Codex agent |

Add entries whenever milestones move forward or new initiatives start.
