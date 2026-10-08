# XMB mock: feasibility notes (issue #271, round 3)

A short realism check for the chosen direction. Not an implementation plan. Nothing was built or measured on a Vita; counts and sizes are estimates. Budget: about 80 draw calls for Home, 15 MB of textures.

## What the mock uses, and what it becomes on the Vita

| Mock element | vita2d |
|---|---|
| Flowing wave background (5 ribbons, time-of-day tint, a few dust points) | One `vita2d_draw_array` strip per ribbon fill, plus one line strip per ribbon highlight, vertex colours fade the edges. Updated on the CPU at 30 Hz. Background gradient: 1 `draw_array`. About 12 calls. |
| Category icons, list icons | The app's own 48 px nav icons (`icon_play`, `icon_settings`, `icon_controller`, `icon_profile`), drawn as quads. Selection is brightness (`vita2d_draw_texture_tint`), scale and a baked soft-glow texture behind it. No boxes. |
| Console status ring and badge | One baked ring texture per state (ready, standby, PSN, dashed for unregistered) drawn tinted, the console card image inside it, and a small baked badge. The app's existing console card images are inverted for the dark UI; in the app this is a tint or a pre-inverted copy. |
| Status dots | The existing `ellipse_green/yellow/red.png`, tinted for PSN and unregistered. |
| Thin text with a soft shadow | The existing FreeType atlas. Roboto Light, Regular and Medium at 16, 20 and 28 px, plus Roboto Mono at 16 and 28 for numbers. The shadow is the same text drawn once more, offset and dark (doubles the text calls; skip it for small text if the budget needs it). |
| Hints row | The existing button-symbol PNGs plus text. |
| Settings and Profile pages | Flat `background.png`, text, 1 px lines as `vita2d_draw_rectangle`, selection as a translucent rectangle. Toggle is two rectangles. No 9-slice at all. |
| Controller screen | The existing `controller_front.png` and `controller_back.png` (already how today's screen works), with the 3x6 touch grid as rectangles over the diagram and short labels as text. Callouts are text and a line. |
| Popups and the options column | A dark translucent rectangle with a 1 px border, text rows, a gradient strip for selection. The blur behind a popup is a pre-blurred copy of the last frame, or just a darker scrim. |
| Cover art | Optional fetch by title ID, cached to `ux0:data/vitarps5/covers/`, one texture at about 96x128 for the detail view. The fallback is a baked tile with a monogram. |

## Draw-call estimates (paper)
- Home about 70: ribbons and gradient 12, category row 6, list rows 4 x 6 = 24 (ring, art, badge, dot, two texts), detail view about 18, hints and top bar about 10.
- Settings and Profile pages about 30 (groups, rows, lines, hints). Controller summary about 25. Mapping view about 45 (18 zones plus labels). Connecting about 28. Stream overlay about 8. Popups about 12.
- This is under the 80-call Home budget. The thin-text shadow is the one thing that can push it over; drawing it only for titles keeps it in.

## Textures (estimate)
Nav icons and symbols about 0.1 MB, console cards 0.6 MB (8 PNGs), rings, badges and glow about 0.2 MB, controller diagrams about 2.5 MB at source size (874x396 and 720x327 RGBA; scale down for a saving), background 960x960 palette image about 0.4 MB, 4 covers at 96x128 about 0.2 MB, Roboto atlas about 1.5 MB. **About 5.5 MB total.** The wave itself costs no texture.

## Reuse
- `ui_controller_diagram.c` and the controller screen logic: unchanged behaviour (three views, preset slots, mapping popup). Only the drawing colours and chrome change.
- `ui_focus.c` zones: category row, list, options column, Settings groups and rows, controller views, popup. I did not verify that nothing in the focus manager assumes the wave sidebar.
- `ui_input.c`, the IME dialog and `ui_text.c` atlas: reused. The wave sidebar in `ui_navigation.c` is deleted.

## Build risk: low
- The wave needs a CPU vertex update each frame; freeze or halve it while a stream is starting.
- The glow is a baked texture drawn behind the selected icon; confirm it looks right on the Vita's OLED (the mock uses CSS drop-shadow).
- Touch: the mock keeps left categories as a small tappable trail. True XMB lets them slide off; that is our addition for touch.

## Assumptions
1. The three presets are today's Custom 1/2/3 slots. "Save" copies the active mapping into another slot; "Apply" makes a slot active.
2. The stream overlay content and Esc/Circle to leave are assumed, as before.
3. Logs has no existing icon, so the mock draws a simple list glyph.
4. Console images are the app's existing card art. There is no PS4 "ready" image, so the mock tints the standby one.
