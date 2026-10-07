# UI mocks: feasibility notes (issue #271)

A short realism check for each mocked direction. It is not an implementation plan. Nothing here was built or measured on a Vita; all counts are estimates from the mocks and the ground rules in #271. The headline claim to take away: all three fit the 15 MB texture budget, and all three can be drawn with a small number of draw calls if rounded shapes are pre-baked textures (one tinted quad each) rather than per-pixel rects.

## Shared parts (all three)

| Mock element | On the Vita |
|---|---|
| Manrope 500/700/800, four sizes (16/20/28/48) | FreeType into the existing `ui_text.c` atlas. 3 weights x 4 sizes = 12 faces; today it pre-renders 7 sizes of 2 families. |
| Rounded panels, chips, tiles | One baked white rounded PNG, drawn tinted with `vita2d_draw_texture_tint`. Fixed-size shapes (tiles, chips, tabs) are 1 call; variable-size panels use a 9-slice (9 calls). |
| `backdrop-filter: blur` glass panels | A pre-blurred copy of the backdrop, baked once per background change, cropped under the panel, then a tinted 9-slice on top. No real-time blur. |
| Soft glow around the focused item | A baked glow 9-slice PNG, tinted. 9 calls. |
| Gradients (bars, shades, vignette) | One `vita2d_draw_array` triangle strip with per-vertex colour. 1 call. |
| Console art | 2 small textures (PS5-like slab, PS4-like box), LED colour as a separate tinted dot. The mock SVG is placeholder art with no Sony marks. |
| Cover art | Optional, fetched by title ID, cached in `ux0:data/vitarps5/covers/`, decoded once into a 170x226 texture. Fallback is a baked "no art" texture plus the game name as text. |
| Focus, input, popups, IME | Unchanged: `ui_focus.c`, `ui_input.c`, the IME dialog. |

Assumption: the zone-based focus manager in `ui_focus.c` is reused as is for list and row navigation. I did not verify it handles 2-D grid movement; the Launcher needs that (see its build risk).

## 1. XMB "Remote Play Bar"

**(a) Element to call**
- Animated ribbons: 4 `vita2d_draw_array` strips (about 100 vertices each), one per ribbon, updated on the CPU each frame. Palette tinted by time of day (night/dawn/day/dusk from the RTC; a manual override exists in the mock only).
- Background gradient: 1 `draw_array` quad. Vignette: 1 tinted baked texture.
- Category row: 5 icon quads, selected icon gets a baked ring (9-slice) and a label text. Motion is a position lerp (240 ms).
- Item list: per row a baked icon tile (1), console art (1), title text (1), subtitle text (1). Font size swap on the selected row (28 px vs 20 px) instead of scaling.
- Right value panel: tinted 9-slice glass (9), cover (1), chip (1+1), up to 4 label/value rows as text (about 8). Radio options and the toggle are baked quads.
- Options side panel (Triangle): glass 9-slice, slides in, 4 rows. Only drawn when open.
- Hint bar: 1 gradient strip plus 4 icon+text pairs.

**(b) Draw calls, Home** (Consoles, selected console, panel open): about 88. Breakdown: ribbons and background 6, category row 16, list 20, panel 35, status bar 3, hints 9. This is just over the 80 target. Trimming the panel rows to two pre-joined text strings brings it to about 75. Options panel open: add about 25. Waking: about 55. In-stream overlay: about 14 (strip, 3 stats as 6 texts, 2 hint texts, badge).

**(c) New textures and memory (estimate)**: icon atlas 512x512 about 1.0 MB; console art 2 about 0.2 MB; covers 4 cached about 0.6 MB (12 max about 1.8 MB); glass and glow 9-slices about 0.15 MB; vignette about 0.13 MB; fonts about 2 MB. **Total about 4 MB.** No full-screen background texture, since the ribbons are geometry.

**(d) Reuse**: `ui_focus.c` zones map to category row, list, value panel and options panel. `ui_input.c` unchanged. The IME dialog is unchanged. `ui_text.c` atlas is extended with the new family. The wave sidebar and `ui_navigation.c` go away.

**(e) Build risk: low to medium.** Lightest on rendering and the most controller-native. Risks: the CPU cost of recomputing ribbon vertices at 60 fps while a stream is starting (mitigate: 30 Hz update, or freeze while connecting); Settings has 15 items in a long vertical list. The least reuse of the existing card UI.

## 2. Showcase carousel

**(a) Element to call**
- Per-console backdrop: one blurred texture (480x272 upscaled) per console, cross-faded with a tint alpha (480 ms). Without a cover, a baked hue gradient plus `draw_array`. Standby desaturation is a grey tint.
- Left shade and bottom shade: 2 `draw_array` calls.
- Hero text: label, title (48 px), subtitle, call-to-action pill (baked, tinted, 1) and its text and icon.
- Console art and cover: 1 quad each, drop shadow as a baked 9-slice.
- Metadata panel: baked glass strip, 4 label/value pairs as 8 texts, 3 divider rects.
- Carousel strip: 5 baked card textures (1 each, tinted), art, 2 texts per card. Selected card gets a glow ring and a position lerp. Off-screen cards are skipped.
- Settings: 6 tab pills (baked, tinted), rows as baked rounded rects, toggle as a 2-state baked quad, `[<]` `[>]` arrows as icon quads, right panel 9-slice glass.
- Waking: backdrop, big title, 8-segment bar (8 rects), 8 numbers.

**(b) Draw calls, Home**: about 100 as mocked (backdrop and shades 3, top bar 6, hero 25, art and cover 11, metadata 20, strip 29, hints 6). With optimisations (draw non-focused strip cards as one pre-baked strip texture, join metadata into 2 texts) about 65. Settings: about 45 (visible rows limited to 6). Waking: about 30. In-stream overlay: about 10.

**(c) New textures and memory (estimate)**: 5 blurred backdrops about 2.6 MB (only the current plus neighbours need to be resident, so 1.6 MB); covers 4 to 12 about 0.6 to 1.8 MB; console art about 0.2 MB; icon atlas about 1.0 MB; baked cards, pills and glass about 0.3 MB; fonts about 2 MB. **Total about 7 to 9 MB.** The heaviest of the three but inside budget.

**(d) Reuse**: `ui_focus.c`: the carousel is a one-axis list zone, Settings rows a second zone. `ui_console_cards.c` data model reused, drawing replaced. IME and popups unchanged.

**(e) Build risk: medium.** It depends on assets we do not have yet: nice console art, the covers fetch (libcurl, caching, a title-ID to image source we have not chosen, and a fallback when offline), and `running_app_name` surfaced from `discovery.c`. The backdrop blur has to be generated on the Vita on first load (a small box blur at 480x272 is cheap, but it is new code) or pre-shipped. Without covers it is still good, but less special.

## 3. Launcher home

**(a) Element to call**
- Flat background: one clear colour. No gradients on Home.
- Tiles: one baked white rounded rect per size (215x208, 215x96, and the two-wide 442x184), drawn tinted per state colour. The dashed "not registered" outline is a separate baked texture.
- Tag, name, subtitle: 3 texts. Console art: 1 quad (tinted LED).
- Selected tile: a baked white ring, 1 call, drawn with a small scale (position and size lerp).
- Page tabs: 3 baked pills, active one in the page colour, plus L/R chips.
- Settings page: 15 tiles, 4 columns, 3 rows visible, vertical scroll by row (culled).
- Clock and status icons: 1 text, 2 icons.
- Waking: the console tile, step title, and an 8-block bar of baked tinted rects.

**(b) Draw calls, Home**: about 64 (background 1, top bar 12, console tiles 24, action tiles 16, focus ring 1, detail strip 5, hints 5). Settings: about 55. Profile: about 40. Waking: about 40. In-stream overlay: about 8.

**(c) New textures and memory (estimate)**: baked tile, tab and ring shapes about 0.3 MB; console art about 0.2 MB; icon atlas about 1.0 MB; fonts about 2 MB. **Total about 3.5 MB.** The smallest of the three, and it needs no cover art.

**(d) Reuse**: `ui_focus.c` list zones can be reused, but this direction wants 2-D movement (up, down, left, right between tiles); the header suggests zones are index based, so a small neighbour lookup per page is probably needed. `ui_console_cards.c` data reused. Touch maps cleanly to tiles. IME and popups unchanged.

**(e) Build risk: low.** Fewest assets, all geometry is flat, and it is cheap to draw. Risks: it shows nothing of the running game, so it is the least "wow"; large text tiles truncate long console names (the mock ellipsises at 28 px); and with only 4 consoles most of the grid is quick actions, so the layout can feel sparse or crowded depending on how many consoles people own.

## Open assumptions
1. Stream overlay in the mocks shows latency, FPS, bitrate and a "Network Unstable" badge, with `Esc` / Circle to leave. I did not check how the real overlay leaves a stream.
2. The Controller, Profile and Logs content in the mocks is placeholder, showing the navigation structure only.
3. All draw-call counts are paper estimates; none was measured. Texture sizes assume RGBA8 at the stated resolutions.
4. Font choice is Manrope (OFL) with weights 500/700/800. Inter is a fine alternative; the mocks only need a face with these three weights. The licence file would ship with the font.
