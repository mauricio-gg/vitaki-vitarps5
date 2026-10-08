# XMB feasibility (issue #271, round 7)

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
| Button symbols `symbol_ex, circle, square, triangle` | existing in the app | hint row, inline text | 28 px, scaled to 20 in the hint row, 20 inline |
| Flat hint glyphs: D-pad (all, left-right, up-down), L, R, Start, Select | new, baked | hint row, login steps, stream exit pill | 20 px high in the hint row (the 4 existing symbol PNGs are scaled from 28 to 20); L, R, Start, Select are pill badges; white |
| Rounded shape textures | new, baked once | selection bars, buttons, toggles, popups, toast, pills, PIN boxes, icon cells | `R_SM` 8: one 24 x 24 white 9-slice; `R_MD` 16: one 48 x 48 white 9-slice (popup also has a 1 px border variant); pill: one 48 x 48 3-slice (left cap, stretched middle, right cap) plus a bordered variant; toggle: track on/off 48 x 24 and a 16 px round knob; PIN box 56 x 72 (idle and focused). All white, tinted at draw time. About 0.05 MB in total |
| Wi-Fi, battery, check, warning, lock, globe, moon, clock, chevrons, close | new, simple strokes | top bar, badges, rows, popups | one small glyph atlas |
| `controller_front.png` (874 x 396), `controller_back.png` (720 x 327) | existing in the app | Controller screens | unchanged; the mock uses `controller_back_clean.png` (the tiny "Sony Computer Entertainment Inc" line erased), ship that copy |
| `Vita_RPS5_Logo.png` | existing | top bar | scaled to 32 px high |
| Glow | baked soft blob textures (white, and one per status colour) | focus glow behind icons, rings and text | each padded by its glow radius on all sides (art + 2 x radius, transparent border), drawn before the art, never clipped to the art or the row (SPEC 2.0 Glow rule) |
| Wave | no texture | background | ribbons are geometry (section 3) |

Removed from the app by this design: the wave sidebar, particles, rounded-rect and shadow helpers, the Logs screen, the Add item, the third Profile card, the `dropdown` widget, 6 modal implementations.

## 2. Type

**5 sizes, 5 faces, Roboto only; no mono face.** Pre-rendered into the existing FreeType atlas:

| Face | Size | Use |
|---|---|---|
| Roboto Light | 20, 28, 40 | rows and buttons (20), titles (28), PIN digits (40) |
| Roboto Regular | 14, 16 | hint row (14), captions, kv rows and values such as IPs and stats (16) |

Today the app loads Roboto Regular and Roboto Mono with 7 pre-rendered sizes. Changes: Roboto Mono and its atlas go away; **Roboto Light is one new TTF (170 KB)** with 3 sizes; Roboto Regular 14 already exists today (`FONT_SIZE_SMALL`), so the hint row adds no new atlas size; Roboto Regular 16 already exists. Net: 5 faces instead of today's 7 sizes plus the mono atlas, so the atlas is no larger than today (about 1.4 MB). Roboto's digits are equal width, so numbers still line up without OpenType features (FreeType does not apply them). A soft text shadow is the same text drawn once more, offset and dark: titles only if the call budget is tight.

## 3. Background

Fixed palette (no time of day). 5 ribbons: one triangle strip per ribbon fill plus one line strip per highlight, vertex colours fade the edges, one full-screen gradient, 36 dust points. Vertices are updated on the CPU at 30 Hz; freeze or halve the update while Connecting. About 12 calls and no texture. Page wash, scrim and the Home vignette are full-screen alpha rectangles (1 call each). There is no backdrop blur on the Vita: popups use a darker scrim, or a pre-blurred copy of the last frame.

## 4. Draw-call budget per screen (paper)

| Screen | Estimate | Breakdown |
|---|---|---|
| Home (Consoles) | about 75 (focus glow is one padded blob per row, 4 rows: +3 vs the earlier count, offset by dropping the red hint lines) | wave 12, categories 5, list 32 (4 rows x 8), detail 14, hint row 10, top bar 6, filter line 2 |
| Home with Options open | about 55 | dimmed layers are still drawn; the column adds about 15; skip the list cascade |
| Settings, Profile | about 52 | groups 6, rows 5 x 5 (a focused row is a 3-slice rounded bar: 3 draws; each toggle is a track texture + a knob texture: 2 draws instead of 3 rectangles), lines, identity block (Profile) 5, hint row 10 |
| Controller summary | about 30 | diagram 1, callouts 6, footers 4, preset switcher 4, hint row 12 |
| Controller zone view | about 60 | diagram 1, 18 cells + 18 labels, hint row 10, borders for picked and cursor |
| PIN | about 38 | 8 digit boxes (one baked 56 x 72 rounded texture each), chevrons, 3 pill buttons (3-slice: 9 draws), prompt, hint row |
| Connecting | about 30 | ring, spinner arc, halo, logo, steps text, button |
| Stream overlay | about 8 | up to 3 pills or panels |
| Popups | about 25 to 30 on top of the screen behind | a 9-slice popup body is 9 draws (was 4 rectangles), buttons are 3-slice pills, a focused list row is a 3-slice bar; the screen behind is drawn once, dimmed |

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
| Rounded shape textures (9-slice, 3-slice, toggle, PIN box) | 0.05 |
| **Total** | **about 4.65** |

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

## 8. Background Blur (user setting: None / Soft / Strong / Dark)

Settings > Display > Background Blur, default **None** (CEO decision, round 7). It applies live and is stored as one integer in the config (SPEC flag 13).

| Mode | Method | Veil | Look |
|---|---|---|---|
| None | today's full-resolution wave | none | sharp ribbons with thin bright highlight lines |
| Soft | wave into a 240 x 136 render target (1/4), bilinear upscale | 14% dark | nearly identical to None, ribbons slightly softer; still reads as XMB |
| Strong | wave into a 60 x 34 target (1/16), bilinear upscale | 5% white + 14% dark | soft clouds of light, hazy; the ribbons lose their lines |
| Dark | same 60 x 34 target | 20% dark | deep calm gradient with a faint glow; flattest look |

Panels (frosted rectangles behind the content zones) were tried and rejected: they read as cards.

### Cost (paper estimates, not measured)

| | None | Soft | Strong / Dark |
|---|---|---|---|
| Extra texture memory | 0 | one 240 x 136 RGBA target, about 0.13 MB (up to 0.15 MB aligned) | one 60 x 34 target, 8 KB |
| Draw calls | wave about 12 | same geometry, drawn into the target, plus 1 upscaled quad and 1 veil rect: about +2 | same: about +2 (Strong adds one more tint rect: about +3) |
| Fill | wave shaded at full resolution (about 5 overlapping strips over 522k pixels) | wave shaded at 1/16 of the pixels, then one bilinear quad and the veil over 522k pixels: likely equal or cheaper | wave shaded at 1/256 of the pixels, same quad and veil: cheapest |
| Update rate | 30 Hz CPU vertices | target can refresh at 15 to 30 Hz while the quad is drawn every frame | 15 to 30 Hz is invisible at this softness |
| Unknown | none | cost of switching to a render target and back on GXM (one extra pass per frame), skipped on frames where the ribbons are not updated; measure | same |

The mock renders exactly this way (small canvas, upscaled with bilinear smoothing), so for Soft, Strong and Dark it is a faithful preview; only the browser's smoothing kernel differs a little from GXM's. Resolution choice for Strong and Dark was made by trying 1/8 (visible grid artefacts from the 8x upscale), 1/4 then 1/8 (smooth but needs the 1/4 pass anyway) and 1/16 (smooth, one tiny pass): 1/16 ships.

### Legibility (WCAG contrast of the text against the pixels actually behind it)

Method: text made transparent, wave frozen at 12 instants (0 to 66 s in 6 s steps, about one full cycle of the slowest ribbon), pixels inside each text line's rectangle measured. Body text `TEXT_2` #DDE3F0, hints and errors `ERR` #FF8080. "Brightest px" is the worst single background pixel at the worst instant (thin highlight lines), "p95" the 95th percentile, "mean" the average, all at the worst instant. 4.5 is the WCAG AA target for body text.

| Text on | mode | brightest px | p95 | mean |
|---|---|---|---|---|
| Home list, TEXT_2 | None | 2.57 | 4.17 | 6.29 |
| | Soft | 4.64 | 5.42 | 7.48 |
| | Strong | 4.59 | 4.88 | 6.47 |
| | Dark | 5.73 | 6.10 | 7.92 |
| Home red hints, ERR | None | 1.48 | 2.50 | 4.23 |
| | Soft | 2.23 | 2.99 | 4.84 |
| | Strong | 2.35 | 2.71 | 4.21 |
| | Dark | 2.88 | 3.35 | 5.10 |
| Settings, TEXT_2 | None | 4.53 | 6.81 | 9.75 |
| | Soft | 6.69 | 7.92 | 10.72 |
| | Strong | 6.49 | 6.89 | 9.29 |
| | Dark | 8.14 | 8.52 | 11.20 |
| Profile error, ERR | None | 3.65 | 5.04 | 5.09 |
| | Soft | 4.88 | 5.25 | 5.28 |
| | Strong | 4.39 | 4.47 | 4.54 |
| | Dark | 5.25 | 5.32 | 5.36 |

What the numbers say:
- The veil, not the blur, lifts typical contrast. Blur alone only softens the brightest thin lines (Home brightest px 2.57 to 3.66, mean unchanged at 6.30); a 14% veil alone lifts the mean (Home 6.29 to 7.47).
- **Soft** gains about 10 to 19% on mean and clears the worst spots, and keeps the XMB look.
- **Strong** is no better than None on typical contrast (the white tint cancels the veil; Profile error gets worse, 5.09 to 4.54) and looks like a blurry wallpaper. **Dark** has the best numbers but is only about 1.5 to 6% above Soft on mean, and the flattest look.
- Red error text (`ERR`) is the lowest-contrast text in every mode (averages 4.2 to 5.1, worst spots 1.5 to 2.9; measured when it was in the list, now it is in the info panel). The CEO says it reads fine, so there is no change.

Decision: the setting ships with **None** as the default (today's look); Soft is the recommended choice if a legibility gain is wanted; Strong and Dark are there for people who want a calmer background.
