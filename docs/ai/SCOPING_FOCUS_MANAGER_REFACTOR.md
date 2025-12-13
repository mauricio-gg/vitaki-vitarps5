# Focus Manager Refactor - Implementation Plan

## Problem Statement

The current focus management system has architectural issues causing repeated bugs:
- Global `current_focus` variable scattered across files
- Zone-crossing logic (LEFT/RIGHT) duplicated in multiple handlers
- Guard-based fixes (`if (current_focus != FOCUS_NAV_BAR)`) are band-aids
- No modal support for dialogs that need focus isolation

## Solution: Centralized Focus Manager

Create a single focus manager module that:
1. **Owns all focus state** in one location
2. **Handles ALL zone-crossing input** (LEFT/RIGHT) before screen handlers
3. **Delegates intra-zone input** (UP/DOWN) to content handlers
4. **Supports modal focus stacking** for future dialogs

---

## Architecture Overview

### Current Flow (Problematic)
```
D-Pad Input
    ↓
ui_nav_handle_shortcuts() → May process input
    ↓ (returns false)
Screen handler → Also processes same input

Result: Double-processing, inconsistent behavior
```

### New Flow (Centralized)
```
D-Pad Input
    ↓
ui_focus_handle_zone_crossing() → Handles LEFT/RIGHT exclusively
    ↓ (returns true = consumed, false = delegate)
Screen handler → Only handles UP/DOWN in content zone

Result: One input, one handler
```

### Focus Zones

| Zone | Screen | Elements | UP/DOWN | LEFT/RIGHT |
|------|--------|----------|---------|------------|
| `ZONE_NAV_BAR` | All | Nav icons | Cycle icons + auto-switch | → Content |
| `ZONE_MAIN_CONTENT` | Main | Console cards | Cycle cards | ← Nav bar |
| `ZONE_SETTINGS_ITEMS` | Settings | Settings list | Cycle items | ← Nav bar |
| `ZONE_PROFILE_CARDS` | Profile | Info/Connection cards | N/A | Switch cards (intra-zone) |
| `ZONE_CONTROLLER_TABS` | Controller | Tab bar | N/A | L1/R1 (triggers) |
| `ZONE_CONTROLLER_ITEMS` | Controller | Tab content | Cycle items | Scheme cycle (mappings) |
| `ZONE_MODAL` | Any | Modal overlay | Modal-specific | Trapped |

---

## Files to Create

### 1. `vita/include/ui/ui_focus.h` (NEW)
```c
// Focus zone enumeration
typedef enum {
    ZONE_NAV_BAR,
    ZONE_MAIN_CONTENT,
    ZONE_SETTINGS_ITEMS,
    ZONE_PROFILE_CARDS,
    ZONE_CONTROLLER_TABS,
    ZONE_CONTROLLER_ITEMS,
    ZONE_MODAL
} FocusZone;

// Focus state for stack
typedef struct {
    FocusZone zone;
    int index;
} FocusState;

// API
void ui_focus_init(void);
FocusZone ui_focus_get_zone(void);
int ui_focus_get_index(void);
void ui_focus_set_zone(FocusZone zone);
void ui_focus_set_index(int index);
bool ui_focus_is_nav_bar(void);
bool ui_focus_is_content(void);

// Modal support
void ui_focus_push_modal(FocusZone modal_zone);
void ui_focus_pop_modal(void);
bool ui_focus_has_modal(void);

// Main input handler - call FIRST each frame
bool ui_focus_handle_zone_crossing(UIScreenType current_screen);
```

### 2. `vita/src/ui/ui_focus.c` (NEW)
- Focus stack implementation (max depth 4)
- Zone transition logic
- Screen-to-zone mapping

---

## Files to Modify

### 1. `vita/include/ui/ui_types.h`
- Add `FocusZone` enum
- Add `FocusState` struct
- Deprecate old `FocusArea` enum

### 2. `vita/include/ui/ui_internal.h`
- Remove `extern FocusArea current_focus` (line 84)
- Remove `extern int last_console_selection` (line 85)
- Add `#include "ui_focus.h"`

### 3. `vita/src/ui/ui_navigation.c`
- Remove `current_focus` global (line 37)
- Remove `last_console_selection` global (line 38)
- Simplify `ui_nav_handle_shortcuts()`:
  - Remove LEFT/RIGHT zone-crossing logic (lines 819-825)
  - Keep UP/DOWN nav icon selection (lines 827-839)
  - Keep Triangle toggle

### 4. `vita/src/ui/ui_screens.c`
| Function | Lines | Change |
|----------|-------|--------|
| `draw_main_menu()` | 398-411 | Remove duplicate D-pad handling |
| `draw_settings()` | 757 | Remove `current_focus` guard |
| `draw_controller_config_screen()` | 1523 | Remove `current_focus` guard |

### 5. `vita/src/ui/ui_console_cards.c`
- Replace `current_focus == FOCUS_CONSOLE_CARDS` with `ui_focus_is_content()`

### 6. `vita/src/ui.c`
- Add `ui_focus_init()` in initialization
- Add `ui_focus_handle_zone_crossing()` at start of frame loop

### 7. `vita/CMakeLists.txt`
- Add `ui_focus.c` to source list

---

## Implementation Phases

### Phase 1: Create Focus Manager Foundation
**Files:** `ui_focus.h`, `ui_focus.c`, `CMakeLists.txt`
**Tasks:**
- Create header with full API
- Create implementation with basic state management
- Add to build system
- Call `ui_focus_init()` from `init_ui()`

**Verification:** Build succeeds, no functional changes yet

### Phase 2: Migrate Navigation Module
**Files:** `ui_navigation.c`, `ui_internal.h`
**Tasks:**
- Keep `current_focus` temporarily but mirror to focus manager
- Add `ui_focus_handle_zone_crossing()` call in `ui_nav_handle_shortcuts()`
- Return early if zone crossing was handled

**Verification:** Navigation works, zone crossing uses new code path

### Phase 3: Migrate Screen Handlers
**Files:** `ui_screens.c`, `ui_console_cards.c`
**Tasks:**
- `draw_main_menu()`: Remove lines 405-408 (duplicate LEFT handling)
- `draw_settings()`: Remove guard at line 757
- `draw_controller_config_screen()`: Remove guard at line 1523
- `ui_console_cards.c`: Use `ui_focus_is_content()`

**Verification:** All screens navigate correctly, no double-processing

### Phase 4: Remove Legacy State
**Files:** `ui_navigation.c`, `ui_internal.h`
**Tasks:**
- Remove `current_focus` global variable
- Remove `last_console_selection` global
- Remove extern declarations
- Update any remaining references

**Verification:** `grep -r "current_focus"` shows zero results in UI code

### Phase 5: Add Modal Support (Future)
**Files:** `ui_focus.c`, registration/debug screens
**Tasks:**
- Implement focus stack push/pop
- Update registration dialog
- Update debug menu
- Test modal focus isolation

**Verification:** Modals block background input, restore focus on exit

---

## Testing Checklist

### Navigation Tests
- [ ] LEFT from content → nav bar focused
- [ ] RIGHT from nav bar → content focused
- [ ] UP/DOWN in nav bar → cycles icons, auto-switches pages
- [ ] UP/DOWN in content → cycles items (screen-specific)
- [ ] Triangle → toggles nav collapse from any screen

### Screen-Specific Tests
- [ ] Main: Card selection, connection trigger
- [ ] Settings: Item selection, toggle activation
- [ ] Profile: Card switching (LEFT/RIGHT within zone)
- [ ] Controller: Tab switching, scheme cycling, item selection

### Edge Cases
- [ ] Empty host list handling
- [ ] Focus preservation across screen transitions
- [ ] Touch input still works alongside controller
- [ ] No visual glitches during focus animations

---

## Risk Assessment

| Risk | Level | Mitigation |
|------|-------|------------|
| Adding new module | Low | Purely additive initially |
| Screen handler changes | Medium | Phase each screen separately |
| Input timing | High | Single call point in main loop |
| Modal edge cases | Medium | Implement last after base stable |

---

## Estimated Effort

| Phase | Effort | Dependencies |
|-------|--------|--------------|
| Phase 1: Foundation | 1-2 hours | None |
| Phase 2: Navigation | 1-2 hours | Phase 1 |
| Phase 3: Screens | 2-3 hours | Phase 2 |
| Phase 4: Cleanup | 30 min | Phase 3 |
| Phase 5: Modals | 1-2 hours | Phase 2 (parallel) |

**Total: ~6-9 hours**

---

## Approval Checklist

- [ ] Architecture approach approved
- [ ] File changes scope acceptable
- [ ] Phase ordering makes sense
- [ ] Risk mitigations adequate
- [ ] Ready to proceed with Phase 1
