# XMB feasibility (issue #271, round 8)

One current assessment of the locked XMB design (`SPEC.md`, `xmb.html`) for vita2d. It replaces every earlier version. Nothing was built or measured on a Vita; all counts and sizes are paper estimates to be checked on hardware. References: about 80 draw calls on Home as a paper reference, not a hard limit for menu screens (CEO ruling, 2026-10-09, #271), and about 15 MB of textures. A menu screen passes when it stays smooth on hardware, with `UI/DRAWS` used to spot outliers; the stream overlay stays lean because it shares the CPU with decode and networking.

## 1. Asset inventory

| Asset | Source | Used for | Notes |
|---|---|---|---|
| `icon_play.png`, `icon_settings.png` (48 px) | existing in the app | Consoles and Settings category icons only | white, drawn tinted; the group rows use the dedicated item icons below, not the gear |
| `icons/controller.svg`, `icons/profile.svg` | new, flat white | Controller and Profile category icons, page titles | bake to 48 px PNG |
| Room icons `tv, sofa, bed, bunk, desk, house` | new, flat white | console list items (38 px, no ring), Connecting ring centre, icon picker | one atlas, 6 x 48 px, under 0.05 MB; default tv; dimmed to 55% for Unpaired, Unavailable and Cooldown |
| Connecting ring | baked once | Connecting screen only | one 128 px ring texture tinted per state colour (list rings and badges were removed in round 8); with the halo, 2 draws |
| Item icons | new, flat white SVG baked to 38 px | settings groups (video, network, display, controls, advanced), Profile groups (account, connection, psn), presets (slot1 to slot3), Filter (search), Pair new device (plus in a ring, #330) | 13 icons of 38 x 38 RGBA (about 5.8 KB each), about 0.08 MB as one atlas; same weight as the room icons |
| Status dot | none | list rows | flat circle `vita2d_draw_fill_circle` in a token colour (the ellipse PNGs are no longer used) |
| `PS5_logo.png` (132 x 49), `ps4.png` (100 x 100) | existing in the app | detail panel, Connecting | cropped to the wordmark, drawn large in white |
| Button symbols `symbol_ex, circle, square, triangle` | existing in the app | hint row, inline text | 28 px, scaled to 20 in the hint row, 20 inline |
| Flat hint glyphs: D-pad (all, left-right, up-down, and up only for the Pair new device hint, #330), L, R, Start | new, baked | hint row, login steps, stream exit pill | 20 px high in the hint row (the 4 existing symbol PNGs are scaled from 28 to 20); L, R, Start are pill badges; white |
| Rounded shape textures | new, baked once | focus bars, buttons, toggles, popups, toast, pills, PIN boxes, icon cells | **One rule (SPEC 1.3):** fixed-height shapes are a 3-slice baked at their own height: focus bars 48 and 56 high, buttons and toast 48, pills 32, toggle track 24 (caps 8 px or half the height, stretched middle; 3 draws). Large or variable shapes are a 9-slice: popups (`R_MD` 16, with a 1 px border variant), icon cells and the QR plate (`R_SM` 8; 9 draws). Plus a 16 px round knob and the 56 x 72 PIN box (idle and focused, 1 draw each). All white, tinted at draw time. About 0.05 MB in total |
| Wi-Fi, battery, check, warning, lock, globe, moon, clock, chevrons, close | new, simple strokes | top bar, badges, rows, popups | one small glyph atlas |
| `controller_front.png` (874 x 396), `controller_back.png` (720 x 327) | existing in the app | Controller screens | unchanged; the mock uses `controller_back_clean.png` (the tiny "Sony Computer Entertainment Inc" line erased), ship that copy |
| `Vita_RPS5_Logo.png` | existing | top bar | scaled to 32 px high |
| Glow | baked soft blob textures: one white 112 x 112 (focused list icon, same for every category), one state-tinted for the Connecting ring | focus glow behind icons and text | each padded by its glow radius on all sides (art + 2 x radius, transparent border), drawn before the art, never clipped to the art or the row (SPEC 2.0 Glow rule) |
| Wave | no texture | background | ribbons are geometry (section 3) |

Removed from the app by this design: the wave sidebar (done in #305: `ui_navigation.c`, its state, pill, toast and icons are gone), particles, rounded-rect and shadow helpers, the Logs screen, the Add item, the third Profile card, the `dropdown` widget, 6 modal implementations. #308 then removed the rest: Roboto Mono and its atlas, the Messages and Stream screens (`UI_SCREEN_TYPE_STREAM` is now `UI_SCREEN_TYPE_NONE`, the "nothing drawn yet" value), the corner logo and `screen_has_xmb_chrome()`, the old widgets in `ui_components.c` and `ui_graphics.c`, the point-size `ui_text_*` calls and the `font` global, the Regular 18, 20, 24 and 28 atlas sizes, the `show_nav_labels` config key, and 8 unused images (`background.png`, `ellipse_green/yellow/red.png`, `button_add_new.png`, `icon_controller.png`, `icon_profile.png`, `icon_button_triangle.png`).

## 2. Type

**6 faces from 2 loaded weights, Light and Regular; no mono face.** Roboto Medium appears only inside baked glyphs. Pre-rendered into the existing FreeType atlas:

| Face | Size | Use |
|---|---|---|
| Roboto Light | 20, 28, 40 | rows and buttons (20), titles (28), PIN digits (40) |
| Roboto Regular | 14, 16, 20 | hint row (14), captions, kv rows and values such as IPs and stats (16), the focused list item name (20, added in #329) |

As built (#308): the app loads Roboto Regular and Roboto Light only. Roboto Mono, its atlas and the Regular 18, 20, 24 and 28 sizes are gone; **Roboto Light is one TTF (170 KB)** with 3 sizes; Regular is baked at 14 and 16. Net: 5 faces from 2 weights at that time, where the app before the redesign had 7 sizes plus the mono atlas: an estimate of about 1.4 MB for the five faces (superseded: the atlas cost is two fixed 1 MiB textures, about 2.10 MB, see the next paragraph; section 5 uses that figure). Roboto's digits are equal width, so numbers still line up without OpenType features (FreeType does not apply them). A soft text shadow is the same text drawn once more, offset and dark: titles only if the call budget is tight.

**Cost of Regular 20 (#329).** None in texture memory. The vendored `third-party/libvita2d/vita2d_font.c` gives each font object one fixed 1024 x 1024 `U8_R111` atlas texture (`ATLAS_DEFAULT_W/H`, 1 MiB) and keys glyphs by FreeType glyph index only (`texture_atlas_get(font->atlas, glyph_index, ...)`), not by size; a glyph is rasterised once, at the first size that draws it (2x supersampled), and every other size of that font draws the same bitmap scaled by `size / glyph_size`. So adding a Regular size adds no glyphs and no texture: Regular 20 reuses the glyphs baked for Regular 14 (28 px bitmaps, drawn at 20/28), and the real atlas total is fixed at two 1 MiB textures (Regular and Light), not 1.4 MB. The focused title's Regular 20 is therefore minified about 1.4:1 from the 14 pt bake, a little softer than the 2:1 of the Light faces. The only added per-frame cost is one glow draw behind the focused title.

## 3. Background

Fixed palette (no time of day). 5 ribbons: one triangle strip per ribbon fill plus one line strip per highlight, vertex colours fade the edges, one full-screen gradient, 36 dust points. Vertices are updated on the CPU at 30 Hz; freeze or halve the update while Connecting. About 12 calls and no texture. Page wash, scrim and the Home vignette are full-screen alpha rectangles (1 call each). There is no backdrop blur on the Vita: popups use a darker scrim, or a pre-blurred copy of the last frame.

## 4. Draw-call counts per screen (paper)

The ~80 figure is a paper reference, not a hard limit for menu screens (CEO ruling, 2026-10-09, #271). The test for a menu screen is that it stays smooth on hardware, with `UI/DRAWS` used to spot outliers. The stream overlay stays lean, because it shares the CPU with decode and networking. `UI/DRAWS` counts a text run as 1 while vita2d draws one quad per glyph, so the counter understates real cost (#353).

Recounted bottom-up in round 7b from the real primitives (one draw per texture quad, text run, strip or rectangle). Earlier round estimates undercounted. Default settings (Background Blur None).

| Screen | Draws | Breakdown |
|---|---|---|
| **Home (Consoles)** | **67** (69 with a status message, 70 with the cooldown banner) | wave 12 (5 fill strips + 5 line strips + gradient + dust), vignette 3 (gradient rects), top bar 5 (logo, Wi-Fi, battery icon, percent, clock), categories 6 (4 icons, glow, label), list 22 (4 full rows plus a faded fifth preview, 5 rows x 4 draws: icon, dot, name, status; +1 focus glow; +1 "Internet" label), detail 13 (logo, name, status dot + text, 3 kv rows x 3: label, value, hairline), hint row 6 (Connect, Options, L R Category: 3 x glyph + text) |
| Home, Filter row focused | 55 | same frame 12 + 3 + 5 + 6, list 21 (Filter row: icon, name, status, glow = 4; 4 console rows x 4 + Internet label), detail 4 (title, two text lines, count), hint row 4 (Filter, L R Category; +2 with Clear) |
| Home with Options open | **75** | the retained Home layers still drawn dimmed: wave 12, vignette 3, top bar 5, categories 6, list 22, detail 13 = 61 (dimming is a tint on each draw, no extra call); hint row 4 (Select, Back); Options column 10 (edge gradient 1, name 1, "Options" 1, 4 row labels, focused 3-slice bar 3). Cheap saving if needed: do not draw the detail panel behind the column (-13) |
| Settings (Video group, worst case) | 62 | wave 12, wash 1, top bar 5, title 3, group list 6 (5 labels + current marker), rows 25 (5 rows x 5: label, divider, and a control of 3: toggle = track + knob + On/Off text, choice = 2 chevrons + value), focused 3-slice bar 3, description 1, hint row 6 |
| Profile (Account or Connection) | about 55 | same frame as Settings with 3 to 5 info rows (3 draws each) and the identity block 4 |
| Controller summary | 49 | wave 12, wash 1, top bar 5, title 3, preset switcher 3, diagram 1, 2 callouts x 4 (text, underline, leader, dot) = 8, footers 2, hint row 14 (7 hints) |
| Controller zone view | 70 | wave 12, wash 1, top bar 5, title 3, diagram 1, grid as **one baked grid texture** 1, 18 mapped fills 18, 18 labels 18, footer 1, hint row 10. Drawn naively (a fill and an outline per cell) it would be 87, well above the ~80 reference, so the grid is baked |
| PIN | 62 | wave 12, wash 1, top bar 5, title 3, prompt 1, 8 boxes x 2 (baked box + digit) 16, chevrons 2, 3 pill buttons x 4 (3-slice + label) 12, hint row 10 |
| Connecting | 54 on the Internet flow (44 local ready, 46 standby; +5 with the Network Unstable pill) | wave 12, wash 1, top bar 5, title 3, halo + ring with glow + room icon + spinner arc + logo + name + route 7, steps 2 each for a done or pending step and 4 for the current one (16 on the Internet flow, 6 local ready, 8 standby), Cancel button 8 (it is always focused: label, border 3, focus fill 3, glow), hint row 2. Over the earlier estimate of 51 because the focused button was counted as 4 |
| Reconnecting | 27 | wave 12, wash 1, top bar 5, title 3, halo 1, spinner arc 1, four text lines 4 (no ring, no steps, no button, no hint row) |
| Stream overlay | 26 worst case | exit pill 7 (3-slice, 3 glyphs, text), stats panel 14 (9-slice 9, title, 2 rows x 2), unstable badge 5 |
| Home, Pair new device focused (#330) | 62 to 66 | the item is one list row like any other (icon, name, status, glow = 4, replacing the focused console row, so the list stays 4 full rows plus a faded preview); the detail panel is lighter (title, 2 text lines, 2 kv rows of 3 = about 9, against 13). Hint row 4. A focused console whose row above is the item adds the Up hint: +2 draws (glyph and text) to Home's 66, so 68 |
| Pair new device popup (#330) | **about 59 worst** (12 found, filter active, Filter row focused with the Clear hint), about 53 filter idle, about 37 searching or empty | frozen copy 1, `ui_popup_draw` frame 19 (scrim 1, card 9-slice 17, title 1), instruction 2 lines 2, section label 1 + spinner arc 1 (the count moves into the Filter row), Filter row 11 (glow 1, bar 3, icon 1, text 1, count 1, Clear button 4), 3 console rows x 3 (label, right label, divider) 9, scroll indicator 2, pinned Enter IP row 4, hint row 9 (4 hints, L R Page included). The list is a 4-row scrolling viewport, so the cost does not grow with 10 to 30+ consoles; only rows inside the viewport are drawn, L and R paging adds none. Home behind is one frozen draw. Well under the ~80 reference. "Looking for console" (size S): about 24 |
| Any popup | about 36 | **the screen behind is frozen into one half-resolution copy when the popup opens (1 draw)**, scrim 1, popup body 9-slice 9, title and text 3 to 4, buttons 2 x 4 or list rows (6 x 2 + focused 3-slice bar 3 + scroll 2), hint row 4. Popups are not drawn over the live screen because that would be costly (Home 67 + a 31-draw list popup = 98) |

**As built (#306), stream overlay:** the paper worst case is 28, not 26: exit pill 9 (3-slice 3, 3 text runs, 3 glyphs; the paper's 7 counted the label as one run), stats panel 14, Network Unstable pill 5. Testing builds add the debug resync widget: 10 (9-slice 9, text 1), so 38 there. While streaming the `UI/DRAWS` line reports `screen=overlay`. Not yet measured on hardware.

**As built (#305), draw counts per frame:** Controller Summary page 1 draws 56, page 2 draws 44 to 80 (by how many zones are mapped) and a zone view draws 79. Page 1 and the zone view are over the paper budgets above (49 and 70) by the touch controls of SPEC 4.1 (the back chevron, the small Clear and Whole surface buttons) and, in the zone view, because the grid is a baked grid-lines texture plus a baked state texture per mapped, cursor or picked cell instead of one baked grid. All stay at or under the ~80 reference of Home.

**As built (#340), Controller draw counts.** Summary page 1 adds the read-only front grid: the grid-lines texture plus one fill and one label per mapped or Mixed zone (None zones draw nothing), so 1 to 37 draws. Worst case with all 18 zones mapped, which is also the default mapping (all 18 Touchpad): 97 with Clear focused (the #305 figure 56 + 4 for the focused Clear + 37), 94 with a callout focused. Both rear views gain the "Left" and "Right" labels (+2): Summary page 2 is 46 to 82, the Rear Touch zone view 81. The ~80 figure is a paper reference, not a limit (CEO, 2026-10-09); page 1 is now 14 to 17 above it on paper and page 2 and the zone view 1 to 2 above, which is acceptable for menu screens if they stay smooth on hardware. Hardware `UI/DRAWS` readings are still to be taken.

**As built (#307), touch parity.** The shared back chevron adds 1 draw to Settings and Profile (Controller already counted it). The swipes and the long-press add no draws. This ticket added no instrumentation: the testing build's `UI/DRAWS` line is how each screen is measured on hardware, and the last column below is filled in from it. Phone login + toast was 87; #346 removed step 4 and the Open browser button (-7), so it is 80, at the ~80 reference. The as-built paper counts from #300 to #306:

| Screen | FEASIBILITY paper | As built paper | Measured on hardware |
|---|---|---|---|
| Home Consoles | 67 | 66 | |
| Home + Options | 75 | about 79 | |
| Settings Video worst | 62 | about 69-71 (68-70 + chevron) | |
| Profile Account | 55 | 49 (48 + chevron) | |
| Profile Account + toast | - | 59 | |
| Profile Connection | 55 | about 58 | |
| Profile PSN | - | 49-52 | |
| Profile phone login | - | 79 | |
| Profile phone login + toast | - | 80 | |
| Controller summary | 49 | 56 (page 2: 44-80) | |
| Controller zone view | 70 | 79 | |
| PIN | 62 | 61-63 | |
| Connecting local | 44 | 44 | |
| Connecting standby | 46 | 46 | |
| Connecting Internet | 54 (the earlier figure was 51) | 54 | |
| Reconnecting | 27 | 27 | |
| Re-pair popup | about 36 | 36 | |
| Results popup | about 36 | 32-40 | |
| Connect via popup | about 36 | 39 | |
| Change icon popup | about 36 | 45 | |
| Stream overlay | 26 | 28 (38 in testing builds) | |

Status messages in the detail panel are laid out once when the selection or message changes (`ui_text_face_width` per word), never per frame.

## 5. Texture budget (paper)

| Group | MB |
|---|---|
| Category and group icons, glyph atlas (hint glyphs, status icons) | 0.15 |
| Item icons (12 x 38 px), room-icon atlas, glow textures, Connecting ring | 0.27 |
| PS5 and PS4 logos | 0.05 |
| Controller diagrams at source size (scale down for a saving) | 2.50 |
| Font atlases (6 faces, Light and Regular; two fixed 1 MiB atlases, see section 2) | 2.10 |
| Top-bar logo, misc | 0.10 |
| Rounded shape textures (3-slice, 9-slice, knob, PIN box) | 0.05 |
| Frozen half-resolution background copy for popups (480 x 272 RGBA) | 0.52 |
| **Total** | **5.74, about 5.7** |

Well under the 15 MB budget. The wave costs none.

## 6. Reuse

- `ui_controller_diagram.c` and the controller screen logic: unchanged behaviour (summary pages, zone views, mapping popup, preset slots). Only chrome, colours and the new Triangle entry to the zone view change.
- `ui_focus.c` zones: the focus zone and the modal stack. Nothing in the focus manager assumes the wave sidebar any more (its nav-bar zone and zone crossing were removed in #305).
- `ui_input.c`, the IME dialog, `ui_text.c` atlas, `ui_qr.c`: reused as they are.

## 7. Risks

| Risk | Level | Mitigation |
|---|---|---|
| Static screens, popups, pages, overlay | low | plain rectangles and text |
| Detail-panel status message wrapping (up to 3 lines, so the kv table below shifts 0 to 48 px) | medium-low | wrap once when the selection or message changes (`ui_text_face_width` per word), never per frame; all current messages fit in 2 lines at 304 px; the Home list itself now has fixed row heights |
| Touch swipe and paint gestures (list, categories, pane, popup list, zone paint) | medium | thresholds in SPEC section 4; a touch over 8 px is never also a tap |
| Registration and connection outcomes (finished OK, PIN not accepted, unreachable, timeout) | medium, backend | the UI needs these four results from the registration code (SPEC 3.3), not UI work |
| Wi-Fi, battery and clock reads for the top bar | low | poll about once per second (`sceNetCtl`, `scePower`, RTC) |
| Glow looks right on the Vita OLED | low | the mock uses CSS drop-shadow; confirm the baked texture on hardware |
| All numbers are estimates | n/a | measure Home and Home + Options first; they are under the ~80 reference (67 and 75; Home with Options is the heaviest Home state) |

## 8. Background Blur (user setting: None / Soft / Strong / Dark)

Settings > Display > Background Blur, default **None** (CEO decision, round 7). It applies live and is stored as one integer in the config (SPEC flag 13).

| Mode | Method | Veil | Look |
|---|---|---|---|
| None | today's full-resolution wave | none | sharp ribbons with thin bright highlight lines |
| Soft | wave into a 480 x 272 target, averaged 2:1 to 240 x 136 (1/4), bilinear upscale | 14% dark | nearly identical to None, ribbons slightly softer; still reads as XMB |
| Strong | same chain continued to 120 x 68 and 60 x 34 (1/16), bilinear upscale | 5% white + 14% dark | soft clouds of light, hazy; the ribbons lose their lines |
| Dark | same 60 x 34 target | 20% dark | deep calm gradient with a faint glow; flattest look |

Panels (frosted rectangles behind the content zones) were tried and rejected: they read as cards.

### Cost (paper estimates, not measured)

| | None | Soft | Strong / Dark |
|---|---|---|---|
| Extra texture memory | 0 | 480 x 272 RGBA about 0.52 MB, plus 240 x 136 about 0.13 MB | the same two, plus 120 x 68 about 32 KB and 60 x 34 about 8 KB |
| Draw calls | wave about 12 | about 12 for the wave into the 480 x 272 target, 1 halving, then 1 upscaled quad and 1 veil rect on screen; the main scene's count is unchanged | about 12 for the wave, 3 halvings, then the quad and the veil (Strong adds one more tint rect) |
| Scenes per refresh | 0 | 2 (wave, one halving) | 4 (wave, three halvings) |
| Fill | wave shaded at full resolution (about 5 overlapping strips over 522k pixels) | wave shaded at 1/4 of the screen's pixels, then the halvings (tiny), then one bilinear quad and the veil over 522k pixels | same wave fill, the halvings are tiny, same quad and veil |
| Update rate | 30 Hz CPU vertices | targets refresh when the vertices do (15 to 30 Hz) while the quad is drawn every frame | same; 15 to 30 Hz is invisible at this softness |
| Unknown | none | cost of the extra scenes on GXM (each switch to a render target and back), only on frames where the ribbons update; measure | same, with two more scenes |

Why a bigger base and halvings (#326): the first version drew the wave straight into the 240 x 136 or 60 x 34 target. A GXM target pass has no MSAA, so each target texel took one sample at its centre. That is point sampling, not averaging: the thin highlight lines came out dotted and the ribbon edges stair-stepped, and the bilinear upscale stretched those hard texels into visible blocks on hardware. The mock does not show this because a browser canvas antialiases into its small canvas, so each small pixel is the average of the full-size picture. Drawing at 480 x 272 and averaging each 2 x 2 block (one bilinear draw at half size per step) gives the same kind of average. The costs above are paper estimates, not measured.

The mock's look is still the reference for Soft, Strong and Dark: the Vita now reaches the same averaged small picture by a different route (the 480 x 272 base and the 2:1 halvings), and the only remaining difference is the browser's smoothing kernel against GXM's bilinear. Resolution choice for Strong and Dark was made by trying 1/8 (visible grid artefacts from the 8x upscale), 1/4 then 1/8 (smooth but needs the 1/4 pass anyway) and 1/16 (smooth, one tiny pass): 1/16 ships.

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
