# UI Code Best Practices for PS Vita (vita2d)

**Document Version:** 1.0
**Last Updated:** December 2025
**Applies To:** VitaRPS5 and similar PS Vita C-based UI applications using vita2d

---

## Table of Contents

1. [File Organization Principles](#1-file-organization-principles)
2. [Code Architecture Patterns](#2-code-architecture-patterns)
3. [Resource Management](#3-resource-management)
4. [Maintainability Guidelines](#4-maintainability-guidelines)
5. [Performance Considerations](#5-performance-considerations)
6. [PS Vita / vita2d Specific Best Practices](#6-ps-vita--vita2d-specific-best-practices)

---

## 1. File Organization Principles

### 1.1 Module Structure

Organize UI code into logical modules that separate concerns. For PS Vita projects, follow this recommended structure:

```
vita/
├── src/
│   ├── ui.c                 # Main UI coordinator and render loop
│   ├── ui_components.c      # Reusable UI primitives (buttons, cards, toggles)
│   ├── ui_navigation.c      # Navigation system (sidebar, tabs)
│   ├── ui_particles.c       # Animation systems (particles, waves)
│   ├── ui_views/            # Screen-specific implementations
│   │   ├── main_view.c
│   │   ├── settings_view.c
│   │   ├── profile_view.c
│   │   └── controller_view.c
│   ├── input.c              # Touch and button input handling
│   └── assets.c             # Asset loading and lifecycle management
├── include/
│   ├── ui.h                 # Public UI API (screen types, state structs)
│   ├── ui_components.h      # Component function declarations
│   ├── ui_constants.h       # Layout constants, colors, dimensions
│   └── ui_types.h           # Type definitions (enums, structs)
└── res/
    └── assets/              # PNG/JPG textures, fonts
```

### 1.2 Naming Conventions

#### Files
- Use lowercase with underscores: `ui_components.c`, `main_view.c`
- Prefix related files with common identifier: `ui_*.c` for UI modules
- Header files mirror source files: `ui_components.h` for `ui_components.c`

#### Functions
```c
// Public API: module_action_object pattern
void ui_render_console_card(ConsoleCardInfo* card, int x, int y);
void ui_handle_touch_input(SceTouchData* touch);
bool ui_is_point_in_rect(float px, float py, int rx, int ry, int rw, int rh);

// Internal/static functions: descriptive verb-noun pattern
static void render_status_indicator(int x, int y, ConnectionStatus status);
static void update_particle_positions(void);
static bool check_button_pressed(SceCtrlButtons btn);
```

#### Types
```c
// Enums: SCREAMING_SNAKE_CASE with prefix
typedef enum ui_screen_type_t {
    UI_SCREEN_TYPE_MAIN = 0,
    UI_SCREEN_TYPE_SETTINGS,
    UI_SCREEN_TYPE_PROFILE,
} UIScreenType;

// Structs: PascalCase with descriptive suffix
typedef struct console_card_info_t {
    char name[32];
    int status;
    // ...
} ConsoleCardInfo;

typedef struct card_focus_anim_state_t {
    int focused_card_index;
    float current_scale;
    uint64_t focus_start_us;
} CardFocusAnimState;
```

#### Constants and Macros
```c
// Layout constants: CATEGORY_PROPERTY format
#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544
#define WAVE_NAV_WIDTH 130
#define CONSOLE_CARD_WIDTH 200
#define CONSOLE_CARD_HEIGHT 205

// Colors: UI_COLOR_PURPOSE format (ABGR for vita2d)
#define UI_COLOR_PRIMARY_BLUE 0xFFFF9034
#define UI_COLOR_TEXT_PRIMARY 0xFFFAFAFA
#define UI_COLOR_STATUS_AVAILABLE 0xFF50AF4C

// Animation timing: COMPONENT_PROPERTY_MS format
#define TOGGLE_ANIMATION_DURATION_MS 180
#define NAV_COLLAPSE_DURATION_MS 280
```

### 1.3 Header File Organization

#### Public Header (`ui.h`)
```c
#pragma once
#include <stdbool.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <vita2d.h>

// Only expose what external modules need
typedef struct vita_chiaki_ui_state_t {
    SceTouchData touch_state_front;
    uint32_t button_state;
    uint32_t old_button_state;
    int active_item;
    bool error_popup_active;
} VitaChiakiUIState;

// Public API
void ui_init(void);
void ui_cleanup(void);
void draw_ui(void);
bool ui_is_streaming(void);
```

#### Internal Header (`ui_internal.h` or within source files)
```c
// Keep implementation details private using static
static Particle particles[PARTICLE_COUNT];
static bool particles_initialized = false;

// Forward declarations for internal functions
static void render_error_popup(void);
static void handle_error_popup_input(void);
```

---

## 2. Code Architecture Patterns

### 2.1 Screen State Machine Pattern

Manage UI screens using a state machine approach:

```c
// Screen types enumeration
typedef enum {
    UI_SCREEN_TYPE_MAIN = 0,
    UI_SCREEN_TYPE_SETTINGS,
    UI_SCREEN_TYPE_PROFILE,
    UI_SCREEN_TYPE_STREAM,
} UIScreenType;

// Screen state
static UIScreenType current_screen = UI_SCREEN_TYPE_MAIN;
static UIScreenType next_screen = UI_SCREEN_TYPE_MAIN;

// Main render loop dispatcher
void draw_ui(void) {
    // Handle screen transitions
    if (next_screen != current_screen) {
        on_screen_exit(current_screen);
        current_screen = next_screen;
        on_screen_enter(current_screen);
    }

    // Render current screen
    switch (current_screen) {
        case UI_SCREEN_TYPE_MAIN:
            next_screen = draw_main_screen();
            break;
        case UI_SCREEN_TYPE_SETTINGS:
            next_screen = draw_settings_screen();
            break;
        // ...
    }
}

// Each screen returns next screen type
static UIScreenType draw_main_screen(void) {
    // Render logic...

    // Handle navigation
    if (btn_pressed(SCE_CTRL_TRIANGLE)) {
        return UI_SCREEN_TYPE_SETTINGS;
    }
    return UI_SCREEN_TYPE_MAIN;  // Stay on current screen
}
```

### 2.2 Component-Based Design Pattern

Create reusable, self-contained UI components:

```c
// Component structure with all rendering state
typedef struct {
    int x, y;
    int width, height;
    bool selected;
    bool pressed;
    const char* label;
} ButtonComponent;

// Component rendering function
void render_button(const ButtonComponent* btn) {
    uint32_t bg_color = btn->pressed ? UI_COLOR_PRESSED :
                        btn->selected ? UI_COLOR_SELECTED :
                        UI_COLOR_BUTTON_BG;

    // Background
    draw_rounded_rectangle(btn->x, btn->y, btn->width, btn->height, 8, bg_color);

    // Selection highlight
    if (btn->selected) {
        draw_rounded_rectangle(btn->x - 4, btn->y - 4,
                              btn->width + 8, btn->height + 8,
                              10, UI_COLOR_PRIMARY_BLUE);
    }

    // Label
    int text_x = btn->x + (btn->width - get_text_width(btn->label)) / 2;
    int text_y = btn->y + btn->height / 2 + FONT_SIZE_BODY / 2;
    vita2d_font_draw_text(font, text_x, text_y, UI_COLOR_TEXT_PRIMARY,
                         FONT_SIZE_BODY, btn->label);
}

// Component hit testing
bool is_button_touched(const ButtonComponent* btn, float touch_x, float touch_y) {
    return touch_x >= btn->x && touch_x <= btn->x + btn->width &&
           touch_y >= btn->y && touch_y <= btn->y + btn->height;
}
```

### 2.3 Input Handling Pattern

Separate input handling from rendering with a clear event system:

```c
// Input state management
static uint32_t button_block_mask = 0;
static bool touch_block_active = false;

// Check for newly pressed button (edge detection)
bool btn_pressed(SceCtrlButtons btn) {
    if (button_block_mask & btn) return false;  // Blocked
    return (ui_state.button_state & btn) &&
           !(ui_state.old_button_state & btn);
}

// Block inputs during screen transitions to prevent double-activation
static void block_inputs_for_transition(void) {
    button_block_mask |= ui_state.button_state;
    touch_block_active = true;
}

// Clear blocks at frame end
static void clear_input_blocks(void) {
    if (!(ui_state.button_state & button_block_mask)) {
        button_block_mask = 0;
    }
    if (touch_block_active && ui_state.touch_state_front.reportNum == 0) {
        touch_block_active = false;
    }
}
```

### 2.4 Animation State Machine Pattern

Manage complex animations with explicit state machines:

```c
// Animation states
typedef enum {
    NAV_STATE_EXPANDED = 0,
    NAV_STATE_COLLAPSING,
    NAV_STATE_COLLAPSED,
    NAV_STATE_EXPANDING
} NavSidebarState;

// Animation context
typedef struct {
    NavSidebarState state;
    uint64_t anim_start_us;
    float anim_progress;        // 0.0 to 1.0
    float current_width;        // Interpolated value
} NavCollapseState;

static NavCollapseState nav_collapse = {
    .state = NAV_STATE_COLLAPSED,
    .anim_progress = 0.0f,
    .current_width = 0.0f
};

// Animation update (call once per frame)
static void update_nav_collapse_animation(void) {
    if (nav_collapse.state != NAV_STATE_COLLAPSING &&
        nav_collapse.state != NAV_STATE_EXPANDING) {
        return;  // No animation in progress
    }

    uint64_t now = sceKernelGetProcessTimeWide();
    float elapsed_ms = (float)(now - nav_collapse.anim_start_us) / 1000.0f;
    float progress = elapsed_ms / (float)NAV_COLLAPSE_DURATION_MS;

    if (progress >= 1.0f) {
        // Animation complete
        progress = 1.0f;
        nav_collapse.state = (nav_collapse.state == NAV_STATE_COLLAPSING) ?
                            NAV_STATE_COLLAPSED : NAV_STATE_EXPANDED;
    }

    // Apply easing function for smooth motion
    float eased = ease_in_out_cubic(progress);

    // Update interpolated values
    if (nav_collapse.state == NAV_STATE_COLLAPSING) {
        nav_collapse.current_width = WAVE_NAV_WIDTH * (1.0f - eased);
    } else {
        nav_collapse.current_width = WAVE_NAV_WIDTH * eased;
    }

    nav_collapse.anim_progress = progress;
}

// Easing function for smooth animations
static inline float ease_in_out_cubic(float t) {
    return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;
}
```

### 2.5 Focus Management Pattern

Implement consistent focus handling for controller navigation:

```c
typedef enum {
    FOCUS_NAV_BAR = 0,
    FOCUS_CONSOLE_CARDS = 1,
    FOCUS_ACTION_BUTTONS = 2
} FocusArea;

static FocusArea current_focus = FOCUS_CONSOLE_CARDS;
static int focus_index_per_area[3] = {0, 0, 0};  // Remember selection in each area

// Handle D-pad navigation
static void handle_dpad_navigation(void) {
    if (btn_pressed(SCE_CTRL_LEFT)) {
        // Move focus area left
        if (current_focus > FOCUS_NAV_BAR) {
            current_focus--;
        }
    }
    if (btn_pressed(SCE_CTRL_RIGHT)) {
        // Move focus area right
        if (current_focus < FOCUS_ACTION_BUTTONS) {
            current_focus++;
        }
    }
    if (btn_pressed(SCE_CTRL_UP)) {
        // Navigate within current area
        focus_index_per_area[current_focus]--;
        // Clamp to valid range...
    }
    if (btn_pressed(SCE_CTRL_DOWN)) {
        focus_index_per_area[current_focus]++;
        // Clamp to valid range...
    }
}
```

---

## 3. Resource Management

### 3.1 Texture Lifecycle Management

Load textures once during initialization, free on exit:

```c
// Global texture pointers
static vita2d_texture *img_ps5 = NULL;
static vita2d_texture *icon_settings = NULL;
static vita2d_texture *background = NULL;

// Texture loading with error handling
static vita2d_texture* load_texture_safe(const char* path) {
    vita2d_texture* tex = vita2d_load_PNG_file(path);
    if (!tex) {
        LOGE("Failed to load texture: %s", path);
        return NULL;
    }
    LOGD("Loaded texture: %s (%dx%d)", path,
         vita2d_texture_get_width(tex),
         vita2d_texture_get_height(tex));
    return tex;
}

// Initialize all textures
void ui_load_assets(void) {
    img_ps5 = load_texture_safe("app0:/assets/ps5.png");
    icon_settings = load_texture_safe("app0:/assets/settings.png");
    background = load_texture_safe("app0:/assets/background.png");
    // ... load other textures
}

// Free all textures
void ui_free_assets(void) {
    if (img_ps5) { vita2d_free_texture(img_ps5); img_ps5 = NULL; }
    if (icon_settings) { vita2d_free_texture(icon_settings); icon_settings = NULL; }
    if (background) { vita2d_free_texture(background); background = NULL; }
    // ... free other textures
}
```

### 3.2 Font Management

```c
static vita2d_font *font = NULL;
static vita2d_font *font_mono = NULL;

void ui_init_fonts(void) {
    font = vita2d_load_font_file("app0:/assets/Roboto-Regular.ttf");
    if (!font) {
        // Fallback to system font or handle error
        LOGE("Failed to load font, using default");
    }

    font_mono = vita2d_load_font_file("app0:/assets/RobotoMono-Regular.ttf");
}

void ui_free_fonts(void) {
    if (font) { vita2d_free_font(font); font = NULL; }
    if (font_mono) { vita2d_free_font(font_mono); font_mono = NULL; }
}
```

### 3.3 Memory Allocation Strategies

For embedded systems, prefer static allocation over dynamic:

```c
// BAD: Dynamic allocation per frame
void render_particles_bad(void) {
    Particle* particles = malloc(PARTICLE_COUNT * sizeof(Particle));  // AVOID
    // ... render
    free(particles);
}

// GOOD: Static allocation
static Particle particles[PARTICLE_COUNT];
static bool particles_initialized = false;

void render_particles_good(void) {
    if (!particles_initialized) {
        init_particles();
        particles_initialized = true;
    }
    // Use statically allocated array
}

// GOOD: Pre-allocated buffers for strings
#define MAX_TOOLTIP_CHARS 200
static char tooltip_buffer[MAX_TOOLTIP_CHARS];

void set_tooltip(const char* msg) {
    strncpy(tooltip_buffer, msg, MAX_TOOLTIP_CHARS - 1);
    tooltip_buffer[MAX_TOOLTIP_CHARS - 1] = '\0';
}
```

### 3.4 Cache Expensive Calculations

```c
// Text width cache to avoid repeated font measurements
#define TEXT_WIDTH_CACHE_SIZE 16

typedef struct {
    const char* text;      // Pointer comparison for static strings
    int font_size;
    int width;
    bool valid;
} TextWidthCacheEntry;

static TextWidthCacheEntry text_width_cache[TEXT_WIDTH_CACHE_SIZE] = {0};

int get_text_width_cached(const char* text, int font_size) {
    // Check cache
    for (int i = 0; i < TEXT_WIDTH_CACHE_SIZE; i++) {
        if (text_width_cache[i].valid &&
            text_width_cache[i].text == text &&  // Pointer comparison
            text_width_cache[i].font_size == font_size) {
            return text_width_cache[i].width;
        }
    }

    // Calculate and cache
    int width = vita2d_font_text_width(font, font_size, text);

    // FIFO replacement
    static int next_slot = 0;
    text_width_cache[next_slot].text = text;
    text_width_cache[next_slot].font_size = font_size;
    text_width_cache[next_slot].width = width;
    text_width_cache[next_slot].valid = true;
    next_slot = (next_slot + 1) % TEXT_WIDTH_CACHE_SIZE;

    return width;
}
```

---

## 4. Maintainability Guidelines

### 4.1 Don't Repeat Yourself (DRY)

Extract common patterns into reusable functions:

```c
// BAD: Repeated rectangle drawing code
void render_card_bad(void) {
    vita2d_draw_rectangle(x, y, w, h, UI_COLOR_CARD_BG);
    vita2d_draw_rectangle(x-4, y-4, w+8, h+8, UI_COLOR_SHADOW);
}

void render_button_bad(void) {
    vita2d_draw_rectangle(x, y, w, h, UI_COLOR_BUTTON_BG);
    vita2d_draw_rectangle(x-4, y-4, w+8, h+8, UI_COLOR_SHADOW);
}

// GOOD: Reusable component
void draw_card_with_shadow(int x, int y, int w, int h, int radius, uint32_t color) {
    // Shadow layer
    draw_rounded_rectangle(x + 2, y + 2, w, h, radius, UI_COLOR_SHADOW);
    // Main card
    draw_rounded_rectangle(x, y, w, h, radius, color);
}

void render_card_good(void) {
    draw_card_with_shadow(x, y, w, h, 12, UI_COLOR_CARD_BG);
}
```

### 4.2 Create Reusable Primitive Components

```c
// Rounded rectangle primitive (vita2d doesn't have this built-in)
void draw_rounded_rectangle(int x, int y, int width, int height,
                           int radius, uint32_t color) {
    if (radius <= 0) {
        vita2d_draw_rectangle(x, y, width, height, color);
        return;
    }

    // Clamp radius
    int max_radius = (width < height ? width : height) / 2;
    if (radius > max_radius) radius = max_radius;

    // Draw body rectangles
    vita2d_draw_rectangle(x + radius, y, width - 2 * radius, height, color);
    vita2d_draw_rectangle(x, y + radius, radius, height - 2 * radius, color);
    vita2d_draw_rectangle(x + width - radius, y + radius,
                         radius, height - 2 * radius, color);

    // Draw corner circles
    draw_circle(x + radius, y + radius, radius, color);
    draw_circle(x + width - radius, y + radius, radius, color);
    draw_circle(x + radius, y + height - radius, radius, color);
    draw_circle(x + width - radius, y + height - radius, radius, color);
}

// Status indicator component
void render_status_ellipse(int x, int y, ConnectionStatus status) {
    vita2d_texture* tex = NULL;
    switch (status) {
        case STATUS_AVAILABLE: tex = ellipse_green; break;
        case STATUS_UNAVAILABLE: tex = ellipse_red; break;
        case STATUS_CONNECTING: tex = ellipse_yellow; break;
    }
    if (tex) {
        vita2d_draw_texture(tex, x, y);
    }
}
```

### 4.3 Documentation Standards

```c
/**
 * render_console_card() - Render a console card at the specified position
 * @card: Pointer to console card info structure
 * @x: X coordinate of card top-left corner
 * @y: Y coordinate of card top-left corner
 * @selected: Whether this card is currently selected/focused
 *
 * Renders a complete console card including:
 * - Background with optional selection glow
 * - Console logo (PS4/PS5)
 * - Console name bar
 * - Status indicator (green/yellow/red ellipse)
 * - State text ("Ready" / "Standby")
 *
 * Card dimensions are CONSOLE_CARD_WIDTH x CONSOLE_CARD_HEIGHT (200x205 px).
 *
 * Reference: docs/ai/UI_FINAL_SPECIFICATION.md, Section 3
 */
void render_console_card(const ConsoleCardInfo* card, int x, int y, bool selected);

// Inline comments for non-obvious logic
// Calculate wave offset: use sine wave with phase offset per icon
// for subtle "floating" effect as specified in UI_FINAL_SPECIFICATION.md
float wave_offset = sinf(wave_animation_time + i * 0.5f) * 3.0f;
```

### 4.4 Constants Organization

Centralize UI constants in a dedicated header:

```c
// ui_constants.h

#pragma once

// ============================================================================
// Screen Dimensions
// ============================================================================
#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544

// ============================================================================
// Layout System (per docs/ai/UI_FINAL_SPECIFICATION.md)
// ============================================================================
#define WAVE_NAV_WIDTH 130
#define CONTENT_AREA_X WAVE_NAV_WIDTH
#define CONTENT_AREA_WIDTH (SCREEN_WIDTH - WAVE_NAV_WIDTH)  // 830px

#define MARGIN_STANDARD 16
#define MARGIN_LARGE 32
#define CARD_PADDING 20

// ============================================================================
// Typography
// ============================================================================
#define FONT_SIZE_HEADER 24
#define FONT_SIZE_SUBHEADER 18
#define FONT_SIZE_BODY 16
#define FONT_SIZE_SMALL 14

// ============================================================================
// Colors (ABGR format for vita2d)
// ============================================================================
#define UI_COLOR_PRIMARY_BLUE    0xFFFF9034  // PlayStation Blue #3490FF
#define UI_COLOR_BACKGROUND      0xFF1A1614  // Dark background
#define UI_COLOR_CARD_BG         0xFF37322D  // Card background
#define UI_COLOR_TEXT_PRIMARY    0xFFFAFAFA  // Primary text (white)
#define UI_COLOR_TEXT_SECONDARY  0xFFB4B4B4  // Secondary text (gray)

// ============================================================================
// Animation Timing
// ============================================================================
#define TOGGLE_ANIMATION_DURATION_MS 180
#define NAV_COLLAPSE_DURATION_MS 280
#define CARD_FOCUS_DURATION_MS 180
```

### 4.5 Testing Strategies

While automated UI testing on PS Vita is limited, structure code for testability:

```c
// Separate pure logic from rendering for easier testing
// Pure function: can be unit tested
bool is_point_in_rect(float px, float py, int rx, int ry, int rw, int rh) {
    return px >= rx && px <= rx + rw && py >= ry && py <= ry + rh;
}

// Rendering function: requires hardware
void render_button(const ButtonComponent* btn) {
    // ... vita2d calls
}

// Create mock-friendly interfaces
#ifdef UNIT_TESTS
#define vita2d_draw_rectangle(x, y, w, h, c) mock_draw_rectangle(x, y, w, h, c)
#endif
```

---

## 5. Performance Considerations

### 5.1 Draw Call Optimization

Minimize draw calls by batching and avoiding redundant operations:

```c
// BAD: Multiple small rectangles
for (int i = 0; i < 100; i++) {
    vita2d_draw_rectangle(x + i, y, 1, h, color);  // 100 draw calls!
}

// GOOD: Single rectangle
vita2d_draw_rectangle(x, y, 100, h, color);  // 1 draw call

// BAD: Redrawing unchanged elements
void render_frame(void) {
    render_static_background();  // Same every frame
    render_dynamic_content();
}

// GOOD: Cache static content or skip unchanged regions
static bool background_dirty = true;

void render_frame(void) {
    if (background_dirty) {
        render_static_background();
        background_dirty = false;
    }
    render_dynamic_content();
}
```

### 5.2 Texture Batching

Reuse textures and combine small textures into atlases when possible:

```c
// For particle systems, load each symbol once
static vita2d_texture* symbol_textures[4];

void init_particle_textures(void) {
    symbol_textures[0] = vita2d_load_PNG_file("app0:/assets/symbol_triangle.png");
    symbol_textures[1] = vita2d_load_PNG_file("app0:/assets/symbol_circle.png");
    symbol_textures[2] = vita2d_load_PNG_file("app0:/assets/symbol_ex.png");
    symbol_textures[3] = vita2d_load_PNG_file("app0:/assets/symbol_square.png");
}

// Render all particles using shared textures
void render_particles(void) {
    for (int i = 0; i < PARTICLE_COUNT; i++) {
        vita2d_texture* tex = symbol_textures[particles[i].symbol_type];
        vita2d_draw_texture_scale_rotate(tex,
            particles[i].x, particles[i].y,
            particles[i].scale, particles[i].scale,
            particles[i].rotation);
    }
}
```

### 5.3 Avoid Per-Frame Allocations

```c
// BAD: String formatting with malloc
void render_fps_bad(int fps) {
    char* buffer = malloc(32);  // Allocation every frame!
    sprintf(buffer, "FPS: %d", fps);
    vita2d_font_draw_text(font, x, y, color, size, buffer);
    free(buffer);
}

// GOOD: Static buffer
void render_fps_good(int fps) {
    static char buffer[32];
    sprintf(buffer, "FPS: %d", fps);
    vita2d_font_draw_text(font, x, y, color, size, buffer);
}
```

### 5.4 Reduce Animation Update Frequency

For background animations, 30fps updates are indistinguishable from 60fps:

```c
static int particle_update_frame = 0;

void update_particles(void) {
    // Update particles every other frame (30fps instead of 60fps)
    particle_update_frame++;
    if (particle_update_frame % 2 != 0) return;

    for (int i = 0; i < PARTICLE_COUNT; i++) {
        // Double the movement to compensate for half update rate
        particles[i].x += particles[i].vx * 2.0f;
        particles[i].y += particles[i].vy * 2.0f;
        particles[i].rotation += particles[i].rotation_speed * 2.0f;
    }
}
```

### 5.5 Efficient Layout Calculations

Cache layout calculations that don't change:

```c
// Layout structure for caching
typedef struct {
    int card_x, card_y;
    int button_x, button_y;
    bool calculated;
} MainScreenLayout;

static MainScreenLayout main_layout = {0};

void calculate_main_layout(void) {
    if (main_layout.calculated) return;

    // Calculate once
    main_layout.card_x = (SCREEN_WIDTH - CONSOLE_CARD_WIDTH) / 2;
    main_layout.card_y = 150;
    main_layout.button_x = (SCREEN_WIDTH - 120) / 2;
    main_layout.button_y = SCREEN_HEIGHT - 80;
    main_layout.calculated = true;
}

void invalidate_main_layout(void) {
    main_layout.calculated = false;
}
```

---

## 6. PS Vita / vita2d Specific Best Practices

### 6.1 Memory Constraints

The PS Vita has 512MB RAM, but available application memory is much less:

```c
// Memory budget guidelines for UI
// - Total UI textures: < 15MB
// - Per-texture recommendation: < 512KB
// - Particle count: 8-12 (balance visuals vs memory)

#define PARTICLE_COUNT 8  // Reduced from 12 for better performance

// Compress textures using pngquant before including
// $ pngquant --quality=65-80 --output optimized/ source/*.png
```

### 6.2 Resolution Awareness

Always design for 960x544 native resolution:

```c
// Centered content helper
int center_x(int width) {
    return (SCREEN_WIDTH - width) / 2;
}

int center_y(int height) {
    return (SCREEN_HEIGHT - height) / 2;
}

// Touch coordinate conversion (touch panel is 1920x1088)
float touch_to_screen_x(int touch_x) {
    return (float)touch_x / 1920.0f * SCREEN_WIDTH;
}

float touch_to_screen_y(int touch_y) {
    return (float)touch_y / 1088.0f * SCREEN_HEIGHT;
}
```

### 6.3 vita2d Initialization and Cleanup

```c
void ui_init(void) {
    // Initialize vita2d
    vita2d_init();
    vita2d_set_clear_color(UI_COLOR_BACKGROUND);

    // Enable MSAA for smoother edges (optional, costs performance)
    // vita2d_set_vblank_wait(1);  // VSync

    // Load assets
    ui_init_fonts();
    ui_load_assets();

    // Initialize subsystems
    init_particles();
}

void ui_cleanup(void) {
    // Free in reverse order of initialization
    ui_free_assets();
    ui_free_fonts();

    // Shutdown vita2d
    vita2d_fini();
}
```

### 6.4 Common vita2d Drawing Functions

```c
// Basic shapes
vita2d_draw_rectangle(x, y, w, h, color);           // Filled rectangle
vita2d_draw_line(x1, y1, x2, y2, color);            // Line
vita2d_draw_fill_circle(cx, cy, radius, color);    // Filled circle

// Texture drawing
vita2d_draw_texture(tex, x, y);                                    // Basic
vita2d_draw_texture_scale(tex, x, y, sx, sy);                      // Scaled
vita2d_draw_texture_rotate(tex, x, y, rad);                        // Rotated
vita2d_draw_texture_scale_rotate(tex, x, y, sx, sy, rad);          // Both
vita2d_draw_texture_tint(tex, x, y, color);                        // Tinted
vita2d_draw_texture_part(tex, x, y, tex_x, tex_y, tex_w, tex_h);   // Partial

// Font drawing
vita2d_font_draw_text(font, x, y, color, size, text);
int width = vita2d_font_text_width(font, size, text);
int height = vita2d_font_text_height(font, size, text);
```

### 6.5 Color Format (ABGR)

vita2d uses ABGR format, not RGBA:

```c
// RGBA8 macro converts from standard RGBA to ABGR
#define RGBA8(r, g, b, a) ((a << 24) | (b << 16) | (g << 8) | r)

// Examples
#define COLOR_WHITE     RGBA8(255, 255, 255, 255)  // 0xFFFFFFFF
#define COLOR_BLACK     RGBA8(0, 0, 0, 255)        // 0xFF000000
#define COLOR_RED       RGBA8(255, 0, 0, 255)      // 0xFF0000FF
#define COLOR_GREEN     RGBA8(0, 255, 0, 255)      // 0xFF00FF00
#define COLOR_BLUE      RGBA8(0, 0, 255, 255)      // 0xFFFF0000

// PlayStation Blue (#3490FF in web format)
// R=0x34, G=0x90, B=0xFF, A=0xFF
#define PS_BLUE RGBA8(0x34, 0x90, 0xFF, 0xFF)      // 0xFFFF9034
```

### 6.6 Frame Rendering Pattern

```c
void render_frame(void) {
    // Start frame
    vita2d_start_drawing();
    vita2d_clear_screen();

    // Render layers back-to-front
    render_background();
    render_particles();
    render_navigation();
    render_content();
    render_overlays();

    // End frame
    vita2d_end_drawing();
    vita2d_swap_buffers();
}
```

### 6.7 Input Handling

```c
// Controller input
SceCtrlData ctrl;
sceCtrlPeekBufferPositive(0, &ctrl, 1);

// Store previous state for edge detection
static uint32_t old_buttons = 0;
bool cross_pressed = (ctrl.buttons & SCE_CTRL_CROSS) &&
                    !(old_buttons & SCE_CTRL_CROSS);
old_buttons = ctrl.buttons;

// Touch input
SceTouchData touch;
sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

if (touch.reportNum > 0) {
    float x = touch.report[0].x / 1920.0f * 960.0f;
    float y = touch.report[0].y / 1088.0f * 544.0f;
    // Handle touch at (x, y)
}
```

### 6.8 Thread Safety Considerations

UI rendering should happen on the main thread. If background threads update UI state:

```c
// Shared state protection
static volatile bool discovery_in_progress = false;
static volatile int discovered_host_count = 0;

// In discovery thread
void on_host_discovered(void) {
    // Simple atomic operations are safe
    discovered_host_count++;
}

// In UI thread
void render_discovery_status(void) {
    // Read volatile variables - no lock needed for simple reads
    if (discovery_in_progress) {
        draw_spinner();
        render_text("Discovering... (%d found)", discovered_host_count);
    }
}
```

---

## Appendix A: Quick Reference Card

### Essential Defines
```c
#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544
#define RGBA8(r,g,b,a) ((a<<24)|(b<<16)|(g<<8)|r)
```

### Common Operations
```c
// Draw centered text
int w = vita2d_font_text_width(font, size, text);
vita2d_font_draw_text(font, (960-w)/2, y, color, size, text);

// Touch hit test
bool hit = (tx >= x && tx <= x+w && ty >= y && ty <= y+h);

// Button edge detection
bool pressed = (buttons & btn) && !(old_buttons & btn);

// Easing function
float ease(float t) { return t<0.5f ? 4*t*t*t : 1-powf(-2*t+2,3)/2; }
```

### Performance Checklist
- [ ] Textures loaded once at startup
- [ ] No per-frame malloc/free
- [ ] Static buffers for strings
- [ ] Layout calculations cached
- [ ] Background animations at 30fps
- [ ] Draw calls minimized

---

## Appendix B: VitaRPS5 Codebase References

| File | Purpose | Key Lines |
|------|---------|-----------|
| `vita/src/ui.c` | Main UI implementation | Lines 1-300: Constants, 1500+: Rendering |
| `vita/include/ui.h` | Public UI interface | UIState struct, screen types |
| `vita/include/context.h` | Global context | VitaChiakiContext struct |
| `docs/ai/UI_FINAL_SPECIFICATION.md` | Design specification | Full UI design document |
| `docs/ai/UI_DEVELOPMENT_GUIDE.md` | Development patterns | Code examples and patterns |

---

**Document Maintainer:** VitaRPS5 Development Team
**Last Reviewed:** December 2025
