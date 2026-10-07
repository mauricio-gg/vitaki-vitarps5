# UI mocks: feasibility notes (issue #271, round 2)

A short realism check per direction. Not an implementation plan. Nothing was built or measured on a Vita; draw-call counts and memory are estimates from the mocks. Budget: about 80 draw calls for Home, 15 MB textures in total.

Common to all four:
- Rounded shapes, chips, tiles and bubbles are baked PNGs drawn tinted (1 call each). Variable-size panels are 9-slice (9 calls).
- Backdrop blur ("glass") is a pre-blurred copy of the background, baked once per background change.
- Gradients, ribbons and shades are `vita2d_draw_array` strips with per-vertex colour (1 call each).
- Fonts: FreeType and the existing `ui_text.c` atlas. Each mock uses one family with 3 weights at 4 sizes. Only the chosen direction's font would ship.
- Covers: optional fetch by title ID, cached to `ux0:data/vitarps5/covers/`, decoded once at about 244x326. The mocks use 4 covers (about 1.3 MB). The no-art fallback is a baked tile with a monogram.
- Focus, input and popups reuse `ui_focus.c` (zones and modal stack), `ui_input.c` and the IME dialog. I did not verify that `ui_focus.c` handles 2-D movement; LiveArea's honeycomb needs a small neighbour table.
- The "View on device" frame is mock-only.

## 1. XMB "Remote Play Bar" (CEO's pick so far)
- **Elements to calls.** Background gradient: 1 `draw_array`. 6 ribbons: each a fill strip plus a highlight line, about 12 `draw_array` calls, updated on the CPU (30 Hz is enough). Dust: one baked star texture. Icons: duotone icon atlas, 1 quad each. Panels and options: tinted glass 9-slice. Selected-row size change is a swap between two pre-rendered font sizes, not a scale. Trail categories are small scaled icon quads.
- **Draw calls.** Home about 95 as drawn, about 78 with the detail panel's rows joined into two text strings. Settings about 85. Options panel open +25. Connecting about 60. Overlay about 14.
- **Textures.** Icon atlas 1.0 MB, covers 1.3 MB, console art 0.3 MB, star layer 0.5 MB, 9-slices 0.15 MB, fonts 2 MB. About 5 MB.
- **Reuse.** `ui_focus.c` zones map to category row, list, value panel and options panel. `ui_navigation.c` wave sidebar is deleted.
- **Build risk: low.** Watch the CPU cost of ribbon vertices while a stream starts (freeze them while connecting). True XMB lets categories slide off the left edge; this mock keeps them as a small tappable trail, which is our own addition.

## 2. LiveArea Modern
- **Elements to calls.** Wallpaper: one quad plus a vignette `draw_array`. Bubbles: a baked glass orb texture, tinted ring per state (ready, standby, PSN, dashed for unregistered), console art quad, badge, 2 text lines. Floating bob is a position offset. The gate page: blurred cover background (pre-blurred), a Start orb, three glass frames (9-slice), the cover quad. The circular page reveal cannot be a true clip (no stencil in vita2d); it becomes a scaled disc sprite that grows to fill the screen, then a crossfade. Settings list: pill rows as baked tinted quads.
- **Draw calls.** Home about 56. Gate about 65. Settings about 50. Connecting about 35 (8 ring segments as one `draw_array`). Overlay: 3 round stat bubbles, about 14.
- **Textures.** Wallpaper 2.1 MB (960x544 RGBA; a 480x272 upscaled copy would be 0.5 MB), orb and ring variants 0.6 MB, covers 1.3 MB, blurred gate backgrounds 0.4 MB, icons 1.0 MB, fonts 2 MB. About 8 MB.
- **Reuse.** `ui_console_cards.c` data model; zone focus for the two levels (home, gate); touch maps directly to bubbles.
- **Build risk: medium.** The reveal and peel are bespoke animation code; honeycomb navigation needs 2-D focus. Wallpaper memory is the largest single item.

## 3. Editorial
- **Elements to calls.** Flat paper clear colour; optional baked grain tile (1 call). Hairlines and the accent block are `vita2d_draw_rectangle`. Selected row is an inverted rectangle plus text. Console names are text at 104 px (a new glyph size); the rest at 16/20/36. Cover has an accent rectangle behind it and a 2 px rectangle outline. Hard offset shadows are one more rectangle. No 9-slice, no blur.
- **Draw calls.** Home about 48. Settings about 40. Connecting about 38. Overlay about 12 (paper panel plus text).
- **Textures.** Covers 1.3 MB, icons 0.3 MB, console art 0.3 MB, fonts about 2.5 MB (the 104 px set is a small subset: digits and the letters in console names). About 4 MB.
- **Reuse.** `ui_focus.c` list zones fit directly. Everything is rectangles and text.
- **Build risk: low.** Smallest and fastest build. Product risks, not technical ones: a light theme costs power on an OLED Vita and may feel bright in the dark; text rows are smaller touch targets (mitigate with tall hit boxes).

## 4. Cover-flow
- **Elements to calls.** Room: 2 gradient `draw_array`. Floor grid: one baked 512x256 texture. Spotlight: baked radial quad. Cards: **vita2d has no perspective-textured quad** (`draw_array` is colour only; textured draws support scale and rotate, not skew). Plan: when a cover loads, bake two pre-warped copies (left-turned and right-turned) on the CPU with the reflection and a fade included, then draw each card as 1 quad. Alternative is a custom GXM vertex path using vita2d's texture program, which is not part of the public API. Focused card is the flat texture, scaled. Settings drum rows are baked tinted pills with a vertical scale to fake the tilt; the tilted detail card is a pre-warped 9-slice or a flat panel.
- **Draw calls.** Home about 70. Settings about 55. Connecting about 40. Overlay about 12.
- **Textures.** Covers 1.3 MB, 8 warped variants with reflections about 2.4 MB, floor and spot 0.6 MB, icons 1.0 MB, fonts 2 MB. About 7 MB.
- **Reuse.** `ui_focus.c` as a one-axis list zone; the same zone for the drum.
- **Build risk: high.** The warp bake is new code and the only direction that needs it; without covers the carousel falls back to plainer cards. The CSS 3-D transforms in the mock are exact; the Vita version is an approximation, so judge its final look from a prototype before committing.

## Assumptions
1. Stream overlay content (latency, FPS, bitrate, "Network Unstable") and Esc/Circle to leave the stream are assumed.
2. Controller, Profile and Logs content is placeholder, to show structure.
3. All counts are paper estimates. Texture sizes assume RGBA8 at the stated resolutions.
4. Fonts are open-licence families per direction: Manrope, Nunito, Space Grotesk and Outfit (OFL). The licence file would ship with the winner.
5. Art: covers, scene and wallpaper are AI-generated fictional images (checked by eye for logos and text; none found). Total image weight is under 0.3 MB.
