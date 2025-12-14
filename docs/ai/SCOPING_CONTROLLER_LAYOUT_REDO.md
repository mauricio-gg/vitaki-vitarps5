# VitaRPS5 Controller Layout Redesign Scope

**Author:** Codex (Implementation Agent)
**Date:** 2025-12-11
**References:**
- `docs/ai/UI_FINAL_SPECIFICATION.md:24-190` (general style, typography, animation rules)
- `docs/ai/PHASE2_SCREEN_REDESIGN_SPEC.md:144-218` (original controller layout)
- Screenshot inspirations provided by maintainer (front/back Vita diagrams)

## 1. Problem Statement
The current controller screen uses the wave sidebar and dual-column layout defined for Phase 2, which constrains the working canvas to ~830×544. Showing both the Vita hardware diagram and adjustable mappings at that scale forces tiny text, inconsistent spacing, and no visual parity with the rest of the “modern” UI. We also lack intuitive ways to preview multiple presets or view the rear touch zones simultaneously.

## 2. Goals
1. **Immersive Controller Canvas:** Dedicate the full 960×544 surface to the Vita diagram plus mapping metadata by hiding/replacing the sidebar when this view is active.
2. **Preset Cycling UX:** Provide a single tap/D-pad action to cycle through presets (e.g., Default, FPS, Racing) with real-time highlight updates on the diagram.
3. **Front/Back Visualization:** Allow users to either flip between front/back diagrams or view both at once, depending on screen space and GPU budget.
4. **Minimal Menu Access:** Replace the left wave navigation with a compact top bar that keeps brand consistency while offering a “Menu” affordance so users can jump back to other screens without losing context.
5. **Consistent Input Support:** Touch, controller buttons, and the Vita motion sensors (if future toggles) must work identically to other UI surfaces.

## 3. Layout Blueprint
```
┌──────────────────────────────────────────────────────────────────┐
│ [Menu ◀ ▶] Controller Layout      Preset: FPS        Front | Back │  ← 52 px top bar
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│        ┌─────────────────────────────┐   ┌────────────────────┐   │
│        │  Vita Front Illustration    │   │  Quick Legend      │   │
│        │  (720×360 bounding box)     │   │  • X → Jump        │   │
│        │                             │   │  • ◻ → Reload      │   │
│        │ (touch indicators + labels) │   │  • ...             │   │
│        └─────────────────────────────┘   └────────────────────┘   │
│                                                                  │
│        ┌─────────────────────────────┐                           │
│        │  Vita Rear Illustration     │  (optional; collapsible) │
│        └─────────────────────────────┘                           │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

### 3.1 Top Bar
- Height: 52 px; spans full width. Background uses the charcoal gradient (lines 24-30) with 80% opacity overlay to distinguish from canvas.
- Left section: `Menu` button (icon + label) that, when tapped or pressing Triangle, opens a contextual sheet with the standard navigation options (Dashboard, Settings, Controller, Profile). On open, we temporarily show the wave menu as an overlay sliding in from the left (keep animation spec from `UI_FINAL_SPECIFICATION.md:50-80`).
- Center: Screen title “Controller Layout” (32 px bold) plus sublabel “Preset: <name>”. Use D-pad left/right or L/R triggers to cycle presets. Touch the preset label to open dropdown (reusing dropdown component from settings, `UI_FINAL_SPECIFICATION.md:147-149`).
- Right: Toggle group `Front | Back | Both`. Each acts as segmented control. Selecting “Both” renders the rear diagram scaled to 80% width below the front view.

### 3.2 Vita Diagram Canvas
- Import vector-like assets for front and rear outlines (white stroke, 2 px). Provide separate overlay textures for analog sticks, face buttons, shoulders, touch areas.
- Base bounding box: 720×360 px centered horizontally (offset 120 px from top bar). This leaves 120 px margins left/right for legends.
- Button callouts: Each button label sits on 1 px hairlines pointing outwards. Use color-coded chips (PlayStation blue for primary, gray for secondary). Text size 16 px.
- When a preset is highlighted, relevant callouts glow (alpha pulse 0.75↔1.0, 1 second loop) and the mapping text updates.

### 3.3 Legend/Details Panel
- Right column (max width 200 px) lists the currently selected preset actions. Scrollable if >8 entries. The list reuses the card style from Task 2 in `SCOPING_UI_POLISH.md` to stay consistent.
- Provide callout badges for unique features (e.g., “Rear Touch Swipe → L2/R2”).

## 4. Interaction Model
| Action | Controller Input | Touch Input | Behavior |
|--------|------------------|-------------|----------|
|Open menu| Triangle | Tap `Menu` button | Slide-in overlay with wave navigation icons; closes with Circle or tap outside.
|Cycle preset| L/R or D-pad left/right | Tap arrows on preset label | Update mapping data, refresh highlight states.
|Toggle view| D-pad up/down (when focus on toggle) | Tap `Front`, `Back`, or `Both` | Animate diagram flip or show both.
|Select action| D-pad to focus legend; Cross to edit mapping | Tap legend row | Opens mapping modal (existing remap flow) with Vita diagram context.

## 5. Animation Specs
### 5.1 Diagram Flip
- When switching between Front and Back modes, rotate the diagram around the X-axis 180° over 220 ms (ease-in-out). Implementation: use two textures and crossfade with scale/skew to simulate flip (actual 3D not needed). Sequence:
  1. Animate scale Y from 1.0→0.9 while fading out front diagram.
  2. Swap texture at midpoint (~110 ms) and fade in the other while returning scale to 1.0.
- For “Both”, no animation; both diagrams slide into position (front stays center, rear scales to 0.8 and translates downward 40 px).

### 5.2 Preset Cycling
- On preset change, tween callout stroke color from #FFFFFF to accent color (#3490FF) for the relevant buttons, then back over 300 ms.
- Display toast text near bottom (“Preset: FPS”) fading in/out (opacity 0→1 in 120 ms, hold 500 ms, fade out 120 ms).

### 5.3 Menu Overlay
- `Menu` overlay reuses wave assets but animates as a slide-in from the left edge covering 250 px. Use cubic ease (0.4, 0.0, 0.2, 1). Overlay darkens the rest of the screen with alpha 0.55 to keep focus.

## 6. Implementation Tasks
1. **Immersive Layout Framework**
   - Modify `vita/src/ui/navigation.c` (and the navigation controller) to support “immersive” screens that hide the sidebar and display custom top bars.
   - Add API: `ui_nav_set_mode(UI_NAV_MODE_DEFAULT|UI_NAV_MODE_OVERLAY)`.
   - Controller screen registers itself as overlay mode on entry and restores default on exit.

2. **Top Bar Component**
   - Build `ui_top_bar.c` that accepts left button definition, center title/preset info, right toggle group.
   - Handle focus loops: left button → preset control → toggle group → legend list.

3. **Preset Model + Cycling**
   - Define `controller_preset_t { const char* name; ControllerMapping map; const char* description; }` with up to 6 presets (Default, Shooter, Racing, Fighting, Remote Play Classic, Custom).
   - Persist user preset selection in config (`config.controller_preset_id`). When editing custom mapping, auto-switch to Custom and mark dirty state.

4. **Vita Diagram Renderer**
   - Source new assets from designer or create programmatic outlines using vita2d primitives. Asset requirements: `vita_front_outline.png`, `vita_back_outline.png`, `callout_arrow.png`, `touch_zone_highlight.png`.
   - Build renderer that accepts `ControllerMapping` and draws highlight per button.
   - Provide hooking for live highlight when user presses actual Vita buttons (optional debug overlay).

5. **Legend + Mapping Modal Integration**
   - Use existing mapping modal but restyle to match new card aesthetic. When a legend item gets focus, pulse the matching callouts on the diagram.

6. **Menu Overlay**
   - Implement overlay view that instantiates the standard wave nav icons vertically. Starting state: collapsed. On open, disable interactions underneath via modal stack.

7. **Accessibility & Performance Checks**
   - Ensure text is legible in high-contrast mode and that the top bar supports localized strings (max 18 characters for preset names before truncation).
   - Profiling target: <55 draw calls for base controller screen (diagram ~10, labels ~15, legend ~10, top bar ~6, overlay waves optional). Maintain ≥58 FPS measured with debug HUD.

## 7. Testing Checklist
- [ ] Controller input navigation loops through Menu → Preset → Toggle → Legend without traps.
- [ ] Touch interactions replicate controller behavior with hitboxes >=44 px.
- [ ] Preset cycling updates both diagram highlights and legend text instantly (<1 frame delay).
- [ ] Front/back flip animation stays under 250 ms and does not drop frames on hardware.
- [ ] Menu overlay always restores previous focus on close.
- [ ] Config persistence: switching presets, reopening app returns to last preset.
- [ ] Localization: verify longest preset names (12+ chars) truncate gracefully.

## 8. Open Questions / Follow-ups
1. Do we need per-preset descriptive text (e.g., “Optimized for shooters”)? If yes, reserve 60 px area below preset label.
2. Should the controller mapping modal allow editing rear touch gestures separately for left/right halves? Requires backend confirmation.
3. Asset source: confirm whether designer can deliver front/back outlines with transparent backgrounds or if engineering should trace simplified shapes.

---
This scope is ready for assignment. Next steps: assign engineering owner for Tasks 1-6, coordinate with design for assets, and create subtasks in `TODO.md` referencing this document.
