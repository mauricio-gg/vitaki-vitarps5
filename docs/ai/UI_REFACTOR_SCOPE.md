# UI.C Refactoring Scope Document

**Version:** 2.0
**Date:** December 2025
**Status:** Phases 1-4 Complete, Phases 5-8 Pending
**Target:** `vita/src/ui.c` (4,776 lines → ~3,500 lines after Phase 4)

---

## Executive Summary

This document defines the scope for refactoring `vita/src/ui.c` into a modular architecture. The current monolithic file contains 80+ functions handling navigation, rendering, input, animation, and state management. This refactor will:

1. Split the file into 8 focused modules
2. Establish clear interfaces between components
3. Maintain 100% functional equivalence (no behavioral changes)
4. Create a `vita/src/ui/` directory for all UI modules
5. Follow patterns defined in `docs/ai/UI_CODE_BEST_PRACTICES.md`

**Risk Level:** Medium - Many functions have implicit dependencies through global state

---

## Target Architecture

### New Directory Structure
```
vita/
├── src/
│   ├── ui/                          # NEW: UI module directory
│   │   ├── ui_main.c               # Coordinator and render loop
│   │   ├── ui_navigation.c         # Wave sidebar, collapse, toast
│   │   ├── ui_console_cards.c      # Console card rendering and cache
│   │   ├── ui_screens.c            # All 9 screen implementations
│   │   ├── ui_components.c         # Reusable widgets
│   │   ├── ui_graphics.c           # Drawing primitives
│   │   ├── ui_animation.c          # Animation framework + particles
│   │   ├── ui_input.c              # Input handling
│   │   └── ui_state.c              # Global state management
│   └── ui.c                        # DEPRECATED: Redirect to ui/ui_main.c
├── include/
│   ├── ui/                         # NEW: UI header directory
│   │   ├── ui_internal.h           # Internal shared types/state
│   │   ├── ui_constants.h          # Layout constants, colors
│   │   ├── ui_types.h              # Type definitions
│   │   ├── ui_navigation.h         # Navigation public API
│   │   ├── ui_console_cards.h      # Card public API
│   │   ├── ui_components.h         # Component public API
│   │   ├── ui_graphics.h           # Graphics primitives API
│   │   ├── ui_animation.h          # Animation API
│   │   └── ui_input.h              # Input handling API
│   └── ui.h                        # Public UI API (unchanged interface)
```

---

## Module Specifications

### Module 1: ui_main.c (Coordinator)
**Estimated Lines:** 300
**Purpose:** Main render loop and screen dispatch

#### Functions to Extract
```c
// From ui.c
void draw_ui()                        // Main render loop - lines 4614-4776
void init_ui()                        // Initialization
void load_textures()                  // Asset loading
```

#### Dependencies
- All other UI modules
- context.h, host.h

#### Public Interface
```c
// ui.h (unchanged)
void draw_ui(void);
void ui_init(void);
void ui_cleanup(void);
```

---

### Module 2: ui_navigation.c (Navigation System)
**Estimated Lines:** 1200
**Purpose:** Wave sidebar, collapse animation, navigation pill

#### Functions to Extract
```c
// State Machine
static void nav_request_collapse()              // Line ~556
static void nav_request_expand()                // Line ~577
static void nav_toggle_collapse()               // Line ~594
static void nav_reset_to_collapsed()            // Line ~603
static void update_nav_collapse_animation()     // Line ~635

// Rendering
void render_wave_navigation()                   // Line ~1653
void render_nav_pill()                          // Line ~716
static void render_nav_collapse_toast()         // Line ~847
static void draw_hamburger_icon()               // Line ~702
static void update_wave_animation()             // Line ~1631

// Input
static bool nav_touch_hit()                     // Line ~1801
static bool pill_touch_hit()                    // Line ~888
static bool handle_global_nav_shortcuts()       // Line ~1816

// Icons (procedural)
void draw_play_icon()                           // Line ~2336
void draw_settings_icon()                       // Line ~2357
void draw_controller_icon()                     // Line ~2389
void draw_profile_icon()                        // Line ~2424
```

#### Global State to Extract
```c
NavCollapseState nav_collapse;
int selected_nav_icon;
FocusArea current_focus;
WaveLayerState wave_bottom_state, wave_top_state;
uint64_t wave_last_update_us;
```

#### Public Interface
```c
// ui_navigation.h
void ui_nav_init(void);
void ui_nav_render(void);
void ui_nav_handle_input(void);
bool ui_nav_is_expanded(void);
void ui_nav_request_collapse(void);
void ui_nav_request_expand(void);
int ui_nav_get_selected_icon(void);
void ui_nav_set_selected_icon(int index);
FocusArea ui_nav_get_focus_area(void);
void ui_nav_set_focus_area(FocusArea area);
```

---

### Module 3: ui_console_cards.c (Console Cards)
**Estimated Lines:** 400
**Purpose:** Console card rendering, caching, host mapping

#### Functions to Extract
```c
void render_console_grid()                      // Line ~2257
void render_console_card()                      // Line ~1952
void map_host_to_console_card()                 // Line ~1914
void update_console_card_cache()                // Line ~2157
static void update_card_focus_animation()       // Line ~2187
static float get_card_scale()                   // Line ~2228
```

#### Global State to Extract
```c
int selected_console_index;
int last_console_selection;
ConsoleCardCache card_cache;
CardFocusAnimState card_focus_anim;
```

#### Public Interface
```c
// ui_console_cards.h
void ui_cards_init(void);
void ui_cards_render_grid(int x, int y);
void ui_cards_update_cache(void);
int ui_cards_get_selected_index(void);
void ui_cards_set_selected_index(int index);
ConsoleCardInfo* ui_cards_get_card(int index);
int ui_cards_get_count(void);
void ui_cards_handle_input(void);
```

---

### Module 4: ui_screens.c (Screen Implementations)
**Estimated Lines:** 2000
**Purpose:** All 9 screen rendering functions

#### Functions to Extract
```c
// Main Screens
UIScreenType draw_main_menu()                   // Line ~2808
UIScreenType draw_settings()                    // Line ~3171
UIScreenType draw_profile_screen()              // Line ~3557
UIScreenType draw_controller_config_screen()    // Line ~3882

// Overlays
bool draw_registration_dialog()                 // Line ~4085
bool draw_stream()                              // Line ~4198
UIScreenType draw_waking_screen()               // Line ~4253
UIScreenType draw_reconnecting_screen()         // Line ~4392
bool draw_messages()                            // Line ~4472

// Screen-specific helpers
static void draw_settings_streaming_tab()       // Line ~3060
static void draw_settings_controller_tab()      // Line ~3141
static void draw_profile_card()                 // Line ~3311
static void draw_connection_info_card()         // Line ~3363
static void draw_registration_section()         // Line ~3501
static void draw_controller_mappings_tab()      // Line ~3750
static void draw_controller_settings_tab()      // Line ~3860
```

#### Global State to Extract
```c
SettingsState settings_state;
ControllerState controller_state;
ProfileState profile_state;
PinEntryState pin_entry_state;
bool show_cursor, pin_entry_initialized;
uint32_t cursor_blink_timer;
uint32_t waking_start_time, reconnect_start_time;
int reconnect_animation_frame;
```

#### Public Interface
```c
// ui_screens.h (internal header)
UIScreenType ui_screen_draw_main(void);
UIScreenType ui_screen_draw_settings(void);
UIScreenType ui_screen_draw_profile(void);
UIScreenType ui_screen_draw_controller(void);
UIScreenType ui_screen_draw_waking(void);
UIScreenType ui_screen_draw_reconnecting(void);
bool ui_screen_draw_registration(void);
bool ui_screen_draw_stream(void);
bool ui_screen_draw_messages(void);
```

---

### Module 5: ui_components.c (Reusable Widgets)
**Estimated Lines:** 800
**Purpose:** Toggle switches, dropdowns, tab bars, status indicators

#### Functions to Extract
```c
// Widgets
static void draw_toggle_switch()                // Line ~1313
static void draw_dropdown()                     // Line ~1363
static void draw_tab_bar()                      // Line ~1413
static void draw_status_dot()                   // Line ~1449
static void draw_section_header()               // Line ~1473
void render_pin_digit()                         // Line ~4052

// Animation helpers
static void start_toggle_animation()            // Line ~1268
static float get_toggle_animation_value()       // Line ~1275

// Dialogs
void render_error_popup()                       // Line ~939
void handle_error_popup_input()                 // Line ~969
void render_hints_popup()                       // Line ~787
void render_hints_indicator()                   // Line ~826
void trigger_hints_popup()                      // Line ~841

// Debug menu
void render_debug_menu()                        // Line ~1119
void handle_debug_menu_input()                  // Line ~1173
void open_debug_menu()                          // Line ~998
void close_debug_menu()                         // Line ~1009
void debug_menu_apply_action()                  // Line ~1018
```

#### Global State to Extract
```c
ToggleAnimationState toggle_anim;
HintsPopupState hints_popup;
bool error_popup_active;
char error_message[256];
int debug_menu_selection;
bool debug_menu_active;
```

#### Public Interface
```c
// ui_components.h
void ui_draw_toggle(int x, int y, int width, bool value, bool selected, int index);
void ui_draw_dropdown(int x, int y, int width, const char* label, const char* value, bool selected);
void ui_draw_tab_bar(int x, int y, const char** tabs, int count, int selected);
void ui_draw_status_dot(int x, int y, int status);
void ui_draw_section_header(int x, int y, int width, const char* text);

void ui_show_error(const char* message);
void ui_hide_error(void);
bool ui_is_error_visible(void);

void ui_trigger_hints(void);
void ui_render_hints(void);

void ui_debug_menu_open(void);
void ui_debug_menu_close(void);
void ui_debug_menu_render(void);
void ui_debug_menu_handle_input(void);
```

---

### Module 6: ui_graphics.c (Drawing Primitives)
**Estimated Lines:** 600
**Purpose:** Low-level drawing functions

#### Functions to Extract
```c
// Primitives
static void draw_rounded_rectangle()            // Line ~1201
static void draw_card_with_shadow()             // Line ~1243
static void draw_circle()                       // Line ~907
static void draw_circle_outline_simple()        // Line ~398
static void draw_spinner()                      // Line ~1492

// Focus/overlay
void render_content_focus_overlay()             // Line ~773
void render_loss_indicator_preview()            // Line ~1069
```

#### Public Interface
```c
// ui_graphics.h
void ui_draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
void ui_draw_card_shadow(int x, int y, int w, int h, int radius, uint32_t color);
void ui_draw_circle(int cx, int cy, int radius, uint32_t color);
void ui_draw_circle_outline(int cx, int cy, int radius, uint32_t color);
void ui_draw_spinner(int cx, int cy, int radius, float angle, uint32_t color);
void ui_draw_focus_overlay(float opacity);
void ui_draw_loss_indicator(int x, int y);
```

---

### Module 7: ui_animation.c (Animation Framework)
**Estimated Lines:** 400
**Purpose:** Animation timing, easing, particle system

#### Functions to Extract
```c
// Easing
static inline float lerp()                      // Line ~1258
static inline float ease_in_out_cubic()         // Line ~1263

// Particles
void init_particles()                           // Line ~1530
void update_particles()                         // Line ~1568
void render_particles()                         // Line ~1601

// Timing utilities
static void update_cursor_blink()               // Line ~4024
```

#### Global State to Extract
```c
Particle particles[PARTICLE_COUNT];
bool particles_initialized;
int particle_update_frame;
```

#### Public Interface
```c
// ui_animation.h
float ui_anim_lerp(float a, float b, float t);
float ui_anim_ease_in_out_cubic(float t);
uint64_t ui_anim_now_us(void);
float ui_anim_elapsed_ms(uint64_t start_us);

void ui_particles_init(void);
void ui_particles_update(void);
void ui_particles_render(void);
```

---

### Module 8: ui_input.c (Input Handling)
**Estimated Lines:** 300
**Purpose:** Button and touch input processing

#### Functions to Extract
```c
bool btn_pressed(SceCtrlButtons btn)            // Line ~535
UIScreenType handle_vitarps5_touch_input()      // Line ~2499
static void block_inputs_for_transition()       // Line ~544

// Hit testing
static bool is_touched()                        // Line ~2476
bool is_point_in_circle()                       // Line ~2487
bool is_point_in_rect()                         // Line ~2494
```

#### Global State to Extract
```c
uint32_t button_block_mask;
bool touch_block_active;
bool touch_block_pending_clear;
```

#### Public Interface
```c
// ui_input.h
void ui_input_init(void);
void ui_input_update(void);
bool ui_input_btn_pressed(SceCtrlButtons btn);
bool ui_input_btn_held(SceCtrlButtons btn);
void ui_input_block_for_transition(void);
void ui_input_clear_blocks(void);

bool ui_input_point_in_rect(float px, float py, int rx, int ry, int rw, int rh);
bool ui_input_point_in_circle(float px, float py, int cx, int cy, int radius);

float ui_input_touch_x(void);
float ui_input_touch_y(void);
bool ui_input_is_touching(void);
```

---

### Module 9: ui_state.c (Global State)
**Estimated Lines:** 200
**Purpose:** Centralized state management and utilities

#### Functions to Extract
```c
// Connection overlay
void ui_connection_begin()                      // Line ~4205
void ui_connection_set_stage()                  // Line ~4213
void ui_connection_complete()                   // Line ~4220
void ui_connection_cancel()                     // Line ~4226
bool ui_connection_overlay_active()             // Line ~4238
void ui_clear_waking_wait()                     // Line ~4246
static void start_connection_thread()           // Line ~412

// Cooldown
static bool stream_cooldown_active()            // Line ~335
static uint64_t stream_cooldown_until_us()      // Line ~365
static bool takion_cooldown_gate_active()       // Line ~377

// Text caching
static int get_text_width_cached()              // Line ~453
```

#### Global State to Extract
```c
ConnectionOverlayState connection_overlay;
SceUID connection_thread_id;
VitaChiakiHost *connection_thread_host;
TextWidthCacheEntry text_width_cache[TEXT_WIDTH_CACHE_SIZE];
int SCE_CTRL_CONFIRM, SCE_CTRL_CANCEL;
char* confirm_btn_str, *cancel_btn_str;
```

#### Public Interface
```c
// ui_state.h
void ui_state_init(void);

void ui_connection_begin(ConnectionStage stage);
void ui_connection_set_stage(ConnectionStage stage);
void ui_connection_complete(void);
void ui_connection_cancel(void);
bool ui_connection_is_active(void);

bool ui_cooldown_active(void);
uint64_t ui_cooldown_remaining_ms(void);

int ui_text_width(const char* text, int font_size);
void ui_text_cache_clear(void);

SceCtrlButtons ui_get_confirm_button(void);
SceCtrlButtons ui_get_cancel_button(void);
```

---

## Shared Header: ui_internal.h

All modules will share access to internal state through this header:

```c
// vita/include/ui/ui_internal.h
#pragma once

#include "ui_types.h"
#include "ui_constants.h"
#include <vita2d.h>

// Shared texture pointers (loaded in ui_main.c)
extern vita2d_font* ui_font;
extern vita2d_font* ui_font_mono;
extern vita2d_texture* img_ps4;
extern vita2d_texture* img_ps5;
// ... etc

// Shared context access
extern VitaChiakiContext context;

// Current screen state
extern UIScreenType current_screen;

// Forward declarations for cross-module calls
void ui_nav_render(void);
void ui_cards_render_grid(int x, int y);
void ui_particles_render(void);
// ... etc
```

---

## Refactoring Phases

### Phase 1: Setup (Low Risk) ✅ COMPLETED
**Commit:** 260b163
**Status:** All directory structure and header files created
1. Create `vita/src/ui/` and `vita/include/ui/` directories
2. Create `ui_constants.h` with all `#define` values
3. Create `ui_types.h` with all type definitions
4. Create `ui_internal.h` for shared state
5. Build and verify no changes

### Phase 2: Extract Primitives (Low Risk) ✅ COMPLETED
**Commit:** 97c4033
**Status:** Graphics and animation modules extracted successfully
1. Extract `ui_graphics.c` - pure drawing functions
2. Extract `ui_animation.c` - easing and particles
3. Update includes in `ui.c`
4. Build and test

### Phase 3: Extract Utilities (Low Risk) ✅ COMPLETED
**Commit:** ad89b6d
**Status:** Input and state management modules extracted
**Lines Removed from ui.c:** ~360 lines
1. Extract `ui_input.c` - input handling (button press detection, touch handling, hit testing)
2. Extract `ui_state.c` - state management (connection overlay, cooldowns, text cache, connection thread)
3. Update includes in `ui.c`
4. Build and test

### Phase 4: Extract Components (Medium Risk) ✅ COMPLETED
**Commit:** d28046e
**Status:** Reusable components module extracted
**Lines Removed from ui.c:** ~550 lines
1. Extract `ui_components.c` - widgets and dialogs (toggles, dropdowns, tabs, status dots, dialogs)
2. Error popup, hints popup, debug menu extracted
3. Toggle animation with cubic easing implemented
4. Build and test

### Phase 5: Extract Navigation (Medium Risk) 📋 PENDING
1. Extract `ui_navigation.c` - complex state machine
2. Create accessor functions for nav state
3. Update all nav references in `ui.c`
4. Build and test

### Phase 6: Extract Cards (Medium Risk) 📋 PENDING
1. Extract `ui_console_cards.c`
2. Create accessor functions for card state
3. Update card references
4. Build and test

### Phase 7: Extract Screens (High Risk) 📋 PENDING
1. Extract `ui_screens.c` - largest module
2. Wire up all screen dispatch
3. Update main render loop
4. Extensive testing

### Phase 8: Cleanup (Low Risk) 📋 PENDING
1. Rename remaining `ui.c` to `ui_main.c`
2. Update CMakeLists.txt with all new source files
3. Remove deprecated code
4. Final verification

---

## Testing Strategy

### Per-Phase Testing
After each phase, verify:
1. `./tools/build.sh` completes without errors
2. `./tools/build.sh debug` produces working VPK
3. All screens render correctly
4. Navigation works (D-pad + touch)
5. Settings toggles persist
6. Console discovery works
7. Streaming connects successfully

### Regression Checklist
- [ ] Main menu renders with console cards
- [ ] Navigation sidebar collapses/expands
- [ ] Settings screen all toggles work
- [ ] Profile screen displays correctly
- [ ] Controller config tabs navigate
- [ ] Registration PIN entry works
- [ ] Waking screen shows spinner
- [ ] Reconnecting screen animates
- [ ] Error popup displays and dismisses
- [ ] Hints popup appears on Select
- [ ] Debug menu opens (L+R+Select)
- [ ] Particle animation runs smoothly
- [ ] Touch input works on all screens

---

## Risk Mitigation

### High-Risk Areas
1. **Global State Access** - Many functions read/write globals directly
   - *Mitigation*: Create accessor functions before moving code
   - *Mitigation*: Keep `ui_internal.h` with extern declarations

2. **Initialization Order** - Some state depends on init order
   - *Mitigation*: Document dependencies, test each phase

3. **Thread Safety** - Connection thread accesses UI state
   - *Mitigation*: Keep `connection_overlay` access in `ui_state.c`

4. **Screen Coupling** - Screens call components that call primitives
   - *Mitigation*: Extract bottom-up (primitives first)

### Rollback Strategy
- Each phase in a separate Git branch
- Tag before each phase: `refactor-phase-N-start`
- If phase fails, revert to previous tag

---

## Success Criteria

1. **Functional Equivalence**: All UI behavior identical to before refactor
2. **Build Success**: Clean compilation with no warnings
3. **Module Independence**: Each module can be understood in isolation
4. **Clear Interfaces**: Public APIs documented in headers
5. **Reduced Coupling**: No circular dependencies between modules
6. **Line Count**: Each module under 800 lines (except screens ~2000)

---

## Timeline

| Phase | Description | Status | Completion Date |
|-------|-------------|--------|-----------------|
| 1 | Setup directories and headers | ✅ Complete | 2025-12-10 |
| 2 | Extract primitives | ✅ Complete | 2025-12-11 |
| 3 | Extract utilities | ✅ Complete | 2025-12-12 |
| 4 | Extract components | ✅ Complete | 2025-12-12 |
| 5 | Extract navigation | 📋 Pending | TBD |
| 6 | Extract cards | 📋 Pending | TBD |
| 7 | Extract screens | 📋 Pending | TBD |
| 8 | Cleanup and verification | 📋 Pending | TBD |

**Completed: 4/8 phases**
**Estimated Remaining: 4-5 work sessions**

---

## Related Documents

- `docs/ai/UI_REFACTOR_ANALYSIS.md` - Detailed component analysis
- `docs/ai/UI_CODE_BEST_PRACTICES.md` - Coding standards for new modules
- `docs/ai/UI_MAINTENANCE_GUIDE.md` - Long-term maintenance guidelines
- `docs/ai/UI_FINAL_SPECIFICATION.md` - Design specification

---

**Document Status:** Updated - Phases 1-4 Complete
**Current Progress:** 50% of refactoring complete (4 of 8 phases)
**Next Step:** Phase 5 - Extract Navigation system (ui_navigation.c)

## Completion Summary (as of 2025-12-12)

### Modules Created
1. **vita/src/ui/ui_graphics.c/h** - Drawing primitives (rounded rectangles, circles, shadows, spinner, focus overlay)
2. **vita/src/ui/ui_animation.c/h** - Easing functions, timing utilities, particle system
3. **vita/src/ui/ui_input.c/h** - Button press detection, touch handling, hit testing, input blocking
4. **vita/src/ui/ui_state.c/h** - Connection overlay state, cooldowns, text cache, connection thread management
5. **vita/src/ui/ui_components.c/h** - Reusable widgets (toggles, dropdowns, tabs, status dots), dialogs, error/hints popups, debug menu

### Code Metrics
- **Original ui.c:** 4,776 lines
- **Current ui.c:** ~3,500 lines (estimated)
- **Lines Extracted:** ~910 lines
- **Reduction:** 19% of original file size
- **Modules Created:** 5 (graphics, animation, input, state, components)
- **Header Files Created:** 5 corresponding .h files

### Quality Improvements
- Separated concerns into focused modules
- Each module under 600 lines (compliant with design target)
- Clear public interfaces defined in headers
- Reduced global state dependencies in ui.c
- Enabled parallel development on remaining phases
