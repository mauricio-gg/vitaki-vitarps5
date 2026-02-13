# VitaRPS5 UI Migration Roadmap

## Project Goal
Transform vitaki-fork's basic UI into vitarps5's professional PlayStation-inspired interface while preserving the working streaming functionality.

## Development Phases

### Phase 1-8: Complete UI Refactoring (Modularization) - COMPLETE ✅
**Goal**: Split monolithic ui.c (4,776 lines) into 8 focused, reusable modules with clear interfaces

#### Phase 1: Framework Setup ✅ (Commit 260b163)
- [x] Create vita/src/ui/ and vita/include/ui/ directories
- [x] Create ui_constants.h, ui_types.h, ui_internal.h headers
- [x] Set up modular architecture foundation
- [x] Build verification completed

#### Phase 2: Graphics & Animation Extraction ✅ (Commit 97c4033)
- [x] Extract ui_graphics.c - drawing primitives
- [x] Extract ui_animation.c - easing and particles
- [x] Update ui.c includes
- [x] Build and test passed

#### Phase 3: Input & State Management ✅ (Commit ad89b6d)
- [x] Extract ui_input.c - button/touch input handling
- [x] Extract ui_state.c - connection overlay, cooldowns, text cache
- [x] Reduce ui.c by ~360 lines
- [x] Build and test passed

#### Phase 4: Reusable Components ✅ (Commit d28046e)
- [x] Extract ui_components.c - toggles, dropdowns, tabs, dialogs
- [x] Extract error/hints popups and debug menu
- [x] Implement toggle animation with cubic easing
- [x] Reduce ui.c by ~550 lines
- [x] Build and test passed

#### Phase 5: Navigation System Extraction ✅ (Commit 74ce083)
- [x] Extract ui_navigation.c - wave sidebar, collapse state machine, navigation pills
- [x] Implement nav_collapse state management with full accessor API
- [x] Create public navigation API in ui_navigation.h
- [x] Build and test - wave animation and collapse/expand transitions verified
- [x] Reduce ui.c by ~1,100 lines

#### Phase 6: Console Cards Extraction ✅ (Commit fd5d1f5)
- [x] Extract ui_console_cards.c - card rendering, cache, focus animation
- [x] Implement card selection and scaling with accessor functions
- [x] Create public cards API in ui_console_cards.h
- [x] Build and test - focus animation and scaling effects verified
- [x] Reduce ui.c by ~650 lines

#### Phase 7: Screen Implementations Extraction ✅ (Commit 22bc26d)
- [x] Extract ui_screens.c - all 9 screen rendering functions
- [x] Implement screen dispatch logic with complete interface
- [x] Create public screens API in ui_screens.h
- [x] Build and test - all 9 screens render correctly with no regression
- [x] Reduce ui.c by ~2,260 lines (largest extraction)

#### Phase 8: Final Cleanup & Verification ✅ (Commit 533eccf)
- [x] Rename remaining ui.c to ui_main.c (580 lines - 88% reduction)
- [x] Update CMakeLists.txt with all new source files (8 modules + coordinator)
- [x] Remove deprecated code and consolidate headers
- [x] Final verification and regression testing - all functionality intact

### Phase 9: Controller Layout Redesign *(COMPLETE ✅)*
**Goal**: Redesign the controller configuration screen with an immersive visual layout featuring procedurally rendered Vita controller diagrams and interactive button mapping views.

#### Phase 1: Immersive Layout Implementation ✅ (2025-12-14)
- [x] Create immersive 960×544 fullscreen layout with top bar navigation
- [x] Implement Vita controller outlines (front/back) with PlayStation Blue tint
- [x] Build three-view system (Summary, Front Mapping, Back Mapping)
- [x] Create interactive button mapping callouts with pulse animation
- [x] Implement 6 controller presets (Default, FPS, Racing, Fighting, Remote Play Classic, Custom)
- [x] Add preset cycling via D-pad/touch with instant diagram updates
- [x] Create front/back view switching with 220ms ease-in-out animation
- [x] Integrate focus manager for touch + controller parity
- [x] Implement menu overlay with wave navigation

#### Phase 2: Code Review & Refinements ✅ (2025-12-14)
- [x] Extract rectangle outline utility to ui_graphics module
- [x] Eliminate DRY violation in shoulder button rendering
- [x] Fix LEFT/RIGHT d-pad input conflict in view toggle
- [x] Standardize color constant naming conventions
- [x] Extract magic numbers into named constants
- [x] Remove dead code patterns
- [x] Document PlayStation font requirements

#### Phase 3: Procedural Rendering Migration ✅ (2025-12-14)
- [x] Replace PNG-based controller diagrams with vita2d procedural rendering
- [x] Implement ratio-based coordinate system for pixel-perfect scaling at any size
- [x] Create ~97 ratio constants in ui_constants.h for diagram geometry
- [x] Implement procedural front view: body, screen, D-pad, face buttons, analog sticks, shoulders, system buttons
- [x] Implement procedural back view: body, rear touchpad with 4 interactive zones
- [x] Map all 16 button IDs (DPAD, TRIANGLE, CIRCLE, CROSS, SQUARE, L, R, LSTICK, RSTICK, PS, START, SELECT, RTOUCH_UL, RTOUCH_UR, RTOUCH_LL, RTOUCH_LR)
- [x] Implement pulsing glow highlight system for button/zone indication
- [x] Preserve all animations (flip, color tween, pulse) in procedural system
- [x] Remove PNG texture loading/cleanup code
- [x] Remove PNG asset packaging from CMakeLists.txt
- [x] Reduce VPK size by 1.3MB (3.9MB → 2.6MB)

**Status:** ✅ Production-ready with pixel-perfect scaling and no external asset dependencies

### Phase 10: Network Socket Buffer Optimization (FPS Deficit Root Cause) - IN PROGRESS
**Goal:** Eliminate periodic FPS loss (~9.5fps/sec) caused by UDP socket buffer overflow

#### Phase 10.1: Root Cause Analysis & Implementation ✅ (Feb 13, 2026)
- [x] Identified root cause: 100KB socket buffer insufficient for PS5 burst rate (~1200 packets/sec)
- [x] Implemented P0: Vita-specific 512KB socket receive buffer (TAKION_A_RWND)
- [x] Implemented P1: Batch packet drain loop (up to 64 zero-timeout recv calls)
- [x] Added getsockopt logging to confirm actual buffer sizes
- [x] Testing build verification: `./tools/build.sh --env testing` succeeded
- [x] Branch: `feat/startup-connect-burst-rework` (commit 481f7e6)

**Expected Impact:**
- FPS loss: ~9.5fps → near-zero (socket overflow eliminated)
- Sustained latency: 10-15ms reduction (syscall overhead reduction)
- Burst handling: 5x larger buffer handles PS5's packet rate

#### Phase 10.2: Hardware Validation (PENDING)
- [ ] Deploy testing build to PS Vita
- [ ] Conduct 10+ minute streaming session with measurements
- [ ] Verify FPS loss elimination (target: <1fps/sec loss)
- [ ] Test packet loss and latency regression (should be none)
- [ ] Validate with different WiFi conditions

#### Phase 10.3: Post-Validation Decision
- If P0+P1 successful (FPS <1/sec loss): Merge to main, move to Phase 11
- If P0+P1 insufficient (FPS >2/sec loss): Implement P2 (buffer pool malloc elimination)
- P2 effort: ~2 hours, expected benefit: +5-10fps improvement

**Reference:** `docs/ai/NETWORK_SOCKET_BUFFER_OPTIMIZATION.md` (detailed technical analysis)

---

### Phase 11: Integration & Future Work
**Goal**: Ensure all functionality works seamlessly

#### Subphase 11.1: Hardware Validation (Deferred)
- [ ] Test menu overlay navigation on real Vita device
- [ ] Verify focus loops across all interactive elements
- [ ] Validate animation performance at target FPS
- [ ] Test preset persistence across app restarts

#### Subphase 9.2: Core Functionality Integration
- [ ] Verify streaming integration with new UI
- [ ] Test discovery and registration features
- [ ] Validate controller input and touch navigation
- [ ] Ensure configuration persistence

#### Subphase 9.3: Performance Optimization
- [ ] Optimize rendering for 60fps target
- [ ] Minimize memory usage for PS Vita constraints
- [ ] Maintain sub-50ms streaming latency
- [ ] Test extended play sessions

#### Subphase 9.4: Final Polish
- [ ] Code review and cleanup
- [ ] Performance testing and optimization
- [ ] User experience testing
- [ ] Documentation updates

## Current Status
- **Completed Epics**: UI Refactoring (Phase 1-8) ✅, Controller Layout Redesign (Phase 9, 3 phases) ✅
- **In Progress**: Network Socket Buffer Optimization (P0+P1 implemented, hardware validation pending)
- **Progress**: 100% complete on UI refactoring and controller redesign; 50% on network optimization (code complete, testing pending)
- **Latest Version**: v0.1.77
- **Architecture**: 8 UI specialized modules + 1 coordinator + procedurally rendered controller diagram system
- **VPK Size**: 2.6MB (reduced from 3.9MB, 33% reduction)
- **Network Changes**: 512KB Vita-specific socket buffer (P0) + batch packet drain loop (P1) in lib/src/takion.c
- **Next Focus**: Hardware validation of network optimization; then integration testing and UI hardening

## Success Criteria
1. **Visual Transformation**: vitaki-fork displays vitarps5's modern interface
2. **Functionality Preservation**: All streaming features continue working
3. **Performance Maintenance**: Smooth UI with maintained streaming performance
4. **Code Quality**: Clean modular structure following project standards

## Technical Milestones
- [ ] Modern UI compiles without errors
- [ ] Basic navigation between UI states works
- [ ] Console discovery integrates with new dashboard
- [ ] Settings changes persist correctly
- [ ] Streaming launches from new interface
- [ ] Touch and controller input both work
- [ ] Performance meets latency requirements

## Risk Mitigation
- Working on separate branch with original UI preserved
- Incremental testing at each phase
- Performance monitoring throughout migration
- Asset validation for PS Vita compatibility