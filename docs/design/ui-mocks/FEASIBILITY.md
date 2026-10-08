# XMB feasibility (issue #271, round 6c)

One current assessment of the locked XMB design (`SPEC.md`, `xmb.html`) for vita2d. It replaces every earlier version. Nothing was built or measured on a Vita; all counts and sizes are paper estimates to be checked on hardware. Budgets: about 80 draw calls on Home, about 15 MB of textures.

## 1. Asset inventory

| Asset | Source | Used for | Notes |
|---|---|---|---|
| `icon_play.png`, `icon_settings.png` (48 px) | existing in the app | Consoles and Settings categories, setting-group rows | white, drawn tinted |
| `icons/controller.svg`, `icons/profile.svg` | new, flat white | Controller and Profile categories, page titles | bake to 48 px PNG |
| Room icons `tv, sofa, bed, bunk, desk, house` | new, flat white | status ring centre | one atlas, 6 x 48 px, under 0.05 MB; default tv |
| Status ring and badge | baked once | console state | one 2 px ring texture per state colour (OK, WARN, ERR) + one dashed IDLE ring + 6 badge glyphs; tinted at draw time |
| Status dot | none | list rows | flat circle `vita2d_draw_fill_circle` in a token colour (the ellipse PNGs are no longer used) |
| `PS5_logo.png` (132 x 49), `ps4.png` (100 x 100) | existing in the app | detail panel, Connecting | cropped to the wordmark, drawn large in white |
| Button symbols `symbol_ex, circle, square, triangle` | existing in the app | hint row, inline text | 28 px, scaled to 24 |
| Flat hint glyphs: D-pad (all, left-right, up-down), L, R, Start, Select | new, baked | hint row, login steps, stream exit pill | 24 px high, white |
| Wi-Fi, battery, check, warning, lock, globe, moon, clock, chevrons, close | new, simple strokes | top bar, badges, rows, popups | one small glyph atlas |
| `controller_front.png` (874 x 396), `controller_back.png` (720 x 327) | existing in the app | Controller screens | unchanged; the mock uses `controller_back_clean.png` (the tiny "Sony Computer Entertainment Inc" line erased), ship that copy |
| `Vita_RPS5_Logo.png` | existing | top bar | scaled to 32 px high |
| Glow | one baked soft white blob texture | focus glow behind icons, text glow | reused for every glowing element |
| Wave | no texture | background | ribbons are geometry (section 3) |

Removed from the app by this design: the wave sidebar, particles, rounded-rect and shadow helpers, the Logs screen, the Add item, the third Profile card, the `dropdown` widget, 6 modal implementations.

## 2. Type

4 sizes, 6 faces, pre-rendered into the existing FreeType atlas:

| Face | Size | Use |
|---|---|---|
| Roboto Light | 20, 28 | rows, buttons, titles |
| Roboto Regular | 16 | hints, captions, kv rows |
| Roboto Mono Regular | 16, 28, 40 | IDs and values (16), Reconnecting bitrate (28), PIN digits (40) |

Today the app loads Roboto Regular and Roboto Mono only (7 sizes). Roboto Light is **one new TTF (170 KB)**, decided. Atlas about 1.5 MB in total (about 0.3 MB more than today). A soft text shadow is the same text drawn once more, offset and dark: titles only if the call budget is tight.

## 3. Background

Fixed palette (no time of day). 5 ribbons: one triangle strip per ribbon fill plus one line strip per highlight, vertex colours fade the edges, one full-screen gradient, 36 dust points. Vertices are updated on the CPU at 30 Hz; freeze or halve the update while Connecting. About 12 calls and no texture. Page wash, scrim and the Home vignette are full-screen alpha rectangles (1 call each). There is no backdrop blur on the Vita: popups use a darker scrim, or a pre-blurred copy of the last frame.

## 4. Draw-call budget per screen (paper)

| Screen | Estimate | Breakdown |
|---|---|---|
| Home (Consoles) | about 75 | wave 12, categories 5, list 32 (4 rows x 8), detail 14, hint row 10, top bar 6, filter line 2 |
| Home with Options open | about 55 | dimmed layers are still drawn; the column adds about 15; skip the list cascade |
| Settings, Profile | about 40 | groups 6, rows 6 x 5, lines, identity block (Profile) 5, hint row 10 |
| Controller summary | about 30 | diagram 1, callouts 6, footers 4, preset switcher 4, hint row 12 |
| Controller zone view | about 60 | diagram 1, 18 cells + 18 labels, hint row 10, borders for picked and cursor |
| PIN | about 35 | 8 digit boxes, chevrons, 3 buttons, prompt, hint row |
| Connecting | about 30 | ring, spinner arc, halo, logo, steps text, button |
| Stream overlay | about 8 | up to 3 pills or panels |
| Popups | about 15 to 20 on top of the screen behind | the screen behind is drawn once, dimmed |

Home is at the 80-call budget. Two savings if it goes over: skip the text shadow, and draw at most 3 full rows plus a faded fourth. Status hints are wrapped once when they change (word-by-word `ui_text_width`), never per frame.

## 5. Texture budget (paper)

| Group | MB |
|---|---|
| Category and group icons, glyph atlas (hint glyphs, status icons) | 0.15 |
| Room-icon atlas, badges, rings, glow | 0.25 |
| PS5 and PS4 logos | 0.05 |
| Controller diagrams at source size (scale down for a saving) | 2.5 |
| Font atlases (6 faces) | 1.5 |
| Top-bar logo, misc | 0.1 |
| **Total** | **about 4.6** |

Well under the 15 MB budget. The wave costs none.

## 6. Reuse

- `ui_controller_diagram.c` and the controller screen logic: unchanged behaviour (summary pages, zone views, mapping popup, preset slots). Only chrome, colours and the new Triangle entry to the zone view change.
- `ui_focus.c` zones: category row, list, options column, page groups and rows, controller views, popup. Not verified that nothing in the focus manager assumes the wave sidebar.
- `ui_input.c`, the IME dialog, `ui_text.c` atlas, `ui_qr.c`: reused as they are.

## 7. Risks

| Risk | Level | Mitigation |
|---|---|---|
| Static screens, popups, pages, overlay | low | plain rectangles and text |
| Home list with variable row heights (wrapped hint lines) and slide animation | medium | one layout function; compute heights once per data change; hints wrap at most 2 lines (copy rule) |
| Touch swipe and paint gestures (list, categories, pane, popup list, zone paint) | medium | thresholds in SPEC section 4; a touch over 8 px is never also a tap |
| Registration and connection outcomes (finished OK, PIN not accepted, unreachable, timeout) | medium, backend | the UI needs these four results from the registration code (SPEC 3.3), not UI work |
| Wi-Fi, battery and clock reads for the top bar | low | poll about once per second (`sceNetCtl`, `scePower`, RTC) |
| Glow looks right on the Vita OLED | low | the mock uses CSS drop-shadow; confirm the baked texture on hardware |
| All numbers are estimates | n/a | measure Home first; it is the screen at the budget |

## 8. Glass option (CEO exploration, round 6e, default off)

Two optional treatments between the wave and the UI, both switchable in the mock (toolbar "Glass", or `?glass=soft|panels` on any deep link). The default design is unchanged.

**soft**: the wave is rendered into a 240 x 136 render target (1/4 size) and drawn upscaled with bilinear filtering; that upscale is the blur. A light dark veil (`rgba(2,5,14,.14)`) is drawn over it. The mock does exactly this (canvas at 1/4 size, `drawImage` with smoothing). A 4x bilinear upscale is a mild blur (roughly 4 px); for the 6 to 10 px the brief mentions, use a 120 x 68 target (1/8).
**panels**: frosted rectangles behind the content zones only (Home: list column and detail panel; pages: group list and pane; none on Controller, PIN, Connecting). Blur 16 px, white tint 5%, 1 px `LINE_FAINT` edge, square, no shadow. The mock uses CSS `backdrop-filter: blur(16px)`. Vita method: the wave drawn into a smaller texture (1/8 or 1/16, 120 x 68 or 60 x 34, because 16 px of blur needs a very small texture), drawn only inside the panel rects with `vita2d_set_clip_rectangle`, then a tint rectangle and an outline. The mock's blur is a true Gaussian; the Vita result will be blockier-soft, so expect it to look slightly different.

### Cost (paper estimates, not measured)

| | soft | panels |
|---|---|---|
| Extra texture memory | one 240 x 136 RGBA render target, about 0.13 MB (up to 0.15 MB with alignment) | the same target plus a 120 x 68 one, about 0.16 MB in total |
| Draw calls | wave geometry (about 12 calls) moves into the render target; add 1 upscaled quad and 1 veil rectangle: about +2 | per panel: 1 clipped quad, 1 tint rect, 1 outline (4 thin rects): about 6; Home has 2 panels, so about +12 (Home 75 -> about 87, over the 80 budget) |
| Per-frame fill cost | the wave is shaded at 1/16 of the pixels (about 33k instead of 522k per layer, about 5 overlapping strips), then one full-screen bilinear quad (522k). Likely equal or cheaper than today's full-resolution wave. The render target can be refreshed at 30 Hz while the quad is drawn every frame | one extra small render pass plus about 40% of the screen redrawn through the clipped quads; roughly +10% fill |
| Unknowns | cost of switching to a render target and back on GXM (one extra scene or render-target pass per frame); measure | same, plus the clip rectangle changes |

### Legibility measurements

Method: for each screen the text is made transparent, the wave is frozen at 12 different instants (0 to 66 s in 6 s steps, covering about one full cycle of the slowest ribbon), and the pixels inside each text line's rectangle are measured. Contrast is WCAG (text colour luminance + 0.05) / (background luminance + 0.05). Text colours: body `TEXT_2` #DDE3F0, hint `ERR` #FF8080. "brightest px" is the worst case: the single brightest background pixel under any glyph row at the worst instant (thin ribbon highlight lines). "p95" is the 95th-percentile pixel at the worst instant. "mean" is the average pixel at the worst instant. 4.5 is the WCAG AA target for body text.

| Text on | variant | brightest px | p95 | mean (worst instant) |
|---|---|---|---|---|
| Home list text (TEXT_2) | off | 2.57 | 4.17 | 6.29 |
| | soft | 4.64 | 5.42 | 7.48 |
| | panels (white 5%) | 4.07 | 4.46 | 5.50 |
| | panels, dark tint (extra test) | 5.63 | 6.80 | 8.11 |
| Home red hints (ERR) | off | 1.48 | 2.50 | 4.23 |
| | soft | 2.23 | 2.99 | 4.84 |
| | panels (white 5%) | 2.32 | 2.37 | 3.85 |
| | panels, dark tint (extra test) | 3.03 | 3.61 | 4.99 |
| Settings description and labels (TEXT_2) | off | 4.53 | 6.81 | 9.75 |
| | soft | 6.69 | 7.92 | 10.72 |
| | panels (white 5%) | 4.53 | 6.62 | 8.97 |
| Profile red error value (ERR) | off | 3.65 | 5.04 | 5.09 |
| | soft | 4.88 | 5.25 | 5.28 |
| | panels (white 5%) | 4.34 | 4.34 | 4.35 |

Isolating the two ingredients of soft (same method): blur alone raises the brightest-px case (Home 2.57 -> 3.66, hints 1.48 -> 1.74) and leaves the mean unchanged (Home 6.29 -> 6.30); the veil alone raises the mean (Home 6.29 -> 7.47, hints 4.23 -> 4.85) and the p95 (4.17 -> 5.23), with a smaller gain on the brightest px (3.36).

### What it shows, and the recommendation

- **soft helps a little, mostly through the veil.** Typical contrast rises about 10 to 19% and the worst thin-highlight spots improve (Home brightest px 2.6 to 4.6). The blur itself only softens the bright ribbon lines; it does not lift typical contrast. Visually soft is almost indistinguishable from today (ribbons slightly softer); it keeps the look the CEO likes.
- **panels with the specified white tint make legibility slightly worse** (the tint lightens the background: Home mean 6.3 -> 5.5, hints 4.2 -> 3.9). A dark tint fixes that (Home 8.1, hints 5.0) but is a different, darker look. **Honest look verdict: on Home the two frosted rectangles read as cards**, especially the detail panel with its edge; on pages the pane panel reads as a box. This is the boxy look rejected earlier, so panels are not recommended.
- **Red hint text is the weak spot in every variant.** `ERR` #FF8080 on the ribbons averages 4.2 to 4.9 and the worst spots are 1.5 to 3 for all variants. Neither blur nor panels fixes that; a slightly lighter ERR hint colour or a dark text shadow would.
- **Recommendation:** if the CEO wants a legibility gain, adopt **soft** at 1/4 resolution with the 14% veil: it costs about +2 draw calls, about 0.13 MB, is probably cheaper than today's wave, and does not change the look. The same mean-contrast gain is available from the veil alone with no render target at all (one rectangle), so the cheapest honest option is veil only; add the 1/4 blur if the CEO likes the softer ribbons. Do not build panels. Separately, lighten the ERR hint text colour or add a text shadow behind hint lines.

### Round 6f: strong glass (the panel effect over the whole screen, no rectangles)

Modes in the mock: `?glass=strong` (frost) and `?glass=strongdark`; toolbar cycles off / soft / strong (frost) / strong (dark) / panels. Panels stay for reference (rejected).

**Blur method and which resolution to ship.** The mock renders the wave into a small canvas and draws it upscaled with bilinear smoothing, exactly the Vita method, so for strong the mock is a faithful preview (only the browser's smoothing kernel differs a little from GXM's). Tried at the same instant (`?gdiv=8|16|2step`):
- 1/8 (120 x 68): visible grid and cross-hatch artefacts from the 8x bilinear upscale, and the ribbons still read as bands. Too blocky for a "frosted" look.
- 1/4 then 1/8 (two bilinear downsample steps): smooth, a little more ribbon structure survives, but it needs the 1/4 pass anyway.
- 1/16 (60 x 34): smooth, no visible blocks, ribbons become soft clouds of light. **Ship 1/16**: one tiny render target (60 x 34 RGBA = 8 KB), one pass, cheapest. A 16x bilinear upscale is a triangle kernel about 32 px wide (roughly a Gaussian of sigma 6 to 7 px); the mock's panel blur is a true `blur(16px)` (sigma 16), so panels were blurrier on paper, but at this ribbon scale (bands 35 to 70 px thick) the strong result reads about the same.

**Variants.** *strong (frost)*: white 5% tint plus the 14% dark veil. *strong (dark)*: 20% dark veil, no white. The ribbons survive as moving soft light in both; frost is hazier and lighter, dark is a deep gradient with faint glow.

**Cost (paper estimates).** One 60 x 34 render target (8 KB; a second 240 x 136 only if you want the two-step version, 130 KB). Draw calls: the wave geometry (about 12) moves into the target, plus 1 upscaled full-screen quad and 1 veil rectangle: about +2. Fill: the wave is shaded at 1/256 of the pixels instead of full screen, then one bilinear quad (522k pixels) and the veil (522k): likely cheaper than today's full-resolution wave (5 overlapping strips over 522k). Because the result is so soft, the ribbons can be updated into the small target at 15 to 30 Hz while the quad is drawn every frame; nobody will see the lower rate. Unknown: the cost of switching to a render target and back (one extra render-target pass per frame), to be measured; skipped entirely on frames where the ribbons are not updated.

**Contrast** (same method as above: text made transparent, wave frozen at 12 instants, brightest pixel / p95 / mean at the worst instant):

| Text on | variant | brightest px | p95 | mean |
|---|---|---|---|---|
| Home list, TEXT_2 | off | 2.57 | 4.17 | 6.29 |
| | soft | 4.64 | 5.42 | 7.48 |
| | strong (frost) | 4.59 | 4.88 | 6.47 |
| | strong (dark) | 5.73 | 6.10 | 7.92 |
| Home red hints, ERR | off | 1.48 | 2.50 | 4.23 |
| | soft | 2.23 | 2.99 | 4.84 |
| | strong (frost) | 2.35 | 2.71 | 4.21 |
| | strong (dark) | 2.88 | 3.35 | 5.10 |
| Settings, TEXT_2 | off | 4.53 | 6.81 | 9.75 |
| | soft | 6.69 | 7.92 | 10.72 |
| | strong (frost) | 6.49 | 6.89 | 9.29 |
| | strong (dark) | 8.14 | 8.52 | 11.20 |
| Profile error, ERR | off | 3.65 | 5.04 | 5.09 |
| | soft | 4.88 | 5.25 | 5.28 |
| | strong (frost) | 4.39 | 4.47 | 4.54 |
| | strong (dark) | 5.25 | 5.32 | 5.36 |

**Verdict (strong).**
- **Look.** Strong does not feel like XMB any more; it feels like a blurry wallpaper. The waves lose their lines and edges and turn into soft cloud shapes; there is no more sense of ribbons flowing past. Frost is hazy and slightly milky; dark is almost a flat dark-blue gradient with a faint glow. Without the thin highlight lines the background loses the "moving light" that defines the XMB feel; soft keeps it.
- **Legibility.** Frost is no better than off on typical contrast (the white tint cancels the veil: Home mean 6.29 to 6.47, hints 4.23 to 4.21, Profile error 5.09 to 4.54, which is worse); it only removes the worst peaks. Dark is the best of all variants (Home mean 7.92, hints 5.10, Settings 11.2, Profile error 5.36), only about 1.5 to 6% above soft on mean and 0.4 to 1.5 above soft on the worst peaks, and it is the one that looks flattest. Soft is within 6% of strong dark on typical contrast while keeping the ribbons.
- **Pick.** If the CEO wants the strong look at all, ship **strong (dark)** at 1/16, not frost. My overall recommendation is unchanged: **soft** (or just the veil) is the best trade; it keeps what the CEO likes about the look and gets most of the legibility gain. Strong dark is the choice only if legibility matters more than the wave.
