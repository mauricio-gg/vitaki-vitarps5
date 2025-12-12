# UI.C Comprehensive Analysis Report

**Generated:** December 2025
**File Analyzed:** `vita/src/ui.c`
**Purpose:** Detailed component and responsibility mapping for refactoring

---

## 1. FILE STATISTICS

- **Total Lines**: 4,776
- **Total Functions**: ~80+ functions (includes procedural drawing functions)
- **Global Variables**: 40+ state management variables
- **Data Structures**: 15+ typedef structs/enums
- **Include Dependencies**: vita2d, psp2/ctrl, psp2/touch, psp2/message_dialog, psp2/registrymgr, psp2/ime_dialog, psp2/kernel
- **External APIs**: Chiaki streaming library, VitaSDK

---

## 2. COMPONENT INVENTORY

### Main UI Screens (9 total)
| Screen Type | Description | Approximate Lines |
|-------------|-------------|-------------------|
| `UI_SCREEN_TYPE_MAIN` | Primary console selection menu with card grid | 2808-2984 |
| `UI_SCREEN_TYPE_SETTINGS` | Streaming quality, controller, video settings | 3171-3291 |
| `UI_SCREEN_TYPE_PROFILE` | User profile and connection info display | 3557-3614 |
| `UI_SCREEN_TYPE_CONTROLLER` | Controller configuration and button mappings | 3882+ |
| `UI_SCREEN_TYPE_REGISTER_HOST` | Host registration dialog with PIN entry | 4085+ |
| `UI_SCREEN_TYPE_WAKING` | Console wake/connection overlay with spinner | 4253-4387 |
| `UI_SCREEN_TYPE_RECONNECTING` | Packet loss recovery screen with status | 4392-4468 |
| `UI_SCREEN_TYPE_STREAM` | Streaming/video output overlay | 4198+ |
| `UI_SCREEN_TYPE_MESSAGES` | Debug message log viewer | 4472-4590 |

### Navigation System
- **Wave Navigation Sidebar** - Collapsible left sidebar with 4 navigation icons
- **Navigation Pill** - Compact pill-shaped button when sidebar is collapsed (140x44px)
- **Navigation Collapse State Machine** - 4-state animator (EXPANDED, COLLAPSING, COLLAPSED, EXPANDING)
- **Wave Animation** - Dual-layer animated sine wave background in nav area
- **Focus System** - Dual-focus area model: FOCUS_NAV_BAR, FOCUS_CONSOLE_CARDS

### Console Card System
- **Console Grid** - Dynamic card-based layout for multiple PS4/PS5 devices
- **Console Cards** - Individual 200x205px cards with status indicators, logos, names
- **Console Card Cache** - Prevents flickering during discovery (10-second update interval)
- **Card Focus Animation** - Scale-based focus animation (0.95 to 1.0 scale)
- **Card Selection** - Color-coded status borders (blue=ready, yellow=standby, grey=unpaired)

### UI Components (Reusable)
- **Toggle Switches** - Animated on/off controls with easing
- **Dropdowns** - Selection controls with label+value display
- **Tab Bars** - Multi-section navigation with colored tabs
- **Status Dots** - Circular indicators (green/yellow/red) for host status
- **Spinner** - Rotating arc animation for loading states
- **Rounded Rectangles** - Fundamental drawing primitive with radius control
- **Text Rendering** - Font-based text with alignment and color support

### Dialog Systems
- **Error Popup** - Centered modal for connection/streaming errors
- **Hints System** - Bottom-positioned pill hints with fade in/out
- **Connection Overlay** - Waking/connecting feedback with stage transitions
- **Registration Dialog** - PIN entry interface (8-digit numerical input)

### Particle Background System
- **Particle Array** - 8 floating symbols with physics (velocity, rotation, scale)
- **Particle Layers** - 2-speed layers for depth (background 0.7x, foreground 1.0x)
- **Particle Sway Animation** - Horizontal oscillation per particle
- **Symbol Types** - 4 procedural shapes (triangle, circle, X, square)

---

## 3. RESPONSIBILITY MAPPING

### A. Initialization & Cleanup Functions
```
void init_ui()                         - Font/texture loading, touch setup, input binding
void load_textures()                   - Loads all PNG assets from app0:/assets/
void init_particles()                  - Initializes 8-particle system with random state
void reset_pin_entry()                 - Clears PIN entry state for new registration
void load_psn_id_if_needed()          - Loads PSN account ID from config or IME dialog
```

### B. Drawing/Rendering Functions (Primary)
```
void draw_ui()                         - Main rendering loop, screen dispatcher
UIScreenType draw_main_menu()          - Console selection with D-pad/touch navigation
UIScreenType draw_settings()           - Single-tab settings with 10 toggles/dropdowns
UIScreenType draw_profile_screen()     - Two-card layout: profile + connection info
UIScreenType draw_controller_config_screen() - Tab-based controller setup
bool draw_registration_dialog()        - PIN entry dialog for new hosts
bool draw_stream()                     - Stream quality/status overlay
UIScreenType draw_waking_screen()      - Connection spinner with stage feedback
UIScreenType draw_reconnecting_screen() - Packet loss recovery with attempt counter
bool draw_messages()                   - Message log viewer with scrolling
```

### C. Component Rendering (Reusable)
```
void render_wave_navigation()          - Animated sidebar + pill + wave background
void render_nav_pill()                 - Collapsed nav indicator (36x44px pill)
void render_console_grid()             - Card-based console display (vertically stacked)
void render_console_card(...)          - Individual PS4/PS5 card with logo/status
void render_particles()                - Draws all 8 floating symbols
void render_error_popup()              - Centered error dialog overlay
void render_debug_menu()               - Optional debug option selector
void render_loss_indicator_preview()   - Network instability badge (top-right)
void render_hints_popup()              - Bottom fade-in/out hint pill
void render_hints_indicator()          - "(Select) Hints" text in top-right
void render_nav_collapse_toast()       - Transient toast below pill after collapse
void render_content_focus_overlay()    - Semi-transparent tint when nav expanded
```

### D. Drawing Primitives
```
static void draw_rounded_rectangle()   - O(radius) efficient rounded rect drawing
static void draw_card_with_shadow()    - Card + shadow pair (used by many screens)
static void draw_circle()              - Filled circle rasterization
static void draw_circle_outline_simple() - Circle outline using sine/cosine segments
static void draw_hamburger_icon()      - 3-line icon for nav pill
static void draw_toggle_switch()       - Animated track + knob with easing
static void draw_dropdown()            - Label + value + arrow control
static void draw_tab_bar()             - Multi-tab navigation bar
static void draw_status_dot()          - Status indicator (green/yellow/red)
static void draw_section_header()      - Accent bar with title
static void draw_spinner()             - Rotating arc for loading animation
```

### E. Procedural Icon Rendering
```
void draw_play_icon()                  - White triangle (play symbol)
void draw_settings_icon()              - Gear with center circle + teeth
void draw_controller_icon()            - Gamepad body with D-pad + buttons
void draw_profile_icon()               - User head + shoulders silhouette
```

### F. Animation Functions
```
static void update_nav_collapse_animation()    - 280ms sidebar collapse animator
static void update_nav_toast()                 - Toast visibility timeout (2s fade)
static void update_wave_animation()            - Phase accumulation for sine waves
static void update_particles()                 - 30fps particle physics update
static void update_card_focus_animation()      - Card scale focus animation
static void update_cursor_blink()              - PIN entry cursor blink toggle
static void start_toggle_animation()           - Initialize 180ms toggle tween
static float get_toggle_animation_value()      - Compute toggle animation progress
static float get_card_scale()                  - Get scale for focused/unfocused cards
```

### G. Input Handling Functions
```
bool btn_pressed(SceCtrlButtons btn)                      - Edge-triggered button detection
UIScreenType handle_vitarps5_touch_input()               - Touch screen input routing
static bool handle_global_nav_shortcuts()                - Global D-pad/touch navigation
static bool nav_touch_hit()                              - Navigation icon hit detection
static bool pill_touch_hit()                             - Collapsed pill hit detection
void handle_error_popup_input()                          - Error dialog dismissal
void handle_debug_menu_input()                           - Debug menu option selection
```

### H. State Management Functions
```
static void nav_request_collapse()              - Initiate sidebar collapse animation
static void nav_request_expand()                - Initiate sidebar expand animation
static void nav_toggle_collapse()               - Toggle collapse state
static void nav_reset_to_collapsed()            - Force collapsed state
static void show_nav_collapse_toast()           - Trigger collapse notification
static void block_inputs_for_transition()       - Disable inputs during screen transitions
void ui_connection_begin()                      - Start connection overlay with stage
void ui_connection_set_stage()                  - Update connection progress stage
void ui_connection_complete()                   - Close connection overlay on success
void ui_connection_cancel()                     - Close connection overlay on cancel
bool ui_connection_overlay_active()             - Query overlay visibility
void ui_clear_waking_wait()                     - Reset waking timer state
static void start_connection_thread()           - Spawn async streaming thread
```

### I. Utility & Helper Functions
```
static bool stream_cooldown_active()            - Check network recovery cooldown
static uint64_t stream_cooldown_until_us()      - Get cooldown expiration timestamp
static bool takion_cooldown_gate_active()       - Gate streaming on cooldown
static int get_text_width_cached()              - Cached text measurement (16 entries)
static inline float lerp()                      - Linear interpolation helper
static inline float ease_in_out_cubic()         - Easing function for animations
static bool is_touched()                        - Legacy touch hit detection
bool is_point_in_circle()                       - Circle collision detection
bool is_point_in_rect()                         - Rectangle collision detection
static bool is_pin_complete()                   - Check if 8 digits entered
uint32_t pin_to_number()                        - Convert pin_entry_state to uint32
static UIScreenType nav_screen_for_index()      - Map nav icon index to screen
static const char* get_resolution_string()      - Resolution preset formatter
static const char* get_fps_string()             - FPS preset formatter
static const char* get_latency_mode_string()    - Latency mode formatter
void map_host_to_console_card()                 - Convert VitaChiakiHost to ConsoleCardInfo
void update_console_card_cache()                - Refresh card data (10-second interval)
void trigger_hints_popup()                      - Activate hints display
```

### J. Data Conversion Functions
```
void render_pin_digit()                         - Render single PIN digit with cursor
static void get_button_mappings()               - Dynamically generate button map table
static const char* get_scheme_name()            - Map ID to scheme label
static void draw_settings_streaming_tab()       - Settings content renderer
static void draw_settings_controller_tab()      - Controller settings renderer
static void draw_profile_card()                 - Profile info card renderer
static void draw_connection_info_card()         - Connection details card renderer
static void draw_registration_section()         - Registration widget renderer
static void draw_controller_mappings_tab()      - Controller mapping table renderer
static void draw_controller_settings_tab()      - Controller toggle/dropdown renderer
static void apply_force_30fps_runtime()         - Apply FPS clamping to stream
```

### K. Debug Functions
```
void open_debug_menu()                  - Activate debug option selector
void close_debug_menu()                 - Dismiss debug menu
void debug_menu_apply_action()          - Execute selected debug action
void ensure_active_host_for_debug()     - Ensure active_host set for debug
static void render_debug_menu()         - Debug menu visual rendering
static void handle_debug_menu_input()   - Debug menu input handling
```

---

## 4. DATA STRUCTURES

### Global State Variables
```c
// Texture Management
vita2d_font* font, *font_mono
vita2d_texture *img_ps4, *img_ps4_off, *img_ps5, *img_ps5_off
vita2d_texture *symbol_triangle, *symbol_circle, *symbol_ex, *symbol_square
vita2d_texture *wave_top, *wave_bottom
vita2d_texture *ellipse_green, *ellipse_yellow, *ellipse_red
vita2d_texture *icon_play, *icon_settings, *icon_controller, *icon_profile
vita2d_texture *background_gradient, *vita_rps5_logo, *vita_front, *ps5_logo

// Input State
uint32_t button_block_mask              // Blocked input buttons during transitions
bool touch_block_active                 // Touch input disabled during interactions
bool touch_block_pending_clear

// Navigation State
int selected_nav_icon                   // Current focus: 0=Play, 1=Settings, 2=Controller, 3=Profile
FocusArea current_focus                 // FOCUS_NAV_BAR or FOCUS_CONSOLE_CARDS
int last_console_selection

// Particle System
Particle particles[PARTICLE_COUNT]      // 8-particle array
bool particles_initialized
int particle_update_frame               // Frame counter for 30fps updates

// Wave Animation
WaveLayerState wave_bottom_state, wave_top_state
uint64_t wave_last_update_us

// Navigation Collapse State Machine
NavCollapseState nav_collapse           // Manages sidebar collapse/expand animation

// Console Card System
int selected_console_index
ConsoleCardCache card_cache             // Last 10-second cache of console cards
CardFocusAnimState card_focus_anim      // Scale animation for focused card

// Animation State
ToggleAnimationState toggle_anim        // Tracks active toggle animation
HintsPopupState hints_popup

// Connection Overlay
ConnectionOverlayState connection_overlay
SceUID connection_thread_id
VitaChiakiHost *connection_thread_host

// Waking/Reconnection State
uint32_t waking_start_time, reconnect_start_time, reconnect_animation_frame

// PIN Entry
PinEntryState pin_entry_state
bool show_cursor, pin_entry_initialized
uint32_t cursor_blink_timer

// Settings State
SettingsState settings_state
ControllerState controller_state
ProfileState profile_state

// Text Width Cache
TextWidthCacheEntry text_width_cache[TEXT_WIDTH_CACHE_SIZE]

// Config State
int SCE_CTRL_CONFIRM, SCE_CTRL_CANCEL
char* confirm_btn_str, *cancel_btn_str
```

### Key Type Definitions
```c
enum UIScreenType {          // Screen type identifiers
  UI_SCREEN_TYPE_MAIN, REGISTER, REGISTER_HOST, STREAM,
  WAKING, RECONNECTING, SETTINGS, MESSAGES, PROFILE, CONTROLLER
}

enum ui_host_action_t {      // Host operations
  UI_HOST_ACTION_NONE, WAKEUP, STREAM, DELETE, EDIT, REGISTER
}

enum NavSidebarState {        // Sidebar collapse states
  NAV_STATE_EXPANDED, COLLAPSING, COLLAPSED, EXPANDING
}

struct Particle {             // Animated background symbol
  float x, y, vx, vy, scale, rotation, rotation_speed, sway_phase, sway_speed
  int symbol_type, layer
  uint32_t color
  bool active
}

struct ConsoleCardInfo {      // Card representation
  char name[32], ip_address[16]
  int status (0=Available, 1=Unavailable, 2=Connecting)
  int state (0=Unknown, 1=Ready, 2=Standby)
  bool is_registered, is_discovered
  VitaChiakiHost* host
}

struct NavCollapseState {     // Sidebar animation control
  NavSidebarState state
  uint64_t anim_start_us
  float anim_progress, current_width, pill_width, pill_opacity
  bool toast_shown_this_session, toast_active
  uint64_t toast_start_us
  float stored_wave_bottom_phase, stored_wave_top_phase
}

struct CardFocusAnimState {   // Focus animation per card
  int focused_card_index, previous_focused_card_index
  float current_scale
  uint64_t focus_start_us, unfocus_start_us
}

struct ToggleAnimationState { // Toggle tween tracking
  int animating_index
  bool target_state
  uint64_t start_time_us
}

struct PinEntryState {        // PIN input state
  uint32_t pin_digits[8]      // 0-9, or 10 for empty
  int current_digit
  bool pin_complete
  uint32_t complete_pin
}
```

---

## 5. DEPENDENCIES

### External APIs (VitaSDK/Chiaki)
- **vita2d**: Graphics rendering (drawing, texture, font)
- **psp2/ctrl**: Button input reading
- **psp2/touch**: Touch screen data
- **psp2/message_dialog**: Modal dialogs
- **psp2/ime_dialog**: Text input for PIN/host
- **psp2/registrymgr**: Registry access for config
- **psp2/kernel/processmgr**: Process timing (`sceKernelGetProcessTimeWide`)
- **psp2/kernel/threadmgr**: Thread creation/management
- **Chiaki**: Base64 encoding, host discovery, streaming

### Internal Dependencies
- **context.h**: Global app state (hosts, config, stream)
- **host.h**: Host wakeup/streaming functions
- **ui.h**: Public UI interface declarations
- **util.h**: Utility functions
- **video.h**: Video stream overlays

### Font Resources
- `app0:/assets/fonts/Roboto-Regular.ttf` - Main UI font
- `app0:/assets/fonts/RobotoMono-Regular.ttf` - Monospace (debug log)

### Asset Resources (PNG)
- Console icons: `ps4.png`, `ps4_off.png`, `ps4_rest.png`, `ps5.png`, `ps5_off.png`, `ps5_rest.png`
- UI symbols: `symbol_triangle.png`, `symbol_circle.png`, `symbol_ex.png`, `symbol_square.png`
- Waves: `wave_top.png`, `wave_bottom.png`
- Status: `ellipse_green.png`, `ellipse_yellow.png`, `ellipse_red.png`
- Navigation: `icon_play.png`, `icon_settings.png`, `icon_controller.png`, `icon_profile.png`
- Logos: `Vita_RPS5_Logo.png`, `Vita_Front.png`, `PS5_logo.png`, `background.png`, `button_add_new.png`, `discovered_host.png`

---

## 6. CROSS-CUTTING CONCERNS

### Animation Framework
- **Easing**: `ease_in_out_cubic()` used across all animations
- **Lerp**: `lerp()` helper for smooth interpolation
- **Timing**: `sceKernelGetProcessTimeWide()` for frame-independent animation
- **Patterns**:
  - Toggle animation: 180ms cubic easing
  - Nav collapse: 280ms with 3 phases (80ms prep, 120ms collapse, 80ms pill)
  - Card focus: Scale 0.95→1.0 over 180ms
  - Wave animation: Phase accumulation with 2 layers at different speeds
  - Particle update: 30fps (alternating frames)

### Input Handling Pattern
1. **Button Press Detection**: `btn_pressed()` blocks duplicates via old_button_state
2. **Touch Input**: Normalized to 960x544 from 1920x1088 touchscreen
3. **Block Mechanism**: `button_block_mask` and `touch_block_active` prevent double-processing
4. **Global Shortcuts**: Triangle (toggle nav), D-Pad (navigate), Select (hints)
5. **Screen-Specific**: Each screen handles its own input after global shortcuts

### Focus Management
- **Two-Area Model**: Nav bar vs. console cards (not per-item)
- **D-Pad Navigation**: Left/Right moves between areas, Up/Down cycles within area
- **Selection Tracking**: `selected_nav_icon`, `selected_console_index` maintained per screen
- **Memory**: `last_console_selection` remembers card selection when switching away

### State Persistence
- **Config Auto-Save**: Each settings toggle calls `config_serialize()`
- **Cache Strategy**: 10-second TTL for console card cache to prevent flickering
- **Toast One-Shot**: `toast_shown_this_session` tracks per-app-launch notifications
- **Pin State**: Maintains across multiple frames during entry

### Text Rendering
- **Caching**: 16-entry LRU cache for text width (static strings only)
- **Fonts**: Two fonts loaded (regular + monospace)
- **Font Sizes**: `FONT_SIZE_HEADER` (24), `FONT_SIZE_SUBHEADER` (20), `BODY` (18), `SMALL` (14)
- **Colors**: 6 color constants for text (primary, secondary, tertiary)

### Performance Optimizations
- Particle update at 30fps (50% CPU reduction)
- Wave animation only updated when sidebar expanded
- Text width cached to 16 entries
- Double-buffered render (vita2d handles backbuffer)
- Cooldown gate prevents rapid reconnection attempts

### Error Handling
- Error popup modal for connection failures
- Cooldown periods to prevent network storms
- Packet loss detection with retry counter
- Thread creation error handling with fallback

---

## 7. COUPLING ANALYSIS

### Tightly Coupled
- **Navigation + Screen Rendering**: Global `selected_nav_icon` and `current_focus` control what renders
- **Animation State + Rendering**: Multiple animation structures need per-frame updates
- **Input Blocking + Touch/Button**: `button_block_mask` and `touch_block_active` separate but coordinated
- **Connection Overlay + UI State**: `connection_overlay` and `context.stream` tightly linked
- **Config Serialization**: Every settings change triggers `config_serialize()`

### Loosely Coupled
- **Icon Drawing vs. Nav Rendering**: Procedural icons fallback if textures missing
- **Particle System**: Completely independent animation layer
- **Text Width Cache**: Optional optimization, doesn't affect correctness
- **Debug Menu**: Fully conditionally compiled, zero impact if disabled

### No Coupling
- **Wave Animation**: Isolated to nav sidebar, doesn't affect other screens
- **PIN Entry**: Isolated state machine for registration only
- **Card Cache**: Independent data structure for UI optimization

---

## 8. SUGGESTED MODULE BOUNDARIES

### Proposed Refactoring into 8 modules

| Module | Est. Lines | Description |
|--------|-----------|-------------|
| `ui_navigation.c` | 1200 | Wave sidebar, collapse state machine, pill, toast |
| `ui_console_cards.c` | 400 | Card data structure, rendering, cache |
| `ui_screens.c` | 2000 | All 9 screen rendering functions |
| `ui_components.c` | 800 | Reusable widgets (toggle, dropdown, tab bar) |
| `ui_graphics.c` | 600 | Drawing primitives (rounded rect, circle, spinner) |
| `ui_animation.c` | 400 | Animation state, easing, particle system |
| `ui_input.c` | 300 | Input handling, blocking, touch processing |
| `ui_state.c` | 200 | Global state, text cache, connection state |

### Benefits of This Split
1. **Navigation**: Isolated complex state machine
2. **Cards**: Domain-specific console representation
3. **Screens**: Clear screen→rendering mapping
4. **Components**: Reusable widget library
5. **Graphics**: Low-level drawing layer
6. **Animation**: Temporal logic separated from rendering
7. **Input**: Abstraction over VitaSDK
8. **State**: Global state accessors

### Refactoring Cost Estimate
- **Low**: Input, Components, Graphics, State modules (simple extraction)
- **Medium**: Navigation (state machine logic intact), Cards (data structure only)
- **High**: Screens (largest module, many interdependencies), Animation (calls from many screens)

---

## 9. FILE USAGE PATTERNS

### Initialization Path
```
main()
  → draw_ui()
    → init_ui()
      → vita2d_init()
      → load_textures()
      → init_particles()
      → font loading
      → touch setup
      → SCE_CTRL_* binding
```

### Rendering Path (Per-Frame Loop in draw_ui)
```
Frame Loop:
  → Update input (button_state, touch state)
  → handle_error_popup_input()
  → handle_debug_menu_input()
  → Check debug menu combo
  → vita2d_start_drawing()
  → vita2d_clear_screen()
  → Draw background gradient
  → Update particles, render_particles()
  → Switch on current screen:
    → draw_main_menu() / draw_settings() / etc.
    → Update card_focus_animation()
    → render_console_grid()
  → render_content_focus_overlay()
  → render_wave_navigation()
    → update_nav_collapse_animation()
    → update_nav_toast()
    → update_wave_animation() (if expanded)
    → render_nav_pill() / wave background / icons
  → render_hints_indicator()
  → render_hints_popup()
  → render_loss_indicator_preview()
  → render_debug_menu()
  → render_error_popup()
  → vita2d_end_drawing()
  → vita2d_common_dialog_update()
  → vita2d_swap_buffers()
```

### Connection Flow
```
User presses X on console card:
  → btn_pressed(SCE_CTRL_CROSS) detected in draw_main_menu()
  → Set context.active_host
  → Check registration status:
    → Unregistered → UI_SCREEN_TYPE_REGISTER_HOST
    → At rest → ui_connection_begin(WAKING) → start_connection_thread()
    → Ready → ui_connection_begin(CONNECTING) → start_connection_thread()
  → Next frame: draw_waking_screen() renders spinner
  → Connection thread calls host_stream(context.active_host)
  → On stream ready: is_streaming flag set
  → draw_ui() stops rendering (skips vita2d calls when is_streaming)
```

---

## 10. HIDDEN PATTERNS & CONVENTIONS

### Rendering Order (Z-Depth)
1. Background gradient
2. Particle effects
3. Screen content (cards, controls)
4. Content focus overlay (semi-transparent)
5. Navigation sidebar
6. Hints and status indicators
7. Debug menu/error popup (highest)

### Color Scheme Constants
- **Primary Brand**: #3490FF (PlayStation Blue) → `UI_COLOR_PRIMARY_BLUE`
- **Dark BG**: #1A1614 → `UI_COLOR_BACKGROUND`
- **Card BG**: #37322D → `UI_COLOR_CARD_BG`
- **Text Primary**: #FAFAFA → `UI_COLOR_TEXT_PRIMARY`
- **Status**: Green (#4CAF50), Orange (#FF9800), Red (#F44336)

### Coordinate System
- Origin: top-left (0, 0)
- X: 0-960 (left-to-right)
- Y: 0-544 (top-to-bottom)
- Touch normalized from 1920x1088 to 960x544

### Layout Constants
- **Wave Nav Width**: 130px (left sidebar)
- **Content Area**: 830px (960-130)
- **Console Card**: 200x205px
- **Card Spacing**: 100px between cards

---

## 11. KNOWN LIMITATIONS

1. **Touch Input**: Normalized from 1920x1088 to 960x544 (potential precision loss)
2. **Text Width Cache**: Only 16 entries, may miss some strings
3. **Particle Physics**: Simplified gravity model
4. **Wave Generation**: Expensive sine calculations every frame when expanded
5. **No Scrolling**: Lists don't scroll if content exceeds screen
6. **Single Thread Rendering**: vita2d calls must happen on main thread
7. **Connection Thread**: Basic implementation, limited error recovery
8. **No Haptic Feedback**: Touch/button feedback not implemented

---

## 12. NEXT REFACTORING PRIORITIES

1. **Extract Navigation Module** (1200 lines, clear boundaries)
2. **Extract Components Module** (800 lines, standalone)
3. **Extract Graphics Primitives** (600 lines, reusable)
4. **Split Input Handling** (300 lines, abstractable)
5. **Consolidate Screen Rendering** (organize 2000 lines by screen type)
6. **Formalize Animation Framework** (unify timing, easing, lerping)
7. **Create State Manager** (centralize context access)
8. **Add Unit Tests** (especially animation timing and collision detection)

---

**Document generated for VitaRPS5 ui.c refactoring project**
**See also:** `docs/ai/UI_CODE_BEST_PRACTICES.md` for implementation guidelines
