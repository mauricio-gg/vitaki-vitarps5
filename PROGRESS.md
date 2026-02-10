## PROGRESS · Roadmap & Epics

Last Updated: 2026-02-10 (Settings resolution policy clarity follow-up)

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

### 3. Controller Layout Redesign *(COMPLETE ✅)*
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

### 4. Build & Release Reliability *(Ongoing)*
- **Objective:** Keep Docker-based builds, automated releases, and deployment scripts stable.
- **Focus Areas:** Monitor GitHub Actions release workflow, ensure `./tools/build.sh` handles new assets/configs, keep documentation current.
- **Status:** Stable - new modular UI structure integrated into build system, assets pipeline validated

---

### Status Log
| Date | Update | Owner |
|------|--------|-------|
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
