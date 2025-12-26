# VitaRPS5 Main Screen Polish Scope

## References
- `docs/ai/UI_FINAL_SPECIFICATION.md:24-90,104-150,197-212`
- Screenshot `docs/ai/reference/2025-12-11-082031.png`

## Objectives
1. Make the sidebar navigation fully match the wave navigation spec (width, assets, animations, iconography) while staying under Vita draw-call budget.
2. Rebuild the console card + typography stack so it respects the rounded-card system, PS5 logo proportions, and color hierarchy.
3. Replace the static gradient + yellow squares with the PlayStation symbol particle system described in the spec (12 entities, layered speeds, respawn behavior) without dropping below 60 FPS.
4. Introduce low-cost micro-animations (card focus pulse, status indicator breath, panel transition) that reinforce the modern design language and work with both controller + touch input.

---

## Task 1 – Sidebar Icon + Wave Navigation Rebuild

### Deliverables
- Single sprite sheet or set of 32×32 px mono-color icons: play (equilateral triangle), settings (gear), controller, profile. Export from source vector so edges are crisp at Vita resolution.
- `wave_top.png` and `wave_bottom.png` assets from `assets/user_provided/modern_assets/navigation` imported at native 130×544 px (ensure 2048-aligned atlas grouping).
- Updated rendering code in `vita/src/ui/navigation.c` (or equivalent) that:
  - Reserves 130 px width (per spec line 45) and paints the two waves with blending.
  - Offsets each wave’s UV or X position by `sin(phase)` to achieve ±3 px horizontal movement.
  - Applies the same phase-driven vertical offset (±3 px) to each icon so they “ride” the wave (line 78).

### Animation Specification
```c
typedef struct {
    float amplitude;   // pixels, use 3.0f
    float speed;       // radians per second, use 0.7f for bottom, 1.1f for top
    float phase;       // accumulates per frame
    vita2d_texture *tex;
} wave_layer_t;
```
- Each frame: `layer->phase += delta * layer->speed; float offset = sinf(layer->phase) * layer->amplitude;`
- Draw bottom wave first at `x = base_x + offset`, `y = 0`, `alpha = 160/255`; top wave at `alpha = 220/255`.
- Icon positions: compute baseline centers at `(65, y)` spaced 80 px apart; when rendering icon `i`, add `wave_offset = sinf(layer_top.phase + i * 0.35f) * 3.0f` to `y` to create the bob.
- Selection highlight: draw semi-transparent rounded rect (white at 20% alpha) behind the icon when focused (line 76). Use same wave offset to avoid jitter.

### Acceptance Criteria
- Icons render at identical sharpness; QA test: capture screenshot and zoom to ensure consistent anti-aliasing.
- Sidebar wave animation loops smoothly (phase wraps automatically). No more than 4 draw calls (bottom wave, top wave, highlight, icons).
- Navigation remains responsive (<16 ms update) while animations run.

---

## Task 2 – Console Card + Typography Alignment

### Deliverables
- Update card drawing to use 12 px corner radius, card background color `(45,50,55,255)`, and subtle drop shadow (black at 25% opacity, offset 4 px, blur simulated with enlarged rounded rect) as specified on lines 24-48.
- Replace stretched PS logo with the `PS5_logo.png` asset at its native aspect ratio; max width 180 px, center-aligned inside card bounding box. Maintain 24 px top padding.
- Title text uses 32 px bold (line 38) and secondary instructions at 16 px regular (line 40). Ensure baseline grid across views.
- Standby dot uses ellipse assets from `assets/user_provided/modern_assets/status` with 10 px diameter.

### Micro-Interaction
- Card focus animation: when a card becomes selected, tween from scale 0.95 to 1.0 over 180 ms (ease-out). Use `chiaki_time_now()` delta to animate per frame; clamp scale to avoid bouncing.
- Outline glow: draw 2 px stroke (#3490FF) using vita2d’s stroke routine or by rendering a slightly larger rounded rect behind the card.

### Acceptance Criteria
- Layout inspected on device shows card width 300 px, height 250 px ±2 px. Text aligns with baseline grid.
- Focus animation completes within 200 ms and never exceeds scale 1.0. Non-focused cards stay at 0.95 scale.
- Color usage matches palette (primary white text, gray subtext, yellow standby text).

---

## Task 3 – Particle Background Refresh

### Deliverables
- Implement particle manager that spawns 12 PlayStation symbols (triangle, circle, cross, square) per spec lines 52-58. Use textures under `assets/user_provided/modern_assets/particles`.
- Each particle struct stores position, speed (0.2–1.5 px/frame), scale (0.5–1.2), rotation, color tint (RGBA pastel), and layer index (foreground/background).
- Movement: particles drift diagonally downward. When `y > 560`, respawn at `y = -40` with randomized x and properties.
- Parallax: background layer speed multiplier 0.7, foreground 1.0. Optionally apply sin-based horizontal sway ±2 px for variation.

### Acceptance Criteria
- Particles never exceed 12 entities simultaneously.
- Render order: gradient background -> particles -> main UI to avoid overdraw artifacts.
- FPS measured via debug overlay stays ≥58 during idle screen.

---

## Task 4 – Additional Micro-Animations & Polish

### Deliverables
1. **Status Indicator “Breath”**: periodic opacity tween (0.7↔1.0 alpha, 1.5 s loop) on the standby dot. Implement via `fmodf(time, 1.5f)`.
2. **Panel Transition**: when switching sidebar icons, slide old panel 10 px opposite direction over 120 ms while fading to 0%; bring new panel from 10 px offset to 0 and fade to 100%. Ensure both controller and touch navigation trigger the same animation.
3. **Instruction Bar**: add 1 px separator line between sidebar and content (vertical gradient). Update bottom instruction row with small DualShock glyphs per branding.

### Acceptance Criteria
- Animations are frame-independent (delta-based) and pause when UI is offscreen.
- No animation blocks input; selecting a new panel while an older transition is in flight cancels the old tween cleanly.
- QA sign-off includes video capture demonstrating sidebar waves + card focus + particle background simultaneously at target FPS.

---

## Engineering Notes
- Budget: keep total draw calls within 80 (lines 213-219). Aim: sidebar waves (2), icons (4), highlight (1), card (3), particles (12), text (~10) = <40 additional calls.
- Store animation state in existing UI structs to avoid heap churn. Reuse vita2d textures loaded during boot.
- Document new behaviors in `docs/ai/UI_DEVELOPMENT_GUIDE.md` after implementation, and update `TODO.md` items for review workflow.
