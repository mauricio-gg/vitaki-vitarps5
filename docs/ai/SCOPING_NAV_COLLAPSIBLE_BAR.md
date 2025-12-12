# VitaRPS5 Collapsible Navigation Bar Scope

**Author:** Codex (Implementation Agent)
**Date:** 2025-12-11

## References
- `docs/ai/UI_FINAL_SPECIFICATION.md:24-90` (color, layout, wave nav behavior)
- `docs/ai/SCOPING_UI_POLISH.md` (sidebar wave rebuild requirements)
- `docs/ai/SCOPING_CONTROLLER_LAYOUT_REDO.md` (immersive screen needs)

## 1. Objectives
1. Preserve the modern wave-based sidebar on entry, but allow it to collapse into a minimal top-left “Menu” pill when the user interacts with screen content.
2. Provide reversible animations that feel premium yet stay within Vita’s performance budget.
3. Ensure controller + touch users can toggle the sidebar intentionally without losing muscle memory.
4. Support immersive screens (controller layout, future media views) that default to the collapsed state while keeping navigation accessible.

## 2. Behavioral Rules
- **Default State:** On screen entry, the sidebar is fully visible (130 px width, dual wave layers, four icons).
- **Collapse Trigger:** When focus leaves the sidebar for >300 ms *or* the user taps/clicks within the content area, set `nav_collapse_pending = true`. After the grace period, run the collapse animation.
- **Manual Collapse/Expand:** Triangle (controller) or tapping an explicit “Hide Menu” button also collapses the sidebar immediately. Tapping the pill, hitting Triangle, or pressing D-pad left while the pill is focused restores it.
- **Immersive Screens:** A screen may call `ui_nav_set_mode(UI_NAV_MODE_OVERLAY)` during `enter()` to collapse the sidebar instantly (used for controller layout). The pill appears immediately so users know nav is available.
- **Instruction Toast:** The first time the sidebar auto-collapses in a session, display a toast near the pill (“Menu hidden—tap pill or press Triangle to reopen”) for 2 seconds.
- **Persistence:** Sidebar state resets to expanded when leaving the current screen so new screens always introduce the menu, unless that screen explicitly opts into overlay mode.

## 3. Animation Specification
Total collapse duration ≤280 ms to keep UI responsive.

### 3.1 Collapse Sequence
1. **Preparation (0–80 ms):**
   - Fade out icon labels (if any) from alpha 1→0.
   - Reduce highlight opacity to 0 while waves continue moving.
   - Start width tween from 130 px → 70 px (ease-in).

2. **Collapse (80–200 ms):**
   - Continue width tween down to 0 px; wave textures slide left and scale to 0.8.
   - Icon stack translates toward top-left corner (target coordinates x=16, y=16) while non-active icons fade to 0.
   - Active icon morphs into a circular chip by increasing corner radius (render a rounded rect with radius matching current width/2).

3. **Pill Reveal (200–280 ms):**
   - Chip expands horizontally to 120 px (ease-out) while text label “Menu” and a hamburger glyph fade in.
   - Waves pause animation to save GPU cycles; store their phases so they resume smoothly when expanded.

### 3.2 Expand Sequence
- Reverse the steps above: pill contracts to chip (80 ms), chip slides down/right while bar width increases to 130 px (120 ms), icons fade in sequentially (40 ms stagger), wave animation resumes using the stored phases.

## 4. Implementation Tasks
1. **Navigation State Manager**
   - Extend `vita/src/ui/navigation.c` with state machine: `NAV_STATE_EXPANDED`, `NAV_STATE_COLLAPSING`, `NAV_STATE_COLLAPSED`, `NAV_STATE_EXPANDING`.
   - Add timers for focus grace period and animation phases.
   - Expose API `ui_nav_request_collapse(reason)` / `ui_nav_request_expand(source)` so screens can explicitly trigger transitions.

2. **Input Hooks**
   - Integrate with global input dispatcher so D-pad focus changes set `nav_collapse_pending`. Touch events hitting the content area call the same API.
   - Bind Triangle (or Start if Triangle is reserved) to toggle the sidebar regardless of focus.

3. **Pill Component**
   - Drawn as rounded rect (height 36 px, width 44→120 px). Contains hamburger icon + “Menu” text (16 px). Hitbox 52×120 px for touch.
   - Maintains focus state for controller navigation; visible only when `NAV_STATE_COLLAPSED`.

4. **Overlay Menu**
   - When the pill is activated, instantiate an overlay panel that slides the full sidebar in (same animation as expand). Optional: darken rest of screen at 55% alpha until user closes overlay.

5. **Grace Period & Toast**
   - Implement single-use toast manager triggered on the first auto-collapse. Use existing toast component or create one (text, fade in/out).

6. **Performance Safeguards**
   - Pause wave animation (`wave_layer->is_paused = true`) during collapsed state to save 2 draw calls per frame.
   - Ensure animation uses precomputed sine values or incremental updates to avoid perf spikes.

7. **Settings/Config**
   - Add optional toggle in Settings → Controller (or General) allowing users to disable auto-collapse (“Keep navigation pinned”). Default ON.

## 5. Testing Checklist
- [ ] Sidebar collapses after moving focus into content and waiting >300 ms.
- [ ] Touch tap outside sidebar collapses immediately.
- [ ] Pill always appears at top-left, responds to touch and controller input, and restores sidebar reliably.
- [ ] Triangle toggles menu state from anywhere.
- [ ] Immersive screens start collapsed but can expand via pill/Triangle.
- [ ] Toast appears only once per session.
- [ ] All animations complete within 280 ms and do not drop framerate below 58 FPS on hardware.
- [ ] “Keep navigation pinned” setting prevents auto-collapse (sidebar stays expanded unless user explicitly collapses).

## 6. Open Questions
1. Should the pill show the current screen icon (e.g., play icon) alongside “Menu” for easier context?
2. Do we need vibration or audio feedback when the sidebar collapses/expands?
3. Should we remember the manual collapse state per screen when returning via back navigation?

---
This scope can be handed directly to a dev. Implementation should be coordinated with the existing `SCOPING_UI_POLISH.md` tasks since both touch the sidebar rendering and icon assets.
