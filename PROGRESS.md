## PROGRESS · Roadmap & Epics

Last Updated: 2025-12-12 (UI Refactoring Complete - All 8 Phases Finished)

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
