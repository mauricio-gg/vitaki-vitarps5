# UI Polish - Known Issues & Improvements

**Created:** November 4, 2025
**Related:** UI/UX Assessment & Polish Implementation

This document tracks known UI issues discovered during polish implementation that require attention.

---

## CRITICAL - Play Icon Needs Replacement

**Location:** `vita/src/ui.c:794-812` (draw_play_icon function)

**Current Implementation:**
```c
/// Draw a simple white filled triangle play icon (pointing right)
void draw_play_icon(int center_x, int center_y, int size) {
  uint32_t white = RGBA8(255, 255, 255, 255);
  int half_size = size / 2;
  int offset = size / 6;

  // Draw filled triangle using horizontal lines
  for (int y = -half_size; y <= half_size; y++) {
    int x_start = center_x - half_size + abs(y) - offset;
    int x_end = center_x + half_size - offset;
    int width = x_end - x_start;
    if (width > 0) {
      vita2d_draw_rectangle(x_start, center_y + y, width, 1, white);
    }
  }
}
```

**Problem:**
- Uses custom primitive drawing (pixel-by-pixel horizontal lines)
- Looks "weird" and inconsistent on main screen navigation bar
- Other nav icons use proper PNG texture assets (settings, controller, profile)
- Only Play icon lacks a professional texture

**Visual Impact:** Medium-High (visible inconsistency in main navigation)

**Performance Impact:** Low (but inefficient compared to texture blit)

**Solution Options:**

### Option 1: Create PNG Texture Asset (Recommended)
**Effort:** 1-2 hours

Steps:
1. Create `icon_play.png` in `vita/assets/` directory (48x48px minimum)
2. Design should match other navigation icons:
   - White icon on transparent background
   - Simple, clean PlayStation-style design
   - Could be: Play triangle, Console icon, or Home icon
3. Load in `load_textures()`:
   ```c
   icon_play = vita2d_load_PNG_file(TEXTURE_PATH "icon_play.png");
   ```
4. Remove special case in `render_wave_navigation()` (line 537):
   ```c
   // Current (line 537):
   if (i == 0) {
     draw_play_icon(WAVE_NAV_ICON_X, y, current_icon_size);
   }

   // Change to use icon_play texture like others
   ```

**Pros:**
- Consistent with other nav icons
- Better visual quality
- Faster rendering (single texture blit vs many rectangles)
- Professional appearance

**Cons:**
- Requires creating/sourcing icon asset
- Minor code refactoring

### Option 2: Improve Primitive Drawing
**Effort:** 1 hour

Improve the triangle rendering:
- Add anti-aliasing approximation
- Better centering calculation
- Potentially use PlayStation symbols (triangle texture)

**Pros:**
- No new assets needed
- Quick fix

**Cons:**
- Still inconsistent with texture-based icons
- Harder to maintain visual consistency

---

## Recommendation

**Priority:** Medium-High
**Suggested Timeline:** Include in Phase 3 Advanced Polish
**Assignee:** TBD

**Action Items:**
1. [ ] Design/source icon_play.png asset (48x48px, white on transparent)
2. [ ] Add to `vita/assets/` directory
3. [ ] Update `load_textures()` to load icon_play
4. [ ] Remove `draw_play_icon()` function entirely
5. [ ] Update `render_wave_navigation()` to use texture for all 4 icons
6. [ ] Test on actual Vita hardware to verify appearance

**Alternative Icon Concepts:**
- Classic play triangle (▶) - simple and recognizable
- Game controller icon - represents "play games"
- PS5 console icon - represents console selection
- Home icon - represents main/home screen

---

## CRITICAL - Waking Screen Workflow Broken

**Location:** `vita/src/ui.c:2387-2454` (draw_waking_screen function)

**Current Implementation Problems:**

1. **Progress Bar & Timeout** - Shows 30-second countdown with progress bar
   ```c
   // Current: ui.c:2387-2454
   #define WAKING_TIMEOUT_MS 30000  // 30 seconds timeout
   // Draws progress bar and countdown timer
   // Returns to main screen on timeout
   ```

2. **Incorrect Workflow** - Returns to main screen instead of streaming
   ```c
   if (elapsed > WAKING_TIMEOUT_MS) {
     waking_start_time = 0;
     return UI_SCREEN_TYPE_MAIN;  // ❌ Wrong! Should wait for connection
   }
   ```

3. **Visual Design Inconsistency** - Doesn't match modern UI polish
   - Uses basic card without enhanced shadows
   - Simple text without styled layout
   - Doesn't match console cards, navigation, or other screens

**Expected Behavior:**

✅ **What it SHOULD do:**
1. Show "Waking Console..." message with spinner (no progress bar)
2. Wait indefinitely for console to wake up
3. **Automatically transition to Remote Play** when console is ready
4. Only action: Press Circle to cancel
5. Match modern UI design (enhanced shadows, PlayStation Blue accents, proper spacing)

**Problem Impact:** High (broken user flow, inconsistent design)

**Solution Required:**

```c
// Remove timeout and progress bar
// Keep only:
// - "Waking Console..." title
// - Console name/IP
// - Spinner animation (not dots)
// - "Press Circle to cancel" hint
// - Auto-transition to UI_SCREEN_TYPE_STREAM when ready
// - Match modern UI polish (shadows, colors, spacing)
```

**Priority:** CRITICAL (broken workflow)
**Effort:** 2-3 hours
**Blocks:** User experience for console wake-up flow

---

## HIGH - Reconnecting Screen Needs Polish

**Location:** `vita/src/ui.c` (UI_SCREEN_TYPE_RECONNECTING)

**Current State:**
- New screen type added in recent merge (commit 3b80bac)
- Likely uses basic/placeholder UI
- Not yet implemented with modern design polish

**Problem:**
- Reconnecting screen shows during packet loss recovery
- Critical screen for user experience (appears during issues)
- Needs to match modern UI design:
  - Enhanced card with shadow
  - PlayStation Blue accents
  - Consistent spacing and typography
  - Proper visual feedback

**Expected Design:**
```
┌─────────────────────────────────────────────────┐
│ [Enhanced Card with Shadow]                     │
│                                                 │
│   Reconnecting to Console                       │
│                                                 │
│   PS5 - 192.168.1.100                          │
│                                                 │
│   [Spinner Animation]                           │
│                                                 │
│   Recovering from packet loss...                │
│                                                 │
│   Press Circle to cancel                        │
│                                                 │
└─────────────────────────────────────────────────┘
```

**Priority:** HIGH (user-facing during issues)
**Effort:** 2-3 hours
**Related:** Should match Waking screen once polished

---

## Other Minor Issues

### 1. Particle Count Still Potentially High
**Status:** Addressed in Phase 1 (reduced 12→8), but could go lower (6) for older Vita models

### 2. Font Size Hierarchy Could Use More Granularity
**Status:** Fixed in Phase 1, but only 4 levels. Could add FONT_SIZE_XSMALL (12pt) for very minor text

### 3. Shadow Rendering Uses Multiple Rectangle Calls
**Status:** Acceptable performance, but could be optimized with pre-rendered shadow textures

---

## Completed Issues (Fixed in Phase 1 & 2)

✅ Typography hierarchy (FONT_SIZE_SMALL was same as BODY)
✅ Particle count too high (12→8)
✅ Console card spacing too wide (120→85px)
✅ Pure white text causing eye strain (→ off-white)
✅ Waking screen card lacked shadow
✅ Navigation selection feedback too subtle
✅ Console card selection lacked visual weight
✅ Single-tab settings bar looked unfinished
✅ Controller tabs used inconsistent purple theme
✅ Mapping table hard to scan (no zebra striping)
✅ PIN entry digits lacked visual prominence

---

**Last Updated:** November 18, 2025
**Next Review:** After critical workflow fixes (Waking/Reconnecting screens)
