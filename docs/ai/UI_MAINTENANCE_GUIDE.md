# UI Maintenance Guide for VitaRPS5

**Version:** 1.0
**Date:** December 2025
**Purpose:** Prevent UI code degradation and ensure long-term maintainability

---

## Why This Guide Exists

The `vita/src/ui.c` file grew to 4,776 lines because:
1. No clear module boundaries were defined early
2. New features were added without considering organization
3. Related code was placed wherever convenient
4. No formal review process for UI changes

This guide establishes practices to prevent similar issues.

---

## Golden Rules

### 1. One Module, One Responsibility
Each UI file should have a single, well-defined purpose:

| Module | Sole Responsibility |
|--------|---------------------|
| `ui_main.c` | Render loop orchestration |
| `ui_navigation.c` | Sidebar navigation only |
| `ui_console_cards.c` | Console card display only |
| `ui_screens.c` | Screen-specific rendering |
| `ui_components.c` | Reusable widgets only |
| `ui_graphics.c` | Drawing primitives only |
| `ui_animation.c` | Animation timing only |
| `ui_input.c` | Input handling only |
| `ui_state.c` | State management only |

**If you're unsure where code belongs, ask: "What is this code's single responsibility?"**

### 2. Size Limits
- **Hard limit**: No UI module > 1000 lines (except `ui_screens.c` at ~2000)
- **Soft limit**: Individual functions < 100 lines
- **Warning threshold**: If a module exceeds 800 lines, consider splitting

When approaching limits:
1. Review if all code belongs in this module
2. Extract reusable parts to appropriate modules
3. Create a new sub-module if needed

### 3. New Code Location Decision Tree

```
Is this code a...

Drawing primitive (rect, circle, line)?
  → ui_graphics.c

Animation/easing function?
  → ui_animation.c

Input handling (button press, touch)?
  → ui_input.c

State management (connection, config)?
  → ui_state.c

Reusable widget (toggle, dropdown)?
  → ui_components.c

Navigation-specific?
  → ui_navigation.c

Console card-specific?
  → ui_console_cards.c

Screen-specific rendering?
  → ui_screens.c

None of the above?
  → Ask: Can it be made reusable?
    → Yes: ui_components.c
    → No: Screen where it's used
```

### 4. Interface-First Development

Before adding new functionality:

1. **Define the interface** in the header file first
2. **Document the contract** (inputs, outputs, side effects)
3. **Implement** the function
4. **Test** the interface works correctly

```c
// GOOD: Interface defined first
// ui_components.h
/**
 * Renders a slider control at the specified position.
 * @param x Left edge X coordinate
 * @param y Top edge Y coordinate
 * @param width Total width of slider track
 * @param value Current value (0.0 to 1.0)
 * @param selected Whether slider is focused
 * @return New value if changed, otherwise same value
 */
float ui_draw_slider(int x, int y, int width, float value, bool selected);

// BAD: Implementation without interface
static void draw_some_slider_thing(int x, int y, ...) { ... }
```

---

## Code Quality Standards

### Header Organization
Every UI module must have a corresponding header with:
1. Include guards (`#pragma once`)
2. Required includes
3. Type definitions (if module-specific)
4. Function declarations with documentation
5. No implementation details

```c
// ui_navigation.h
#pragma once

#include <stdbool.h>
#include "ui_types.h"

/**
 * Initialize navigation system.
 * Must be called before any nav rendering.
 */
void ui_nav_init(void);

/**
 * Render the navigation sidebar.
 * Handles collapsed/expanded states automatically.
 */
void ui_nav_render(void);

/**
 * Check if navigation is currently expanded.
 * @return true if sidebar is visible, false if collapsed
 */
bool ui_nav_is_expanded(void);
```

### Function Naming
- **Public functions**: `ui_module_verb_object()`
  - Example: `ui_nav_request_collapse()`
- **Static functions**: `verb_object()` or `module_verb_object()`
  - Example: `render_sidebar_icon()` or `nav_render_sidebar_icon()`

### State Access
- **Never** access global state directly from other modules
- **Always** use accessor functions from `ui_state.c`

```c
// BAD: Direct global access
if (context.stream.is_streaming) { ... }

// GOOD: Accessor function
if (ui_state_is_streaming()) { ... }
```

### Constants
- All magic numbers must be defined constants
- Constants go in `ui_constants.h`
- Name format: `UI_CATEGORY_NAME`

```c
// BAD
draw_rounded_rect(x, y, 200, 205, 12, 0xFF37322D);

// GOOD
draw_rounded_rect(x, y, CONSOLE_CARD_WIDTH, CONSOLE_CARD_HEIGHT,
                  CARD_CORNER_RADIUS, UI_COLOR_CARD_BG);
```

---

## Adding New Features

### Adding a New Screen

1. **Define screen type** in `ui_types.h`:
   ```c
   typedef enum {
       // existing...
       UI_SCREEN_TYPE_NEW_SCREEN,
   } UIScreenType;
   ```

2. **Implement in `ui_screens.c`**:
   ```c
   UIScreenType ui_screen_draw_new_screen(void) {
       // Rendering code
       return UI_SCREEN_TYPE_NEW_SCREEN;
   }
   ```

3. **Add to dispatcher** in `ui_main.c`:
   ```c
   case UI_SCREEN_TYPE_NEW_SCREEN:
       next_screen = ui_screen_draw_new_screen();
       break;
   ```

4. **Wire navigation** if needed in `ui_navigation.c`

### Adding a New Component

1. **Define interface** in `ui_components.h`:
   ```c
   void ui_draw_new_widget(int x, int y, /* params */);
   ```

2. **Implement in `ui_components.c`**:
   ```c
   void ui_draw_new_widget(int x, int y, /* params */) {
       // Use primitives from ui_graphics.c
       ui_draw_rounded_rect(...);
       // Rendering code
   }
   ```

3. **Use from screens**:
   ```c
   #include "ui/ui_components.h"

   ui_draw_new_widget(100, 200, ...);
   ```

### Adding New State

1. **Define in `ui_state.c`**:
   ```c
   static NewStateType new_state = {0};
   ```

2. **Create accessors**:
   ```c
   NewStateType* ui_state_get_new_state(void) {
       return &new_state;
   }

   void ui_state_set_new_value(int value) {
       new_state.value = value;
   }
   ```

3. **Declare in `ui_state.h`**:
   ```c
   NewStateType* ui_state_get_new_state(void);
   void ui_state_set_new_value(int value);
   ```

---

## Code Review Checklist

Before merging UI changes, verify:

### Structure
- [ ] New code is in the correct module
- [ ] Module line count still under limit
- [ ] No circular dependencies introduced
- [ ] Header file updated if public interface changed

### Quality
- [ ] All magic numbers are constants
- [ ] Functions are < 100 lines
- [ ] No direct global state access from other modules
- [ ] Function naming follows conventions
- [ ] Documentation for public functions

### Testing
- [ ] `./tools/build.sh` succeeds
- [ ] All screens still render correctly
- [ ] Touch input works
- [ ] Controller navigation works
- [ ] No visual regressions

### Performance
- [ ] No per-frame allocations added
- [ ] Draw calls minimized
- [ ] Layout calculations cached if expensive

---

## Refactoring Guidelines

### When to Refactor
- Module exceeds 800 lines
- Function exceeds 80 lines
- Same code appears in 3+ places
- Change requires modifying 5+ files

### How to Refactor Safely

1. **Create a branch** for the refactor
2. **Write tests** for existing behavior (if possible)
3. **Make small, incremental changes**
4. **Build and test after each change**
5. **Commit frequently** with descriptive messages
6. **Get code review** before merging

### Refactoring Patterns

**Extract Function**
```c
// Before: Long function
void render_settings(void) {
    // 50 lines of streaming settings
    // 50 lines of controller settings
}

// After: Extracted helpers
void render_settings(void) {
    render_streaming_settings();
    render_controller_settings();
}
```

**Extract Module**
When a section of code grows to 300+ lines within a module:
1. Create new module file
2. Move related functions
3. Create header with public interface
4. Update includes in original module

**Replace Magic Numbers**
```c
// Before
vita2d_draw_rectangle(130, 0, 830, 544, color);

// After
vita2d_draw_rectangle(CONTENT_X, 0, CONTENT_WIDTH, SCREEN_HEIGHT, color);
```

---

## Common Mistakes to Avoid

### 1. "I'll just add it here for now"
**Problem**: Temporary code becomes permanent
**Solution**: Always put code in the right place from the start

### 2. Duplicating Instead of Reusing
**Problem**: Same drawing code in multiple places
**Solution**: Extract to `ui_components.c` or `ui_graphics.c`

### 3. Giant Switch Statements
**Problem**: Screen dispatcher with 500+ lines
**Solution**: Each case calls a dedicated function in `ui_screens.c`

### 4. Implicit Dependencies
**Problem**: Module A expects Module B to have initialized state
**Solution**: Explicit init functions, documented dependencies

### 5. Mixing Concerns
**Problem**: Input handling inside rendering function
**Solution**: Separate `render_X()` and `handle_X_input()` functions

---

## Module Dependency Map

```
ui_main.c
  ├── ui_navigation.c
  │     ├── ui_graphics.c
  │     ├── ui_animation.c
  │     └── ui_input.c
  ├── ui_console_cards.c
  │     ├── ui_graphics.c
  │     ├── ui_animation.c
  │     └── ui_state.c
  ├── ui_screens.c
  │     ├── ui_components.c
  │     ├── ui_graphics.c
  │     └── ui_state.c
  ├── ui_components.c
  │     └── ui_graphics.c
  └── ui_state.c
        └── (none - leaf module)

Legend:
  → A depends on B means A #includes B's header
```

**Rule**: No circular dependencies. If A depends on B, B cannot depend on A.

---

## File Template

When creating a new UI module:

```c
// vita/src/ui/ui_newmodule.c
/**
 * @file ui_newmodule.c
 * @brief Brief description of module purpose
 *
 * Detailed description of what this module handles.
 * List any important patterns or considerations.
 */

#include "ui/ui_newmodule.h"
#include "ui/ui_internal.h"
#include "ui/ui_graphics.h"  // if using primitives
#include "ui/ui_state.h"     // if using state

// ============================================================================
// Constants
// ============================================================================
#define NEWMODULE_CONSTANT 42

// ============================================================================
// Types
// ============================================================================
typedef struct {
    int field;
} LocalStateType;

// ============================================================================
// Static State
// ============================================================================
static LocalStateType local_state = {0};

// ============================================================================
// Private Functions
// ============================================================================
static void helper_function(void) {
    // Implementation
}

// ============================================================================
// Public Functions
// ============================================================================
void ui_newmodule_init(void) {
    local_state.field = 0;
}

void ui_newmodule_render(void) {
    // Rendering implementation
}
```

---

## Escalation Path

If you're unsure about:
- **Where code belongs**: Check the decision tree above, or ask in PR
- **Breaking changes**: Create an issue first, discuss approach
- **Performance concerns**: Profile before optimizing, document findings
- **Design patterns**: Reference `UI_CODE_BEST_PRACTICES.md`

---

## Related Documents

| Document | Purpose |
|----------|---------|
| `UI_REFACTOR_ANALYSIS.md` | Current state analysis of ui.c |
| `UI_REFACTOR_SCOPE.md` | Detailed refactoring plan |
| `UI_CODE_BEST_PRACTICES.md` | Coding standards and patterns |
| `UI_FINAL_SPECIFICATION.md` | Design specification |
| `UI_DEVELOPMENT_GUIDE.md` | Development workflow |

---

**Remember**: A few minutes spent organizing code correctly now saves hours of refactoring later.

---

**Document Maintainer:** VitaRPS5 Development Team
**Last Reviewed:** December 2025
