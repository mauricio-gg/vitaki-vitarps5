# VitaRPS5 XMB build spec (issue #271, round 8)

Baseline for engineers. The HTML mock in this folder is the visual reference (`xmb.html`, deep links in section 3); this file is the contract. Where they disagree, fix the mock. Native 960x544, one 8 px grid. Nothing here is built yet.

- Theme source of truth: `tokens-xmb.css` (Ocean, the default) and `tokens-themes.css` (the other four presets). Component CSS: `xmb.css`. Mock logic: `xmb-app.js` (read it for exact behaviour). Canonical screens are lossless 960x544 PNGs in `screens/` (index.html), the device preview is the only JPG.
- vita2d cost numbers: `FEASIBILITY.md`. About 80 draw calls is a paper reference, not a hard limit for menu screens (CEO ruling, 2026-10-09, #271): a menu screen passes when it stays smooth on hardware, with `UI/DRAWS` used to spot outliers. Do not drop labels or clarity to lower the draw count (same ruling). The stream overlay stays lean because it shares the CPU with decode and networking. The UI/DRAWS counter counts a text run as 1 while vita2d draws one quad per glyph (#353).
- Scope decisions applied: Logs category removed; Add item removed in round 8, **reversed by #330 as the `+ Pair new device` item (CEO-approved 2026-10-09, section 3.1a)**; in-stream has no menu (today's overlay only); triangle options per console; one result/error popup; re-pair asks first; everything works by touch; hint row, on by default (Show Button Hints); the alert slot stays when hidden; Settings grouped; "Show Navigation Labels" dropped; no time-of-day wave tint (one fixed palette); Profile has no streaming metrics (stream stats live only in the overlay).

---

## 1. Theme and tokens

One header (`ui_theme.h`). **Rule:** tokens cover colours (alpha included), type faces and sizes, the spacing scale, line widths, durations and easing. Geometry that belongs to one component (its widths, heights, offsets, hit boxes) is a named constant (`UI_<COMPONENT>_*`) listed in that component's section of this file; screen code uses those constants and never a raw number or colour.

### 1.1 Colour

| Token | Value | Used for | Replaces (today) |
|---|---|---|---|
| `TEXT` | #FFFFFF | focused text, values, titles, glyph tint | `UI_COLOR_TEXT_PRIMARY` |
| `TEXT_2` | #DDE3F0 | labels, unfocused rows, hint row labels, body copy | `UI_COLOR_TEXT_SECONDARY` |
| `TEXT_3` | #B3BDD2 | captions, disabled labels, empty text, **Unavailable status** (dot and label) | `UI_COLOR_TEXT_TERTIARY` |
| `OK` | #4EE09A | Ready dot, Connecting ring, success icon, authenticated, loss "Stable" | `UI_STATUS_ACTIVE`, green literals |
| `WARN` | #FFBE4D | Standby, cooldown banner rule, armed log out, loss amber | amber literals |
| `ERR` | #FF8080 | Error status, error text, Network Unstable dot | `UI_STATUS_ERROR`, `RGBA8(0xF4,0x43,0x36)` |
| `INTERNET` | #A9B2FF | "Internet" route label | new |
| `IDLE` | #C9D0DF | Unpaired dot | grey literals |
| `ACCENT` | #6DB4FF | mapped touch zones only (fill at 28%, border at 60%) | `UI_COLOR_PRIMARY_BLUE` (#3490FF), removed everywhere else |
| `PANEL` | rgba(6,12,28,.92) | popups, toast, options column ramp, stats panel | `UI_COLOR_CARD_BG` |
| `SCRIM` | rgba(2,5,14,.55) | behind popups | popup dim literals |
| `SCRIM_STRONG` | rgba(2,5,14,.82) | system keyboard stand-in only (not drawn by the app) | n/a |
| `HUD` | rgba(6,12,28,.60) (the theme's deep colour; Ocean shown) | overlay pill and badges | `RGBA8(0,0,0,180/200)` |
| `PAGE_WASH` | rgba(3,6,16,.60) | dims the wave behind pages, Controller, PIN, Connecting | new |
| `FILL_FOCUS` | white @ 12% | focused row, focused button | blue focus rings |
| `FILL_ON` | white @ 26% | toggle on, selected zone, cursor zone | blue fills |
| `LINE` | white @ 26% | page rule, popup and toast border, button border | grey 1 px lines |
| `LINE_FAINT` | white @ 14% | row dividers, kv rows | grey 1 px lines |
| `BG_*` | fixed wave palette below | background | `UI_COLOR_BACKGROUND`, particles |

Effect colours (every other alpha-bearing colour the components use, all in `tokens-xmb.css`):

| Token | Value | Used for |
|---|---|---|
| `GLOW` | white @ 70%, radius 12 | focus glow on text and icons (baked texture) |
| `GLOW_SOFT` | white @ 35% | type-logo glow, diagram halo |
| `GLOW_INNER` | white @ 45% | inner glow of picked and cursor zones |
| `SHADOW` | black @ 55% | text and icon drop shadow |
| `LEADER` | white @ 55% | callout leader lines and underlines |
| `ZONE_LINE`, `ZONE_MAPPED_LINE` | white @ 22%, #A0CDFF @ 60% | idle and mapped zone borders |
| `RING_FILL` | white @ 10% | Connecting ring inner wash |
| `HALO` | #82AAFF @ 28% | Connecting halo |
| `QR_PLATE`, `QR_INK`, `QR_HIDDEN` | #FAFAFA, #0A0A0A, #1A1A1A @ 55% | QR panel |
| `PANEL_EDGE`, `EDGE_0` | rgba(4,8,20) @ 92% / 0% | Options column feathered edge |
| `VIG_*` | black/navy @ 32-55% | Home vignette (gradients) |
| `GLASS_VEIL` | #02050E @ 14% | Background Blur Soft veil; also the dark part of Strong |
| `GLASS_FROST` | white @ 5% | Background Blur Strong, drawn under the 14% veil |
| `GLASS_VEIL_DARK` | #02050E @ 20% | Background Blur Dark (no white) |

Alpha levels in use (the whole set): 5, 10, 12, 14, 20, 22, 26, 28, 32, 34, 35, 38, 40, 45, 50, 55, 60, 70, 82, 92 percent, always via a token above.

Wave palette (one fixed set, no time of day): top #06204A, mid #0F4585, bottom #245F9C, ribbons #96CDFF / #64B4F0 / #BEDCFF, horizon glow rgba(160,210,255,.28). Tokens `--wave-*`; geometry and speeds in `xmb-wave.js`.

124 hard-coded RGBA8 literals in today's code all collapse into the table above.

### 1.2 Type

Roboto only, pre-rendered. **6 faces from 2 loaded weights, Light and Regular, no mono face** (as built in #308: Regular 14 and 16, Light 20, 28 and 40; #329 added Regular 20 as the sixth face, `UI_FACE_T20_REGULAR`; before the redesign, 7 sizes in 2 families). Roboto's own digits are equal width, so IPs, countdowns and stats still line up without OpenType features (FreeType would not apply them anyway).

| Face | Size / line | Weight | Used for | Replaces |
|---|---|---|---|---|
| `T40` | 40 / 48 | Light 300 | PIN digits | `PIN_DIGIT` 40 (was Roboto Mono) |
| `T28` | 28 / 32 | Light 300 | page titles, popup title, info-panel title, Connecting current stage, focused group, Reconnecting retry bitrate | `FONT_SIZE_HEADER` 28, `HOME_HEADER` 24 |
| `T20` | 20 / 24 | Light 300 | setting label and value, buttons, popup rows, callouts, prompts | `CARD_TITLE` 20, `SUBHEADER` 18 |
| `T20_REGULAR` | 20 / 24 | Regular 400 | **focused list item name** (added in #329; T20 metrics, so rows do not move) | T20 Light, which the focused name used before |
| `T16` | 16 / 24 | Regular 400 | **unfocused list item name**, focused item status line, captions, status messages, descriptions, kv rows and values (IDs, IPs, codes), stats values, pills, toast sub | `BODY` 16, `SMALL` 14, Roboto Mono 16 |
| `T14` | 14 / 20 | Regular 400 | hint row labels, unfocused item status line | `SMALL` 14 (already in today's atlas, no new size) |

Roboto Light is loaded by the app (before the redesign only Regular and Mono were). Decision, now built: load it (one TTF, 170 KB, about 0.3 MB of atlas for 3 sizes); Roboto Mono is dropped, which removes its atlas. List item titles use only existing faces (round 8): focused name T20 (29% smaller than the old T28; Light when built in round 8, Regular 400 since #329), unfocused name T16 Regular (20% smaller than the old T20), status line T14 Regular unfocused and T16 Regular focused. No atlas growth (#329 then added a sixth face, `T20_REGULAR`, Regular 400 at 20 / 24, for the focused list item name only; it adds no atlas memory either, see FEASIBILITY.md section 2). Roboto Medium appears only inside the baked Start glyph, never as a text face.

### 1.3 Spacing, layout, lines, motion

| Token | Value |
|---|---|
| Grid | 8 px. Scale `S1..S6` = 8, 16, 24, 32, 40, 48 |
| `MARGIN_X` | 48 (left and right of every page, top bar, hint row) |
| `TOP_Y` | 16 (top bar, height 32) |
| `TITLE_Y` | 64 (page title row, height 48) |
| `RULE_Y` | 120 (page divider) |
| `BODY_Y` | 136 (page content top) |
| `HINT_Y` | 496 (hint row, height 48; text T14, face symbols 16 px, other glyphs 20 px) |
| `ROW_H` | 48 (setting row, popup list row); 56 for group, options and list-row base; 64 icon box |
| `TAP_MIN` | 48 (every tap target is at least 48 x 48; chevrons are 48 x 48 boxes around 16 px art) |
| `LW1`, `LW2` | 1 px (hairlines, borders), 2 px (ring, focus underline, toggle, spinner) |
| Radius | three tokens: `R_SM` 8 (selection bars on rows, option and popup list rows, icon cells, QR plate, PIN boxes), `R_MD` 16 (popups, stats panel, keyboard stand-in box), `R_PILL` = half the height (buttons, toggles, toast, pills and badges, hint glyph badges). Zone cells, callout underlines, hairlines and the wave stay square. On the Vita every rounded shape is a baked white texture tinted at draw time, never a per-pixel-radius fill. **One rule:** a shape of fixed height (focus bars 48 and 56, buttons 48, toast 48, pills 32, toggle track 24) is a **3-slice** baked at its own height (8 or half-height caps, stretched middle: 3 draws); a shape with a large or variable height (popups, icon cells, QR plate) is a **9-slice** (9 draws). FEASIBILITY.md budgets both |
| `D1`, `D2`, `D3` | 150, 300, 600 ms. Easing `cubic-bezier(.22,.7,.2,1)` (ease-out), linear for spinners. Toggle knob keeps today's 180 ms. List cascade: row i starts after 45 ms x min(i,6). Spinner 1.8 s/turn (ring) and 0.8 s (inline 16 px). |
| Overlay timers | exit hint 5.0 s visible + 0.5 s fade (today's values); unstable badge 5.0 s (today); toast 3.0 s + 0.3 s fade (today 2.0 s, see Flags); log out confirm window 3.0 s (today) |

Today's tokens that go away: `PRIMARY_BLUE` and every blue focus ring, `card_with_shadow` (all shadows), rounded radii 6/8/10/12, 6 modal sizes, `NAV_*` constants (side nav deleted), particles (replaced by the wave).

---

## 2. Component catalogue

### 2.0 Two kinds of component

**Interactive components** hold state and receive input. Each is a plain C struct plus three functions. No inheritance, no vtables, no per-frame allocation.

```
typedef struct { ...data..., ...state..., UiRect visible, hit; } UiThing;
void ui_thing_init(UiThing*, const UiThingSpec*);        // set data, compute rects once
void ui_thing_draw(const UiThing*, const UiTheme*);      // no state change, no allocation
UiEvent ui_thing_input(UiThing*, const UiInput*);        // NONE / MOVED / ACTIVATED / CANCELLED
```

**Display-only components** are plain parameterised draw helpers, `ui_draw_thing(x, y, ...args)`. No struct, no state of their own (a helper may keep one file-static timer, see Toast and Background).

| Kind | Components |
|---|---|
| Interactive (struct + init/draw/input) | C01 CategoryBar, C02 XmbList, C05 OptionsColumn, C07 GroupList (the left list of a page), C08 SettingRow list (with C09 Toggle and C10 ChoiceValue drawn inside), C11 Popup (one struct; C12 ListPopup, C13 ConfirmPopup, C14 ResultPopup are configurations of it), C17 PinField, C21 ZoneGrid, C22 TextButton |
| Display-only (draw helper) | C03 ConnectingRing, C04 DetailPanel, C06 HintRow (draw + hit-test helper, items come from the screen), C07 page frame (title, rule, wash), C15 Toast (one static timer), C16 Spinner and ProgressSteps, C18 QrPanel (the screen owns the tap rect), C19 Pill, C20 ControllerDiagram and Callout (callout tap rect owned by the Controller screen), C23 TopBar, C24 ScrollIndicator, C25 StatsPanel, C26 EmptyState, C27 Background (one static ribbon state) |

`UiInput` is one per-frame snapshot: logical buttons `pressed/down/released` (already swapped by Circle Button Confirm, section 4), D-pad repeat, and touch `{x, y, down, pressed, released, dragged, dx, dy}` in screen pixels. Screens own their components as struct members and forward input to the focused one; draw order is the screen's job.

**Visible rect vs hit rect.** Interactive components store both at init. The hit rect is the visible rect grown to at least 48 x 48 (centred); drawing uses `visible`, hit tests use `hit`. Constants live in the component's section (`UI_<COMPONENT>_HIT_*`). Hit rects may overlap neighbouring visuals. **Overlap priority**, first match wins: popup, then Options column, then hint row, then the screen's small controls (buttons, chevrons, callouts, filter clear, chips), then rows and cells, then big backgrounds (diagram, list viewport, pane). On Home a swipe starts anywhere on the screen and locks to one axis (horizontal moves the category, vertical moves the list; #331). Every other swipe starts only on its own zone: a page pane or a list popup. A touch that moves more than 8 px is a swipe or a paint and never also a tap.

Text is drawn from the 6 pre-rendered faces (Light 20, 28, 40; Regular 14, 16, 20); focus glow and rings are baked textures tinted at draw time (FEASIBILITY.md).

**Glow rule.** Every baked glow texture is padded by at least its glow radius on all sides (texture size = art size + 2 x radius, with a fully transparent border). A glow is its own padded texture, centred on the art, drawn **before** the art, and never clipped to the art's box or to the row's rect. Scaling or animating the art must not scale or clip the glow's texture. List viewports and scissor rects leave room for the glow of the focused item (the Home list viewport starts 24 px above the focus row and 32 px left of the icon column for exactly this reason). Example: the focused list item's icon (38 px art in a 64 px box) gets a 112 x 112 white glow texture centred on it. The mock's earlier square cut-off came from putting a `box-shadow` inside a scaled box, where the compositor clipped it to that box.

### C01 CategoryBar (interactive)
| | |
|---|---|
| Purpose | Horizontal Consoles / Settings / Controller / Profile row. Replaces the wave sidebar. |
| Constants | `UI_CAT_BOX` 64 (art 48), `UI_CAT_Y` 104 (centre), `UI_CAT_X0` 256, `UI_CAT_FAR` 128 (first right neighbour), `UI_CAT_STEP` 112, `UI_CAT_SCALE_ON` 1.25, `UI_CAT_SCALE_LEFT` 0.75, `UI_CAT_SWIPE_PX` 96 |
| Anatomy | Focused centre x 256, scale 1.25, glow, label T16 centred 16 below the box (y 152-176). Right neighbours at x 384, 496, 608. Left neighbours at 256 - 112 per step, scale 0.75. Unfocused opacity 62%. Icons: `icon_play.png`, `icon_settings.png`, `icons/controller.svg`, `icons/profile.svg`. |
| States | focused, unfocused, dimmed to 25% while Options column is open (as C05 and the mock) |
| Input | Left/Right or L/R changes category (slide 300 ms). Touch: tap an icon focuses it (hit 64 x 64); a horizontal swipe from anywhere on Home moves one category per 96 px. |
| Used by | Home |
| Replaces | `ui_navigation.c` wave sidebar and `NAV_*` |

**As built (#307, zone changed by #331).** The strip swipe is `ui_category_bar.c`: a swipe that locks to the horizontal axis (see the #331 note below), wherever it started, changes the category by one per `UI_CAT_SWIPE_PX` (96) from touch-down, swipe left = next category, clamped at both ends, with the existing 300 ms slide. The step count comes from `ui_gesture_swipe_steps()` in the pure `ui_gesture.c`. A swipe is never a tap, so lifting the finger after one does not focus an icon. It does not run while the Options column or a popup is open.

**As built (#331).** The swipe has no start zone on Home. `ui_gesture_swipe_axis()` (pure, `ui_gesture.c`) picks the axis once, on the frame the touch first moves past 8 px: horizontal when |dx| > |dy|, otherwise vertical. `ui_input.c` keeps it until the finger lifts and exposes it as `UiTouch.swipe_axis`, so one touch moves the category or the list, never both. The old y 64..176 strip zone (`UI_CAT_STRIP_Y`, `UI_CAT_STRIP_H`) is gone. A swipe that starts on a hint, a button, an icon or a row is still a swipe and does not fire that target. Settings, Profile and the popups keep their own zone-bound swipes.

**Retuned (#331 follow-up, 2026-10-09).** Hardware feedback said each swipe step needed too little finger travel (starting a swipe was fine). Both Home steps went from 56 and 64 px to 96 px (about 11 mm instead of 6.5 and 7.5 mm), in `UI_CAT_SWIPE_PX` and the new `UI_LIST_SWIPE_PX`. The 8 px start distance and the axis lock did not change. Settings, Profile and popup swipes stay at 48 px.

### C02 XmbList + ListRow (interactive)
| | |
|---|---|
| Purpose | Vertical item column under the focused category. Row status colours (dot and label only): Ready OK, Standby and Retrying and Please wait WARN, Error ERR, **Unavailable neutral TEXT_3** (Unpaired is not a Home state, #330). No rings, no badges. |
| Constants | `UI_LIST_X` 224, `UI_LIST_Y` 160, `UI_LIST_W` 368, `UI_LIST_H` 336 (bottom 48 fades), `UI_ITEM_ICON` **38** (flat white art, about 21% smaller than the 48 px category icons), `UI_LIST_ICON_BOX` 64 (centre x 256, aligned under the category icon), `UI_LIST_TEXT_X` 304, `UI_LIST_TEXT_W` 288, `UI_LIST_FOCUS_Y` 192, `UI_LIST_ROW_H` 64, `UI_LIST_SWIPE_PX` 96 (swipe step, not the row height), `UI_LIST_FOCUS_GAP` 16 (extra space after the focused row), `UI_LIST_SLIDE` 64, `UI_LIST_GLOW` 112 (glow texture) |
| Anatomy | Row (64 high, icon box and text block vertically centred): icon art 38 in the 64 box, text at x 304. **Focused**: name `T20_REGULAR` (Regular 400, 20 / 24, same metrics as T20) white with a soft text glow behind it (`UI_LIST_TITLE_GLOW` pad 12, `UI_LIST_TITLE_GLOW_PCT` 20%, one draw of the shared glow texture around the name's measured box, faded with the row like the icon glow), status line T16. **Unfocused**: name T16 Regular TEXT_2, status line T14. Focus never scales the icon; it adds the glow (white, 112 texture, 50%) and a 15% brightness lift. Rows pitch 64 (focused row followed by 16 more); rows above the focus slide up 64 px per step and fade to 0. Cascade-in on category change. Room icons of Unavailable and Cooldown rows draw at 55% opacity. |
| Filter item | In the Consoles category, **when there are more than 4 consoles or a filter is active**, the first row is "Filter..." with a magnifier icon (neutral, no status). Focus still starts on the first console on launch and on category change (Cross connects straight away); Up once reaches Filter. Idle: name "Filter...", empty status line. Active: name `Filter: "den"`, status line `1 found · [Square] to clear`. Cross or tap opens the system keyboard (title "Filter Consoles", current text prefilled; empty and Done clears). Start is a shortcut from anywhere in the Consoles list (opens the keyboard, or clears an active filter); Square on the focused active Filter row clears. It matches the console name and the IP address. When no console matches the list shows the Filter row plus "No consoles match filter". |
| States | focused, unfocused, dimmed (opacity 55%, cooldown), hidden (above focus) |
| Input | Up/Down moves (no wrap). Confirm or tap on the focused row activates; a tap on the Filter row opens the keyboard at once. Tap on another row focuses it, second tap activates. **Long-press (0.5 s) on a console row opens its Options** (touch path for Triangle). Vertical swipe from anywhere on Home: one row per 96 px (`UI_LIST_SWIPE_PX`), focus follows. Hit = the row, 64 high. |
| Used by | Home (consoles, setting groups, presets, profile groups) |
| Item icons | Settings groups: Video (film frame), Network (Wi-Fi arcs), Display (sun), Appearance (`icons/appearance.svg`), Controls (button cluster), Advanced (sliders). Profile groups: Account (ID card), Connection (two linked nodes), PlayStation Network (cloud). Controller presets: Custom 1, 2, 3 (a row of three slots with 1, 2 or 3 filled). Consoles: the room icon (tv, sofa, bed, bunk, desk, house). Filter: magnifier. All 38 px flat white SVGs in `icons/`, same weight as the room icons. The Settings and Profile **page** group lists (the left column of the pages) are text-only and stay so. |
| Replaces | console card list (`ui_console_cards.c`), 4 duplicated card+focus-ring pieces |

**As built (#329).** The focused row's name draws in `UI_FACE_T20_REGULAR` with one glow draw behind it: `ui_glow_draw_rect()` around a box at `UI_LIST_TEXT_X`, the name's top, its measured width (`ui_text_face_width()`, no allocation) and the 24 px line, pad `UI_LIST_TITLE_GLOW`, colour `UI_GLOW` at `UI_LIST_TITLE_GLOW_PCT`, scaled by the same `k * glow_k` factor as the icon glow so it fades with the row. The Home list is not drawn inside a scissor, so the padded box (x 292 up) is never clipped. A Home frame with a focused row gains one draw (41 to 59 `UI/DRAWS` before; paper reference about 80). The Filter row's ellipsis is measured in the new face.

**As built (#307).** The list swipe (`ui_xmb_list.c`) moves focus one row per `UI_LIST_SWIPE_PX` (96) from touch-down, finger up = next row, clamped, with the normal slide. Since #331 it starts anywhere on the screen and counts only when the touch locked to the vertical axis; a horizontal swipe belongs to the category strip (C01), which is how the two swipes do not fight. The long-press (`UI_LONG_PRESS_MS` 500, finger within `UI_TOUCH_DRAG_PX` 8 of touch-down) on a console row focuses that row, opens its Options column exactly as Triangle does (same cooldown rule) and consumes the touch, so the release neither connects nor closes the column. It does nothing on the Filter row, on the empty state, or while Options or a popup is open, and it has no visible affordance (the Triangle hint stays). A swipe is never a tap or a long-press, and neither swipe runs while Options or a popup is open.

### C03 ConnectingRing (display-only)
| | |
|---|---|
| Purpose | The big ring around the room icon on the Connecting screen, a progress indicator (list rows no longer have rings). `ui_draw_connecting_ring(x, y, size, room, state)`. |
| Constants | `UI_RING_W` 2, `UI_RING_SIZE` 128, `UI_RING_ICON` 50% of size, spinner 176, halo 280 |
| Anatomy | 2 px ring in the state colour (OK, WARN, ERR), soft glow 12 px in the state colour, room icon at 50% of the size, no badge. Glow built to the Glow rule (padded texture). |
| States | one ring texture, tinted per state colour |
| Used by | Connecting |
| Cost | 1 ring texture + 1 halo, 2 draws |

### C04 DetailPanel (display-only)
| | |
|---|---|
| Purpose | Facts about the focused item, no card. |
| Constants | `UI_DETAIL_X` 608, `UI_DETAIL_Y` 192, `UI_DETAIL_W` 304, `UI_DETAIL_LOGO_H` 48 (PS5 176 wide, PS4 200 wide), `UI_DETAIL_KV_H` 32 |
| Anatomy | Console: type logo (`PS5_logo.png`, `ps4.png` crop) white, 16 below it the name T28, status line T16 (dot + label), then, only when the console has one, the **status message** (T16, 24 line, wraps up to 3 lines in the 304 px panel; `ERR` colour for Error, `WARN` for Retrying, 16 gap below), then the kv rows (label T16 TEXT, value T16 right, `LINE_FAINT` hairline): Address, Route, Pairing. Settings group: the group's rows and values. Preset: description line, then L1, R1, Front touch, Rear touch. Profile group: its first 4 rows. |
| States | per item kind; rises 300 ms when focus changes |
| Replaces | the in-card IP/status text |

### C05 OptionsColumn (interactive)
| | |
|---|---|
| Purpose | Triangle options for a console. |
| Constants | `UI_OPTS_X` 608 (w 352 to the right edge), `UI_OPTS_PAD` 48, `UI_OPTS_ROW_H` 56, first row y 160 |
| Anatomy | Slides in from the right 300 ms, left edge feathered into `PANEL_EDGE`. Console name T28 at y 88, "Options" T16 at y 120, rows T20 with 1 px `LINE_FAINT` divider. Focused row: TEXT on an `R_SM` `FILL_FOCUS` bar, glow on the label. Home dims behind it. |
| Items | Connect (Wake and connect on standby) / Connect via (only when Local and Internet both exist) / Re-pair / Change icon. Unpaired consoles are no longer in the Home list, so they have no Options (#330). Cooldown disables Connect and Connect via. |
| Input | Triangle opens, Triangle or Circle closes. Up/Down, Confirm. Touch: tap a row (hit 352 x 56); every tap outside the column, including the hint-row buttons, closes it and is consumed (nothing else runs). While open, the category bar, list, detail panel and top bar dim to 25%; the column and the hint row stay bright. |

### C06 HintRow (display-only, with hit test)
| | |
|---|---|
| Purpose | Persistent bottom row. Replaces the Select toast. |
| Constants | `UI_HINT_Y` 496, `UI_HINT_H` 48, gap 24, glyph h 20 (L, R, Start, D-pad) and `UI_FACE_GLYPH_H` 16 (Cross, Circle, Square, Triangle), glyph-label gap 8, `UI_HINT_ALERT_W` 200 (right slot) |
| Anatomy | x 48..912. Hints are **centred horizontally** (on the screen when no alert pill shows; in the space left of the alert slot when it does), gap 24. Each hint: glyph + 8 + label T14 TEXT_2. Glyphs: the 4 PNG symbols (baked 16 px high, drawn 1:1, never scaled) plus baked flat glyphs for D-pad (all, left-right, up-down), L, R, L+R, Start (new assets, 20 px high; L, R, Start are `R_PILL` badges). The row keeps its 48 px height as the tap target; only the visible content shrank (from T16 and 24 px glyphs). |
| Confirm swap | Hints are declared as `CONFIRM` / `CANCEL`; the row resolves them to Cross or Circle from the setting. |
| Alert slot and collapse rule | When Network Unstable is active on a menu, the right 200 px (x 712..912) are reserved for the alert pill and never overlap hints. Hints use x 48..696. If they do not fit, items flagged **low priority** are dropped from the right until they do (low priority: L R Category on Home, Clear and Preset on the Controller summary, Clear digit on PIN). Confirm, Cancel and the main action are never dropped. |
| Popups | While a popup is open the row shows the popup's hints. The Confirm hint carries the label of the **focused button** (for example "Re-pair", "Try again"); Cancel shows the popup's cancel label. A one-button popup shows Confirm only. If the focused button is the cancel button, only Confirm shows. |
| Visibility | The setting **Show Button Hints** (Settings > Display, toggle, default On) shows or hides the whole row on every menu screen and popup. When Off the row draws nothing but its alert slot (the Network Unstable pill stays); the body does not reflow, the space is left empty. The in-stream exit hint is a separate overlay with its own setting and is unaffected. Every action a hint tap could trigger has another touch path (section 4, touch parity). |
| States | normal, dim (45%, action not available) |
| Input | Touch: tapping a hint triggers the same logical action (hit = hint width plus half the gap on each side, so 24 more than the hint, x 48; neighbouring hits meet in the middle of the gap), except while the Options column is open, where any tap outside the column only closes it. A popup owns the hint row: its hints act on the popup. The Account description under a toast is hidden while a toast shows. |
| Replaces | Select hints toast, per-screen hint strings, `NAV_TOAST_*` |

**As built (#347).** The Cross, Circle, Square and Triangle symbols are 16 px high (were 20 px; 20% smaller), in the hint row, in inline text (Phone Login Assist steps, the armed Log out label) and in the Home Filter row's status line. L, R, Start and the D-pad glyphs stay 20 px. The four PNGs were re-baked at 16 px high (area-averaged from the old 28 to 32 px art) and are drawn 1:1, because a runtime scale of about 0.57 would skip source pixels and give uneven strokes. Glyph width follows the PNG (16, 16, 16 and 18 px). The hint row's tap rects grew by half the gap (12 px) on each side, so the narrower glyphs do not shrink the tap targets; where glyphs and labels draw is unchanged.

### C07 PageShell (frame is display-only, GroupList is interactive)
| | |
|---|---|
| Purpose | Common frame for Settings, Profile, Controller, PIN, Connecting, Reconnecting. |
| Constants | `UI_PAGE_ICON` 32 at x 48, `UI_PAGE_TITLE_X` 96, title row y 64 h 48, rule y 120, `UI_PAGE_GROUP_X` 48 w 256 row h 56, `UI_PAGE_PANE_X` 336 w 576 h 288 (6 rows), `UI_PAGE_DESC_Y` 440 h 48, `UI_PAGE_SCROLL_X` 920 |
| Anatomy | Top bar (C23) always. **Every page title has a 32 px icon at x 48 and the title at x 96**: Settings gear, Profile person, Controller pad, PIN lock, Connecting LAN / globe / moon (by flow), Reconnecting Wi-Fi. Title T28. `LINE` rule y 120. Wave dimmed by `PAGE_WASH`. Optional right slot in the title row (Controller preset switcher). Settings/Profile body: GroupList (rows h 56, T20; focused-active group T28 + glow on an `R_SM` `FILL_FOCUS` bar; current group has a 2 px white bar at its left edge), pane, description line (T16, max 2 lines), scroll indicator (C24). |
| Input | Up/Down, Left/Right moves between group list and pane; L/R switches group; Circle returns (pane to groups, groups to Home). Touch: tap a group, tap a row, swipe the pane (one row per 48 px). |

**As built (#307).** The back chevron is part of the shared page frame: `ui_page_frame_back_draw()` draws it (`UI_PAGE_BACK_ART` 24, stroke `UI_PAGE_BACK_STROKE` 2.0, TEXT_2, one draw) and `ui_page_frame_back_tapped()` is the 48 x 48 hit at x 0, y `UI_TITLE_Y`. It is drawn on Settings, Profile and Controller whether the hint row is on or off. The page decides what back means. Settings: a tap goes back to Home as Circle does from the group list, whichever column has focus. Profile: a tap goes to Home like Cancel (log-out disarmed), also while the phone-login pane shows (the login keeps running), and is ignored while the system keyboard is open. Controller: unchanged behaviour, now drawing the shared chevron instead of its own. The constants were `UI_CTRL_BACK_*` and are now `UI_PAGE_BACK_*`.

### C08 SettingRow (interactive; C09 Toggle, C10 ChoiceValue are drawn inside it)
| | |
|---|---|
| Purpose | One row: label left, control or value right. Kinds: toggle, choice, info, action. |
| Constants | `UI_ROW_H` 48, `UI_ROW_PAD` 16, `UI_TOGGLE_W` 48, `UI_TOGGLE_H` 24, `UI_TOGGLE_KNOB` 16 (inset 2, travel 24, 180 ms), `UI_CHOICE_VALUE_W` 224, `UI_CHOICE_ARROW` 48 x 48 hit around a 16 px icon |
| Anatomy | Label T20, `LINE_FAINT` divider. Focused (only while the pane has focus): `R_SM` `FILL_FOCUS` bar (divider hidden), TEXT, glow on the label. While the group list has focus, no pane row is focused-looking; exactly one focus bar is on screen at a time. Disabled: 50%. Armed (log out): label and value in `WARN`. Error status row: `ERR` value with a 20 px warning icon in front and a 2 px `ERR` rule at the row's left edge. Toggle: **pill** track (`R_PILL`, 48 x 24, 2 px border; off TEXT_2, on white border + `FILL_ON`) with a **round** 16 px knob, "On"/"Off" T16 16 right of the track. Choice: chevrons around a 224 wide centred value T20. Info: value T20 or T16. Action: label, chevron-right when enabled. |
| States | normal, focused, pressed (FILL_ON 150 ms), disabled, armed |
| Input | **First tap acts and takes focus**: toggle flips, choice cycles to the next value, action runs; info rows only take focus. Buttons: Confirm flips/cycles/runs; Left/Right changes a choice. Tap on a chevron steps that way. |
| Replaces | settings list rows, `toggle_switch` (restyled), `dropdown`, Profile text rows |

**As built (#328).** The page tells the two components who has focus every frame (`UiGroupList.focused`, `UiSettingList.active`). `ui_group_list_draw()` puts the baked `UI_SHAPE3_BAR_56` in `FILL_FOCUS` behind the current group's row while the list has focus. `ui_setting_list_draw()` draws the bar, the pressed tint, the glow and the TEXT colours only while `active`; otherwise the remembered row looks like any other, and `focus` and `scroll` are kept so the bar returns to it. The bar is 3 draws in either place and the glow is 1, so they move one for one between the columns; the only difference is that with the list focused the pane's remembered row is drawn unfocused and gains its LINE_FAINT divider, so that frame costs one draw more.

### C11 Popup (interactive), sizes
| | |
|---|---|
| Purpose | The only modal frame. 3 fixed sizes, all 480 wide, x 240. |
| Sizes | **S** 480 x 256 at y 144 (confirm, result, 2-row list). **M** 480 x 352 at y 96 (icon picker grid). **L** 480 x 432 at y 56 (mapping list, 6 visible rows). |
| Constants | `UI_POPUP_W` 480, `UI_POPUP_PAD` 32, `UI_POPUP_S_H` 256, `_M_H` 352, `_L_H` 432, button bar h 48 gap 16, max button w 208 |
| Anatomy | `PANEL` fill, 1 px `LINE` border, `R_MD` corners, padding 32. Title T28 (optional 32 px icon, gap 16), subtitle T16, body T16 (8 below). `SCRIM` behind; the screen behind the popup is frozen into one half-resolution copy when it opens (1 draw, FEASIBILITY.md 4), so it no longer animates while a popup is open. S popup with a list: the rows are vertically centred in the space under the subtitle. Enter: rise 16 px + fade, 300 ms. |
| Input | Modal. Circle or tap on the scrim cancels. Hit for rows 48 high. |
| Replaces | 6 modal sizes (520x280, 560x290, 400x160, 360x340, 700x450, 640x360/380) |

### C12 ListPopup (configuration of C11)
Rows h 48 (T20, optional right label T16 TEXT_3, check icon for the current value), `LINE_FAINT` dividers, focused row an `R_SM` `FILL_FOCUS` bar. Scrolls when rows exceed the visible count (L shows 6), selected row kept near the middle, 2 px scroll indicator at the right edge. Vertical swipe on the list: one row per 48 px, focus follows. **Grid variant** (icon picker): 3 x 2 cells 128 x 104, gap 16, icon 48 (70% opacity), label T16. Focus and current are independent; matrix:

| | Not current | Current |
|---|---|---|
| Not focused | icon 70%, label TEXT_2, no marker | icon 70%, label TEXT_2, quiet check (16 px, `TEXT_3`) at the cell's top-right |
| Focused | radial `FILL_FOCUS`, icon 120% + glow, label TEXT | same plus the check in TEXT |

Input: Up/Down (grid: all four), Confirm, tap a row (selects and activates). Used by: Mapping popup (L), Connect via (S, 2 rows), Change icon (M grid).

**As built (#303).** The focused grid cell is a baked `R_SM` rounded rect in `FILL_FOCUS` (one draw), as the mock's final CSS draws it (`.lg.sel` under `r-sm`), not the radial fill the matrix text names. The check is the 32 px popup check art scaled to 20 px (list) and 16 px (grid cell). The scroll indicator sits on the list's right edge inside the popup padding, as in the mock. The M grid is 8 px taller than the space under the subtitle (as in the mock) and uses the popup's bottom padding. A swipe applies to lists only. The list keeps the focused row at the middle of the viewport as `focus - visible / 2 + 1`, clamped, without the mock's slide.

### C13 ConfirmPopup (configuration of C11)
Size S, title with icon, 1 to 3 lines body, two TextButtons (Cancel left, action right). Default focus Cancel. Left/Right switches, Confirm presses the focused button, Circle cancels. Used by: Re-pair.

### C14 ResultPopup (configuration of C11)
Size S, tone icon (OK check or ERR warning, 32), title, 1 to 3 lines body, one or two TextButtons. Default focus on the right-most (primary) button. One component for pairing success, pairing failure (three reasons, section 3.3) and connection failure. Used by: pairing result, connection failure. Replaces: the debug-only error popup.

### C15 Toast (display-only, one static timer)
`UI_TOAST_Y` 432, h 48, centred, `R_PILL` corners, padding 24, optional 24 px icon + 16 gap, T20, `PANEL` fill, 1 px `LINE`, max w 720, single line. Rise 300 ms, 3.0 s, fade 300 ms. One at a time (a new one replaces). Used by: Profile messages. Replaces: 4 pill/toast implementations.

### C16 Spinner and ProgressSteps (display-only)
**Spinner**: 270 degree arc, 2 px, TEXT; sizes 176 (Connecting art) and 16 (inline). **ProgressSteps** `ui_draw_steps(x, y, steps[], count, current)`: x 464..912 from y 152. Row min h 40, padding 8, gap 16, T20. Done: 12 px `OK` dot. Current: T28 with detail T16 TEXT_2 under it, glow, 16 px spinner in the marker column. Pending: T16 number in TEXT_3. Only the stages of the current flow are listed. Used by: Connecting. Replaces: `ui_draw_spinner` card layout.

### C17 PinField (interactive)
| | |
|---|---|
| Constants | `UI_PIN_BOX_W` 56, `UI_PIN_BOX_H` 72, gap 8, `UI_PIN_CHEV_W` 56, `UI_PIN_CHEV_H` 48 (hit and visible box; art 20), row top y 224 (chevron top; digits y 272-344), `UI_PIN_BTN_Y` 424 |
| Anatomy | Eight boxes (56 x 72, 2 px `LINE` outline, `R_SM` corners), T40 Light digits (focused: white outline + `FILL_FOCUS` + glow). The focused box shows up/down chevron boxes above and below. Empty focused box: blinking 2 x 40 cursor (1 s). |
| Focus | One focus only, in one of two zones: **digits** or **buttons** (Clear digit, Cancel, Register). Right on the last digit when all 8 are filled, or Down, moves to the buttons (Register when ready); Left/Right switch buttons; Up returns to the last digit. In the buttons zone no digit box shows focus. Confirm in the digits zone registers when all 8 are filled (today's behaviour). |
| Input | Up/Down change the digit (empty: Up gives 0, Down gives 9; wraps), Square clears it. Touch: tap a box (focus moves to digits), tap its chevrons, tap the buttons. |
| Used by | PIN screen. Replaces `pin_digit`. |

### C18 QrPanel (display-only)
The panel owns a square slot, `UI_QR_BOX` 216. The module size is a whole number of pixels, the largest that fits the code plus a **quiet zone of 2 modules** (`UI_QR_QUIET_MODULES`) on every side in the slot: floor(216 / (size + 4)). The `QR_PLATE` plate (`R_SM` corners) is shrink-wrapped to that: exactly module px x (size + 4) square, centred in the slot, with the code centred on it, so it always carries exactly 2 modules of white per side. A code that does not fit at 1 px per module is refused (logged, the QR-failed toast), never drawn clipped. The real authorize URL (about 590 characters) is a 93-module code, so 2 px per module and a 194 px plate. Modules from `ui_qr`, ink `QR_INK`; the texture is drawn 1:1. Hidden state: the whole 216 slot in `QR_HIDDEN` with "QR hidden" T16 TEXT_3. The login screen owns a 216 x 216 tap rect over it: **tap toggles show/hide**, same as Start, with the same feedback (toast "QR shown. Scan it with your phone." / "QR hidden. Press Start to show it again."). Next to it a text column. The URL line is a **deliberate short display form** `my.account.sony.com/sso/ca/authorize` (T16, one line, never clipped, never ellipsised); the full authorize URL stays in memory for the QR. Replaces: `draw_profile_login_assist_panel` (reuse the `ui_qr` encoder unchanged).

### C19 Pill (display-only)
h 32, padding 0 16, `HUD` fill, T16 TEXT, `R_PILL` corners. Variants: **warn** (1 px `WARN` outline: cooldown banner), **unstable** (12 px `ERR` dot, pulse 1.4 s), **plain** (exit hint). Used by: stream overlay (exit hint top-right, Network Unstable bottom-right at 16 px margins), hint-row alert slot, top-bar banner slot. Replaces: overlay `draw_pill`, `UI_LOSS_INDICATOR_*`.

**As built (#306).** The plain variant's label is a list of parts, each a text run or a button glyph, measured once by `ui_pill_plain_layout()`, which returns the pill width; `ui_pill_plain_draw()` then draws it with no measuring. A glyph part is the glyph's width plus `UI_PILL_GLYPH_MARGIN` (2 px) on each side, and the space around it comes from the neighbouring text runs ("Hold ", " + "); glyphs draw in TEXT. `ui_pill_draw()` (warn and unstable) takes the width from the caller, measured once with `ui_pill_width()`, so no pill measures text per frame. The old overlay pill and the old menu indicator (`render_loss_indicator_preview()`, `ui_draw_loss_indicator()`, `UI_LOSS_INDICATOR_*`) are deleted; every live menu screen has XMB chrome and shows the alert as the hint-row pill, and the cooldown banner is the top-bar pill.

### C20 ControllerDiagram + Callout (display-only)
| | |
|---|---|
| Images | `controller_front.png` 874 x 396, tinted white (front inverted); the rear art ships as `controller_back_clean.png` (the "Sony Computer Entertainment Inc" line erased, and flipped left to right in #344 so it reads as the back seen from the user's side; keep the unedited original for reference). |
| Constants | Summary front: x 164, y 176, w 630. Summary back: x 170, y 176, w 620. Zone views: front x 120, y 144, w 720; back x 140, y 152, w 680. Callout visible h 32 at y 136 (hit 48: 8 above and below), T20, w 136, left x 64, right edge 896. Footers y 464 h 24. |
| Anatomy | Callout: text "L1 -> <output>" with the arrow glyph, 1 px underline (focused 2 px white + glow). The leader runs from the callout to the shoulder (10% from the top edge, 10% / 90% from the sides) and **ends in a 6 px dot at the shoulder**. Footers T16: left x 48 (preset description or zone name), right x 912 (page label). |
| Input | Up/Down picks L1/R1, Confirm opens its popup. Touch: tap a callout (its 48 high hit rect wins over the diagram) or the diagram. |
| Replaces | procedural diagram drawing is kept as is (`ui_controller_diagram.c`); only chrome changes. |

**As built (#305).** Built as written: the diagram boxes and callout constants are `UI_CTRL_*` in `ui_theme.h`. The left footer is "Zone C2" or "N Zones Selected" in a zone view, and the right footer on page 2 adds the zone count ("Page 2/2 · Back Touch · N zones"). The page-2 label is tappable.

**As built (#325).** The callout's arrow is a baked texture (`ui_arrow.c`, tinted like the text, one space from the name and from the output), because Roboto has no U+2192. The callout draws the shoulder name, the arrow and the output name as separate pieces. The font prewarm set lists only glyphs Roboto has and the UI draws as text (middle dot, almost-equal).

**As built (#327).** The footer buttons (Clear; Whole surface and Clear in a zone view) are at y 460, so every diagram box now ends `UI_CTRL_DIAGRAM_BOTTOM_GAP` (8 px) above them, at y 452: Summary front 175, 176, 609 x 276; Summary rear 176, 176, 607 x 276; zone front 132, 136, 697 x 316; zone rear 132, 136, 695 x 316 (x, y, w x h; the zone-view top moved up from 144 / 152 to 136). The old gaps from the art's bottom edge to the button's top were -2, +2, -11 and -1 px. The buttons are in the D-pad focus order and show the C22 focused look (fill, white border, glow): on Summary page 1, Down from R1 focuses Clear and Up returns to R1 (the callouts show no focus meanwhile); on page 2, Down focuses Clear and Up releases it; in a zone view, Down from the grid's bottom row focuses Whole surface (left half) or Clear (right half), Left and Right switch between them (the grid hides its cursor cell meanwhile, so one thing has focus; picked and mapped cells keep drawing), Up returns to the grid with the cursor where it was, and Confirm activates the focused button. Square and Triangle and touch work as before.

**As built (#340).** Summary page 1 now previews the mapped front zones: it draws the front grid read-only (`UI_CTRL_VIEW_SUMMARY_FRONT` in `ui_controller_zones.c`, over the Summary front box) right after the front art and before the callouts, so the leaders and dots stay on top. It shows the same cell labels as the other grids (T16 Regular, `UI_TEXT`, centred: TP, OPT, L2 and so on, "+" for a Mixed cell) on every mapped block; an unmapped (None) cell stays blank. The widest label (OPT, SHR) is about 30 px and a cell is 61 x 69 px, so no label can overflow. A full grid is 37 draws (the grid-lines texture, 18 fills, 18 labels). It takes no input: a tap on the diagram still opens the Front Touch zone view, and Clear and the preset switch update the blocks through the same sync as the other grids. Both rear views (Summary page 2 and the Rear Touch zone view) also carry a "Left" and a "Right" label (T20, `UI_TEXT_2`) outside the diagram box, `UI_CTRL_SIDE_LABEL_GAP` (16 px) from each side and vertically centred on it; their widths are measured once at init. "Left" is at the diagram's left, beside columns A to C (L2 in the shared default), and "Right" at its right, beside D to F. This follows what a touch does, not the art: `host_input.c` maps a rear touch with `col = x * cols / max_w` (no mirroring) and treats `x < TOUCH_MAX_WIDTH_BY_2` as rear-left, the grid draws column A at its left, and the rear panel reports x in the same direction as the front screen, so a low x is under the user's left hand. Paper draw counts: Summary page 1 worst case 97 (Clear focused; 94 with a callout focused) with all 18 front zones mapped, which is also the default mapping (all 18 Touchpad); the ~80 figure is a paper estimate, not a measured limit (CEO, 2026-10-09), page 2 up to 82, Rear Touch zone view up to 81 (+2 each for the labels). #344 then mirrored the art, so the art, the zones and these labels now agree.

**As built (#344).** The rear art is baked mirrored: `controller_back_clean.png` is flipped left to right on disk, so the rear reads as the back seen from the user's side while holding the Vita. It now agrees with the zones (a left-hand touch hits column A, drawn on the left) and with the #340 "Left" and "Right" labels. Both copies (`vita/res/assets/` and `docs/design/ui-mocks/assets/`) are the same file, and the unedited original under the repo-root `assets/` is untouched. The draw is unchanged (one texture draw, so the draw counts do not move). The dashed rear-pad outline moved from source x 138..582 to 137..581, so the rear grid offset is now x +137 (was +139) in `ui_zone_select.c`, the header comment and the mock's `backRect`; the cell layout, `host_input.c` mapping and the presets are unchanged. The procedural fallback's camera circle moved from the upper right to the upper left (`VITA_CAMERA_CX_RATIO` 0.080).

### C21 ZoneGrid (interactive)
6 x 3 cells (columns A-F, rows 1-3) over the screen rect (front: x +178/874, y +30/396, 526 x 298 source px) or the rear pad rect (x +137/720, y +45/327, 444 x 188 source px), scaled with the diagram. Cell label T16 (OPT, SHR, TP, L1...; blank for None). Constants: `UI_ZONE_COLS` 6, `UI_ZONE_ROWS` 3.

| State | Look | Precedence |
|---|---|---|
| mapped | `ACCENT` 28% fill, `ZONE_MAPPED_LINE` 1 px border | lowest |
| cursor | 2 px white border, `FILL_FOCUS` fill (mapped fill replaced) | over mapped |
| picked (multi-select) | 2 px white border, `FILL_ON` fill, 14 px inner `GLOW_INNER` | highest; a cell that is both cursor and picked draws as picked |

Input: D-pad moves; hold Confirm and move adds cells to the selection (popup on release; a plain tap assigns the one cell); touch (a single-cell gesture opens that cell's popup on release, and the click that follows is ignored): finger paint across cells, backtracking one cell removes the last, release opens the popup; tap = one cell. Used by: Front and Rear zone views; Summary page 2 shows the grid read-only (tap opens Rear zones); Summary page 1 shows the front grid read-only with its cell labels (tap opens Front zones).

**As built (#305).** `ui_zone_grid.c` draws one baked grid-lines texture, one baked state texture per mapped, cursor or picked cell, and one label per cell (37 draws for a full grid), not the single baked grid texture FEASIBILITY.md assumed. `ui_controller_zones.c` keeps one grid per view (Summary rear, Summary front from #340, Front, Rear), created the first time it is shown. A cell that is Mixed shows "+".

### C22 TextButton (interactive)
h 48, min w 128, padding 0 24, T20, `R_PILL` corners, 1 px `LINE` border, no fill. Focused: `FILL_FOCUS`, white border, glow. Disabled: 45%. Pressed: `FILL_ON`. Hit = visible (already above 48). Used by: popups, PIN, Connecting (Cancel). Replaces: `text_button`.

### C23 TopBar (display-only)
y 16, h 32, x 48..912. Three slots: logo (h 32, `Vita_RPS5_Logo.png`) left; centre slot (flex, 24 px padding each side) holds the cooldown banner on Home; right group: Wi-Fi icon 24, battery icon 24 + percent T16, clock T20 (gap 24). **Banner rule:** while the banner shows, the Wi-Fi and battery items are hidden (clock stays), which gives the centre slot about 700 px; the banner pill is at most the slot width on one line, and if the reason string is too long only the reason is shortened with an ellipsis ("Streaming stopped:" and "- Please wait a few moments" always show). Hidden in-stream. System reads needed: link state (`sceNetCtl`), battery percent (`scePower`), local time (RTC), polled about once per second.

### C24 ScrollIndicator (display-only)
2 px track (`LINE_FAINT`), 2 px white thumb, at x 920 beside the pane or the right edge of a list popup. Shown only when rows exceed the viewport. Replaces: 2 scrollbar implementations.

### C25 StatsPanel (display-only)
`PANEL` fill, `R_MD` corners, padding 8 x 16, min w 176, right 16, top 64. Title "Stream Stats" T16 TEXT_2; rows label T16 TEXT_3, value T16 white right aligned. **Latency** = `measured_rtt_ms`, shown as "N ms"; "N/A" when there is no value or the metrics are older than 3.0 s (today's rule). **FPS** = incoming frames per second measured over the last metrics window, shown as "in / target" (target = `target_fps`, else the negotiated fps; "in" alone when no target; "N/A" when none). Unit is whole frames per second. Cadence: the panel text is rebuilt **once per second** (not per frame) so numbers do not flicker; stale detection runs per frame. Shown only when Show Latency is on. Replaces: `draw_stream_stats_panel`.

**As built (#306).** The value rules are pure and live in `ui_stream_stats.c`: `ui_stream_stats_format()` writes the two strings and `ui_stream_stats_metrics_stale()` says whether the latency metrics are too old. Metrics last updated exactly 3.0 s ago still count as fresh; older, or never updated, reads N/A. `video_overlay.c` keeps the strings and their measured widths in a cache rebuilt on the first frame after a stream start and then once per second. Between rebuilds only the stale check runs each frame, and the frame the latency crosses 3.0 s it switches to N/A at once. Turning Show Latency off drops the cache, so turning it on rebuilds on its first frame. The gap between a label and its value is at least 16 px, there are 4 px under the title line (as in the mock), and the width is the widest of the title and the rows plus the 16 px side padding, never under 176.

### C26 EmptyState (display-only)
Single line T20 TEXT_2 at x 304, y 208, with a 16 px inline spinner for Searching. Used by: Home consoles.

### C27 Background (display-only, one static ribbon state)
**Appearance group (CEO-approved 2026-10-10, #375 and #348).** Settings gets a new group **Appearance** with three rows: Background (Waves or Glyphs), Theme and Background Blur. **Display is unchanged** and stays reserved for streaming settings. **Theme** has five presets: **Ocean (default)**, Ember, Orchid, Moss, Graphite. There is one shared Theme for both backgrounds. A theme changes only the waves or Glyphs symbols, the blur veil tint and the accent; text, OK / WARN / ERR and the white focus fill stay fixed. The Theme row shows its name as text only (no colour dot). A rotating monthly theme was considered and is deferred, not part of this design. Mock: `tokens-themes.css` (one token block per preset, `data-theme`), deep links `#settings-appearance`, `?theme=ocean|ember|orchid|moss|graphite`, `?bg=waves|glyphs`, `?glass=none|soft|strong|dark`; screens in `screens/background-theme/`.

5 ribbons, 36 dust points, one fixed palette (section 1.1). The user setting **Background Blur** (Settings > Appearance, choice, default None; applies live, saved immediately; one row, remembered per background) picks one of four modes:

| Mode | Method | Veil on top |
|---|---|---|
| None (default) | today's full-resolution wave | none |
| Soft | wave rendered into a 480 x 272 target (1/2) and averaged 2:1 down to 240 x 136 (1/4), drawn upscaled to 960 x 544 with bilinear filtering; the averaging and the upscale are the blur | `GLASS_VEIL`, 14% dark |
| Strong | same chain continued 2:1 twice more, 120 x 68 then 60 x 34 (1/16), bilinear upscale | `GLASS_FROST` 5% white, then `GLASS_VEIL` 14% dark |
| Dark | same 60 x 34 target | `GLASS_VEIL_DARK`, 20% dark, no white |

The ribbons may be updated into the small target at 15 to 30 Hz while the upscaled quad is drawn every frame (the result is soft enough that the lower rate is invisible); None keeps the 30 Hz CPU vertex update. Freeze or halve updates while Connecting. Measurements, cost and the decision are in FEASIBILITY.md section 8. Replaces: `ui_particles`.

### C29 PairPopup (configuration of C11, issue #330)
| | |
|---|---|
| Purpose | The one place a console is paired: Link Device instructions, the unpaired consoles found on the network (a list that must stay usable with 10 to 30 or more, as in dorms), and a way in for a console discovery cannot see. Opened from the `+ Pair new device` Home item. |
| Size | **L** (480 x 432 at y 56), the same size in every state so the popup never resizes while discovery runs or a filter changes the list. |
| Anatomy | Title T28 "Pair new device". Instruction body T16 `TEXT_2`, 48 high, two lines of one line each: `PS5: Settings > System > Remote Play > Link Device` and `PS4: Remote Play Connection Settings > Add Device`. Section label row T16 `TEXT_3`: "Found on your network", a 16 px spinner arc while discovery runs and at least one console is listed, and a right-aligned count ("12 found"; with a filter the count moves into the Filter row). **List viewport: 4 rows of 48 that scroll** (192 high, so with the Filter row showing, 3 console rows are visible at once; C12 rules: `LINE_FAINT` dividers, focused row an `R_SM` `FILL_FOCUS` bar with glow on the label, 2 px scroll indicator). Inside it, top to bottom: the **Filter row** (when shown), then the consoles. Under the viewport, **pinned and always visible: "Enter IP address"** (48, chevron, a `LINE_FAINT` rule above it), so it never scrolls away however long the list is. A non-focusable note row (T16 `TEXT_3`) sits under the Filter row when there is nothing to list. Height: 32 + 32 + 8 + 48 + 8 + 24 + 8 + 192 + 48 + 32 = 432 of 432 (no spare height; the label row margin is 8). |
| Order | Alphabetical by host name, case-insensitive; the IP address breaks ties. **"Owner first" is not possible:** the discovery reply (`lib/src/discovery.c`) carries host-name, host-type, host-id, system-version, state and running app, and no owner or username field, so the app cannot know whose console it is. Do not add an owner ranking. |
| Filter row | The same control as Home's Filter item (C02), reused: magnifier icon, `Filter...` idle, `Filter: "bed"` active with the count (`2 of 12`) and a small **Clear** button (`tb sm`, 32 high, 8 px hit slop) on the right. Matches a case-insensitive substring of the host name or the IP address ("bed", "192.168.1.4"). Label text is ASCII only: straight double quotes, three dots for the ellipsis (curly quotes and the ellipsis character are not in the font atlas; the Home Filter row follows the same rule). Confirm or a tap opens the system keyboard (title "Filter Found Consoles", current text prefilled; Done with empty text clears). Square on the focused active Filter row clears; Start from anywhere in the popup list opens the keyboard (or clears an active filter), as on Home. **Shown when more than 4 consoles are found (same rule as Home) or while a filter is active**, so a short list stays plain and a filter can never trap the user without its row (flag 18, item 11). Focus starts on the first console, so Confirm still pairs at once; Up reaches the Filter row; it is visible at the top of the list on open. Active filter: Filter row, count and Clear are always on screen at the top of the list or one Up away. |
| States | one console (`#pair-popup-one`), 3 found (`#pair-popup-many`, no Filter row), 12 found with the Filter row (`#pair-popup-long`), 12 found scrolled (`#pair-popup-scroll`), filtered by name (`#pair-popup-filter-name`), filtered by IP (`#pair-popup-filter-ip`), no matches (`#pair-popup-nomatch`: the note "No consoles match filter", focus on the Filter row so the filter can be changed or cleared, Enter IP address still offered), filter keyboard (`#pair-popup-filter-kb`), searching with none yet (`#pair-popup-searching`), scan finished with none (`#pair-popup-none`), Auto Discovery off (`#pair-popup-discoff`), Enter IP address focused (`#pair-popup-ip`). With no console listed focus starts on Enter IP address (on the Filter row when a filter hides everything). |
| Input | Modal. Up/Down moves focus through Filter row, consoles, then the pinned Enter IP address row; no wrap. Confirm on a console closes the popup and opens the PIN screen for it (C17, 3.2). Confirm on Enter IP address closes the popup and opens the system keyboard (below). Circle, or a tap on the scrim, closes the popup and returns to Home with `+ Pair new device` still focused. **Touch:** tap a console (focus and pair in one tap, as every popup list), tap the Filter row (opens the keyboard), tap Clear, tap Enter IP address; vertical swipe on the list scrolls 48 px per row (the pinned row is not part of the swipe). **L and R page the list** (controller path): L moves focus up one page (4 rows) and scrolls the list the same distance, R moves down one page, both clamped to the first and last console (from the pinned Enter IP row, L returns to the last console); the pinned row is reached with Down. Hints: console `[Confirm Pair] [L R Page] [Circle Close]`; Filter row `[Confirm Filter] [Square Clear, only when active] [L R Page] [Circle Close]`; Enter IP row `[Confirm Enter IP] [L R Page] [Circle Close]`. `L R Page` shows only when the list has more rows than the 4 visible, and is the first hint dropped when the row is crowded (not tappable; touch pages by swipe). |
| Discovery | Discovery keeps running while the popup is open; rows appear (in sorted position) as consoles answer and are removed when they stop answering (focus moves to the nearest row if the focused one disappears). A console that is already paired is never listed. |
| Enter IP address | 1. System keyboard, title "Console IP address", empty (`#pair-ip-keyboard`). 2. On Done the popup "Looking for console" (size S, sub line the address, spinner, one Cancel button, `#pair-ip-looking`) while the app sends one unicast discovery packet to that address and waits 5 s. 3a. A reply gives the console's name and model: the PIN screen opens for it (`#pin-manual`, title "PS5 Console Registration", sub "PS5-204 (192.168.1.77)"). 3b. No reply: result popup (C14) "Console not found", "Nothing answered at <ip>. Check the address, and that the console is on and on the same network.", Close / Try again (`#pair-ip-notfound`). 3c. The text is not four numbers 0 to 255 separated by dots: result popup "Not an IP address", "Use four numbers separated by dots, like 192.168.1.20." Close / Try again (`#pair-ip-invalid`). Try again reopens the keyboard with the typed text. Close returns to Home with `+ Pair new device` focused. |
| Draws (paper) | **Worst case about 59** (12 found, filter active, the Filter row focused with the Square Clear hint), counted against the real `ui_popup_draw` frame: frozen background copy 1, popup frame 19 (scrim 1, card 9-slice fill 9 and border 8, title 1), instruction 2 lines 2, section label 1 + spinner arc 1 (the count moves into the Filter row), Filter row 11 (glow 1, bar 3, icon 1, text 1, count 1, Clear 4: border 3 + label), 3 console rows x 3 (label, right label, divider) 9, scroll indicator 2, pinned Enter IP row 4 (rule, divider, text, chevron), hint row 9 (4 hints: Confirm 2, Clear 2, L R 3, Close 2). A console focused instead of the Filter row costs the same for the rows (20) and 2 less in the hint row. 12 found, Filter idle, a console focused: about 53. Searching or empty: about 37. Looking popup: about 24. Home behind is the one frozen draw, so this is well under the ~80 paper reference (FEASIBILITY 4). |
| Cost | No new font size and no new atlas glyph (ASCII, `>` and the middle dot). One new item icon (plus in a ring, 38 px) and one new hint glyph (D-pad up); the Filter row reuses the magnifier and the Clear button reuses the small TextButton. |

### C28 (removed)
The separate filter line was folded into the Filter item of the Consoles list (C02).

---

## 3. Screens

Deep links: `xmb.html#<id>`. The jump menu in the mock lists all of them. Button names are logical (Confirm and Cancel swap, section 4).

### 3.1 Home (XMB)
Layers: C27, top bar C23, C01, C02, C04, C05, hint row C06. Category row y 104, list from y 192, detail x 608.

| Category | Items | Detail | Confirm |
|---|---|---|---|
| Consoles | `+ Pair new device` (row 0), the Filter row (when shown), then **paired** console rows by name (#330) | C04 console | per state, below |
| Settings | Video, Network, Display, Appearance, Controls, Advanced | the group's rows and values | opens Settings page at that group |
| Controller | Custom 1, Custom 2, Custom 3 (+ description line) | L1, R1, Front touch, Rear touch | opens Controller summary on that preset |
| Profile | Account, Connection, PlayStation Network | first rows of the group | opens Profile page at that group |

Console states (rows and detail):

| State | Dot | Status text | Confirm does | Deep link |
|---|---|---|---|---|
| Ready | OK | Ready | Connecting (local) | `#home` |
| Standby | WARN | Standby | Waking, auto-connects | `#consoles-standby` |
| ~~Unpaired~~ (#330: not a Home state any more; it is a row in the Pair new device popup, 3.1a) | no dot in the popup | Unpaired | PIN screen | `#pair-popup-one` |
| Internet only (PSN, valid token) | OK | Ready, "Internet" in `INTERNET` | Connecting (internet) | `#consoles-psn` |
| Unavailable (not discovered, no route; not a failure, the console is just not reachable now) | neutral TEXT_3 (room icon at 55%) | Unavailable (TEXT_3) | tries to connect, result popup on failure | `#consoles-unavailable` |
| **Error** (a failure the user must act on) | ERR | Error, message in the info panel | tries to connect | `#hints` |
| **Retrying** (the app is retrying or waiting) | WARN | Retrying, message in the info panel | tries to connect | `#hints-retry` |
| Cooldown | WARN (row 55%) | Please wait... | nothing; Connect hint dimmed; banner in the top bar | `#consoles-cooldown` |

**Status messages.** The list shows only the status label. The message for an Error or Retrying console appears **only in the info panel**, under the status line and above the Address / Route / Pairing table, in the status colour, wrapped up to 3 lines (all current messages fit in 2 lines at 304 px). Full classification in the copy deck; `#hints` selects an Error console, `#hints-retry` a Retrying one. Network Unstable shows as a pill in the hint row's reserved right slot (200 px) on any menu, hints never run under it; collapse rule in C06 (`#home-unstable`). The cooldown banner uses the top bar's centre slot (C23): `Streaming stopped: <reason> - Please wait a few moments`, one line, only the reason is shortened if it does not fit, Wi-Fi and battery hidden while it shows (`#consoles-cooldown`). Empty: `#empty-searching`, `#empty-nomatch`. Filter: `#filter-item` (Filter focused, idle), `#filter` (active filter, 1 found), `#keyboard`.

**Filter item rule.** The Filter row exists only when there are more than 4 consoles or a filter is active. This is intentional: with 4 or fewer consoles filtering is not needed, and Start still works as a shortcut (it has no touch path in that case, which is acceptable because nothing is hidden to filter). Button map: Up/Down item, L/R or Left/Right category, Confirm activate, Triangle options (consoles), Start filter shortcut (not shown in the hint row). Hints: console focused `[Confirm verb] [Triangle Options] [L R Category]` (verb = Connect, Wake, Pair, or "Please wait", dim); Filter focused `[Confirm Filter] [Square Clear, only when active] [L R Category]`. Touch: tap category, tap row (focus then activate; the Filter row opens the keyboard at once), long-press a console for Options, swipe, tap hint.
Transitions: category change slides the bar 300 ms and cascades the list; focus change slides rows 300 ms; detail rises 300 ms.

Sub-screens from Home:

| Id | What | Component |
|---|---|---|
| `#options` | Triangle options column | C05 |
| `#pair-item` and the `#pair-*` links | `+ Pair new device` item and its popup, section 3.1a | C02, C29 |
| `#icon-picker` | Change icon, 3 x 2 grid | C11 M + C12 grid |
| `#connect-via` | Connect via: Local Network (IP) / Internet (PSN) | C11 S + C12 |
| `#repair` | "Re-pair <name>?" Cancel / Re-pair | C13 |
| `#keyboard` | system keyboard "Filter Consoles" (stand-in dim + caption; the real IME is the Vita's) | stand-in |

Categories deep links: `#xmb-settings`, `#xmb-controller`, `#xmb-profile`.

### 3.1a Pair new device (issue #330, CEO-approved 2026-10-09)
Reverses "Add item removed" from round 8. Home's Consoles list now shows **paired consoles only**. Discovered unpaired consoles live in the pairing popup (C29), not on Home. Deep links: the `Pair new device` group in the mock's Jump menu, all listed in `index.html`.

**The item.** Row 0 of the Consoles list, always present, above the Filter row (when shown) and the consoles. Icon: plus in a ring (`icons/plus.svg`, 38 px). Name "Pair new device" (C02 focus styles unchanged). Status line (T16 focused): "1 found nearby", "N found nearby", "Searching..." (discovery running, none yet), or "Link a console" (none found, or Auto Discovery off). Detail panel: title "Pair new device", "Link a PS5 or PS4 to this Vita. Turn the console on and join the same network.", then rows Paired (N consoles) and Found nearby (N unpaired, Searching..., None, or Discovery off). Hints when focused: `[Confirm Pair] [L R Category]`; no Triangle (nothing to option).

**Focus.** Focus still starts on the first console on launch and on category change, so Cross still connects at once. **Up once from the first console reaches the item** (twice when the Filter row is shown: first Filter, then the item). With no paired console the item is the only row and starts focused (`#home-first-run`, `#home-first-run-found`), with "No paired consoles yet" under it. Confirm or a tap opens the popup at once (like the Filter row; no focus-then-activate). Long-press does nothing on it. The list swipe moves focus onto it like any row.

**Unfocused state.** By the list's rule, rows above the focus fade to 0, so the item has no resting appearance while a console is focused (`#pair-item-hint`, the Home list shows only consoles). This is the real cost of putting it above the list (flag 18, item 3). To keep it findable the hint row gains `[D-pad up] Pair new device` (tappable, same effect as Up, dropped first when the row is crowded) whenever the row directly above the focus is the item. With Show Button Hints off, only the swipe or Up reaches it.

**Flow.** Home, item, popup (C29), PIN screen (3.2), result popup (3.3), Home with the new console focused. Leaving the PIN screen with Cancel or Circle before pairing, or closing any popup or result in this flow, lands on Home with the item focused. Leaving the PIN screen of a re-pair (a console that is already paired) lands on that console as today.

**As built (#330, slice 1).** The item, its detail panel and hints, the C29 popup without its Filter row and L/R paging, the flow back to Home, and the removal of Show Only Paired are built; Enter IP address only logs (`ui_pair_popup_enter_ip_address()`). "Searching..." on the item counts from when `ui_pair_hosts_item_phase()` first sees discovery running (the app starts it at launch); in the popup it counts from the popup opening. The popup reads `context.hosts` every frame, not the Home card cache.

**As built (#338).** Triangle > Re-pair keeps the old pairing until a new one succeeds: the console's credentials, its paired state and the saved config are not touched when the PIN screen opens. Cancel before pairing, a cancelled attempt, a timeout, a wrong PIN or any other failure leaves the console paired and listed on Home, and leaving the PIN screen (Cancel, or Close on the failure popup) lands on that console, not on the item. A fresh pair from the popup and the Enter IP address flow still land on the item. A successful re-pair replaces the credentials on every entry of the console (the discovered one and each saved manual host); if any entry could not be updated (one in use by a running session, or out of memory), the success popup is the "Console paired" warning instead. If the console accepts the PIN but the Vita cannot write the config file, the attempt still ends as "Pairing failed", yet the new credentials are already in use until the app restarts, and after a restart the old ones come back from the file; pairing again fixes it.

**As built (#330, Enter IP address).** The flow follows the Enter IP address row of C29: keyboard "Console IP address" (empty, `SCE_IME_TYPE_BASIC_LATIN` so dots can be typed), "Not an IP address" for text that `ip_address_parse()` rejects or that `ip_address_is_console_target()` refuses (first number 0, 127, or 224 and above, so 0.0.0.0, loopback, multicast and broadcast; leading zeros and spaces around the text are accepted and shown in canonical form), "Looking for console" while the probe runs (5 s), then the PIN screen or "Console not found" (also shown when the probe cannot run). Try again reopens the keyboard with the typed text; Close, Cancel, Circle and a tap on the scrim return to Home with the item focused. A console that answers goes straight to the PIN screen, titled by its model, sub line "<name> (<ip>)"; if its MAC matches an already paired console this is a re-pair (same screen, no confirm popup, the new credentials replace the old ones everywhere the console is listed when it succeeds). A successful pairing through an IP saves the address as a manual host, so the console appears on Home even when broadcast discovery cannot see it, and Home focuses it. If the address or the new credentials could not be saved, the success popup is instead the warning "Console paired" ("<name> is paired, but this Vita could not save everything. If it does not appear or connect, pair it again.", one OK button), and OK focuses the console if Home lists it, else the Pair new device item. The probed host stays out of the Home list until pairing succeeds.

**As built (#330, Filter row and paging).** The Filter row is the first row of the popup list (`ui_pair_popup.c`), drawn with the C12 row, a 24 px magnifier and the small TextButton; its keyboard (`ui_filter_keyboard.c`) is the one Home's filter uses, with its own title "Filter Found Consoles" and its own text: the popup's filter is separate from Home's, starts empty each time the popup opens, and survives the keyboard round trip. On open the focus is the first console, so Confirm pairs at once. A filter change that removes the focused console moves the focus to the nearest remaining row, and to the Filter row when nothing matches. L and R page by `UI_PAIR_VISIBLE_ROWS` rows through `ui_pair_page_focus()`; a tap on the "L R Page" hint does nothing. Draw cost on paper, worst case about 59 (see Draws above). Team rulings recorded with the build: (a) the popup's "Searching your network..." lasts 5 s from the popup opening, while the Home item's "Searching..." covers the first 5 s after discovery starts; (b) a successful IP pair saves the address as a manual host; (c) an IP that belongs to an already paired console is a re-pair; (d) the Filter row scrolls with the list, and Start does nothing when the Filter row is not shown.

**Re-pair stays.** Triangle > Re-pair on a paired console is unchanged: it is about one specific console whose credentials are stale, the popup deliberately never lists paired consoles, and the confirm ("Re-pair <name>?") protects against doing it by accident. The Pair new device item is for adding, Options is for repairing.

**Show Only Paired (decided: removed).** Home no longer shows unpaired consoles, so the toggle has nothing left to hide. Remove the Settings > Network row and its config key (`show_only_paired`; an old value is ignored). Network keeps Auto Discovery and Enable PSN Internet Mode. The alternative (keep it, inverted to "Show unpaired consoles on Home", default off) brings back the clutter the CEO asked to remove and is not recommended. Flag 18.

**Unpaired row state (decided: removed from Home).** Removed from Home (table above, the Unpaired deep links, the Unpaired Options variant and the Unpaired row of Profile > Connection). The state survives only as a row in the popup, where it needs no status dot because every row there is unpaired. The word "Unpaired" stays in the copy deck for the detail panel's Pairing row only if a later change lets an unpaired console be selected; today the row always reads Paired.

### 3.2 PIN (`#pin`, `#pin-partial`, `#pin-full`)
Page shell (lock icon). Title "<PS5|PS4> Console Registration" with the console name and IP as a T16 sub. Prompt T20 centred at y 160: "Enter the 8-digit session PIN displayed on your <PS5|PS4>:". C17 at y 224. TextButtons at y 424: Clear digit, Cancel, Register (disabled until all 8 are filled).
**Focus**: one focus only, digits zone or buttons zone (C17). `#pin-full` opens with focus on Register and no digit highlighted. Buttons: Left/Right digit (digits zone) or button (buttons zone), Up/Down change the digit, Square clear digit, Confirm register (all 8 filled) or press the focused button, Cancel back to Home. Hints (digits): `[D-pad Digit] [Up-down Change] [Square Clear digit] [Confirm Register (dim until filled)] [Cancel Cancel]`; (buttons): `[D-pad Button] [Up-down Digits] [Confirm <focused button>] [Cancel Cancel]`. Touch: tap a digit box, tap its chevrons, tap a button.
**As built (#303).** `#pin-full` is a preview only: the screen always opens empty with focus on the first digit. Down changes the digit (as the mock does); it does not also move to the buttons, so the buttons zone is reached with Right on the last digit once all 8 are filled (Register is disabled until then; Clear digit and Cancel are also reachable by touch, and Cancel by Circle). While the attempt runs the prompt reads "Pairing..." (new copy, for sign-off), the digits and the Clear digit and Register buttons are drawn at 45%, and Cancel stays live and stops the attempt.

### 3.3 Result and error popups, and the pairing result contract
One C14, used for pairing success, pairing failure and connection failure. `#result-paired` (OK tone, one button OK), `#result-pair-failed`, `#result-pair-timeout`, `#result-pair-unreachable` (ERR, Close / Try again), `#result-connect-failed` (ERR, Close / Try again). Circle closes; hints follow the focused button (C06).

**Build requirement (backend, not UI).** Today registration shows no outcome. For these popups the registration code must report one of exactly four results to the UI, each with the console it concerns:

| Result | Popup |
|---|---|
| finished OK | "Console paired" / "<name> is paired. You can connect to it now." / OK. The console becomes Paired and the list re-sorts |
| failed: PIN not accepted | "Pairing failed" / "<name> did not accept the PIN. Check the code on the console and try again." |
| failed: console unreachable | "Pairing failed" / "<name> could not be reached. Check that it is on and on the same network." |
| timeout | "Pairing failed" / "<name> did not answer in time. Open Link Device on the console again and retry." (the timeout length is an engineering choice; propose 30 s) |

Failure popups offer Close and Try again (Try again returns to the PIN screen). Connection failure shows the raw disconnect reason from the copy deck.

**As built (#303).** The backend cannot tell a wrong PIN from an unreachable console, so its FAILED result shows one collapsed body: "<name> did not accept the PIN or could not be reached. Check the code and that the console is on the same network." The other two bodies are used when the Vita knows the cause (no route to the console before lib is called: unreachable; no answer in 30 s: timeout). A cancel shows no popup. Cancel or a tap outside closes a failure popup (Close) and confirms a success popup (OK).

**Mock-only trigger:** in the mock a PIN starting with 0 gives "PIN not accepted", starting with 9 gives "timeout", anything else succeeds; this is not app behaviour.

### 3.4 Connecting and Reconnecting
Page shell, no groups. Title is set **per flow**, not per stage: a local standby flow reads "Waking Console" on its first stage and "Starting Remote Play" after; a local ready flow reads "Starting Remote Play"; an internet flow reads "Starting Internet Remote Play" from first stage to last, except internet standby, whose wake step reads "Waking Console" with the globe icon (CEO ruling 2026-10-09). (Today's code flips the title per stage; the flip is a bug and this is the fix.) Each title has a 32 px icon (C07). Left art: ring 128 inside a 176 spinner and a soft halo (280), x 68..388, y 168..368, centred; type logo h 32 at y 384, name T28, route T16 ("via Local Network" / "via Internet"). Right: C16 steps. Cancel TextButton right-aligned at y 440. Circle or tap Cancel stops the connect and returns Home. Standby console: wake polls until awake, then auto-connects (steps continue).

| # | Stage | Detail line | Flow |
|---|---|---|---|
| 0 | Waking console | Sending wake signal | local standby |
| 1 | Authenticating with PSN | Validating account tokens | internet |
| 2 | Fetching internet consoles | Loading remote-play capable devices | internet |
| 3 | Creating PSN session | Creating cloud-assisted session | internet |
| 4 | Preparing Remote Play | Negotiating session | all |
| 5 | Punching control channel | Establishing control tunnel | internet |
| 6 | Punching data channel | Finalizing media tunnel | internet |
| 7 | Starting stream | Launching video pipeline | all |

Flows: local ready [4, 7]; local standby [0, 4, 7]; internet [1, 2, 3, 4, 5, 6, 7]. Deep links: `#waking` (standby, stage 0), `#connecting` (local), `#connecting-internet`, `#waking-all` (reference ladder of all 8, not a real flow).

**As built (#341).** A fourth flow, internet standby, is the Internet connect to a console that must be woken first (Triangle, Connect via, Internet on a console in rest mode). It lists stages [0, 1, 3, 4, 5, 6, 7]: the internet flow with "Waking console" (0) in place of "Fetching internet consoles" (2), a stage no code ever reports. Seven rows is what the layout fits, so the list was swapped, not given an 8th row. Its icon is the globe and its route "via Internet" on every step, from the wake step to the last. Its title is "Waking Console" on the wake step (the moon is for local standby only), then "Starting Internet Remote Play" on every later step (CEO ruling 2026-10-09). The route follows the user's choice (Connect via Internet, or a PSN remote console), never the console's state: a begin at stage 0 is local standby only when the connect is local. When the console wakes, the connect moves to CONNECTING, which both internet flows show as the "Authenticating with PSN" step.
**Reconnecting** (`#reconnecting`): same shell, title "Optimizing Stream"; spinner art only; right column: "Recovering from packet loss" T20, "Retrying at 1.80 Mbps" T28, "Attempt 2" and "Please wait..." T16 TEXT_3. No input, no hint row, not cancellable.

### 3.5 Stream overlay (no menu)
Full-bleed video. Only three things draw, all C19/C25, all gated by settings:

| Element | Position | Rule |
|---|---|---|
| Exit hint pill "Back to menu: Hold [L] + [R] + [Start]" | right 16, top 16 | Show Exit Shortcut Hint; visible 5.0 s then 0.5 s fade |
| Stream Stats panel | right 16, top 64 (fixed slot) | Show Latency. Latency in ms, FPS as "in / target" whole frames per second; rebuilt once per second; "N/A" when metrics are older than 3.0 s (C25) |
| Network Unstable pill | right 16, bottom 16 | Show Network Alerts; shown 5.0 s after each event, fading |

**As built (#306).** The exit hint is visible 5.0 s from the first overlay frame of the stream, then fades linearly over 0.5 s (`UI_STREAM_HINT_VISIBLE_MS`, `UI_STREAM_HINT_FADE_MS`); the fade is the layer opacity. Network Unstable fades linearly over its alert (5.0 s unless the stream set another length), on top of the pill's own 1.4 s pulse. The old "(Select) Hints" indicator at the top right and the old Select-hints toast are gone (`ui_hints_*` and their wrappers are deleted); the hint row replaced them. In testing builds the debug resync widget ("tap: resync", `debug_tools_draw_widget()`, a `UI_PANEL` box on the small 9-slice with T16 TEXT_2) also draws on the stream, in the bottom-right corner at 6 px margins, so it overlaps the Network Unstable pill while that shows.

**Stream stats exist only here.** Profile has no streaming metrics (there is no menu while streaming). Exit: hold L + R + Start about 1 s (today). Select = Share and Start = Options still go to the PS5. Deep links: `#stream`, `#stream-stats`, `#unstable`, `#stream-quiet` (after the hint faded). The mock also lets Esc leave (mock only).

### 3.6 Settings (`#settings`, `#settings-network`, `#settings-display`, `#settings-appearance`, `#settings-controls`, `#settings-advanced`, `#settings-circle`)
Page shell, title "Settings". Left: groups. Pane: C08 rows; description line for the focused row. All values save immediately (today).

| Group | Row | Type | Values (default) |
|---|---|---|---|
| Video | Quality Preset | choice | 360p, 540p (540p) |
| Video | Latency Mode | choice | Ultra Low (~1.2 Mbps), Low (~1.8 Mbps), Balanced (~2.6 Mbps), High (~3.2 Mbps), Max (~3.8 Mbps) (Balanced) |
| Video | FPS Target | choice | 30 FPS, 60 FPS (30 FPS) |
| Video | Force 30 FPS Output | toggle | off |
| Video | Fill Screen | toggle | off |
| Network | Auto Discovery | toggle | on (read at startup only) |
| Network | Enable PSN Internet Mode | toggle | off |
| ~~Network | Show Only Paired | toggle | off~~ (#330: row and config key removed, Home lists paired consoles only; flag 18) | | | |
| Display | Show Latency | toggle | off (shows the stats panel in the stream overlay only; default pending CEO, see Flags) |
| Display | Show Network Alerts | toggle | on |
| Display | Show Exit Shortcut Hint | toggle | on |
| Display | Show Button Hints | toggle | on (new) |
| Appearance | Background | choice | Waves, Glyphs (Waves; new, #375) |
| Appearance | Theme | choice | Ocean, Ember, Orchid, Moss, Graphite (Ocean; new, #348). One setting for both backgrounds. No colour dot or swatch beside the name. |
| Appearance | Background Blur | choice | None, Soft, Strong, Dark (None; new). One row, remembered per background. Both backgrounds offer all four levels (CEO ruling 2026-10-10: Glyphs is not limited to None and Soft). Moved here from Display. |
| Controls | Circle Button Confirm | toggle | system default (Cross on a Western unit) |
| Advanced | Clamp Soft Restart Bitrate | toggle | on |
| Advanced | Motion during loss (artifacts) (Experimental) | toggle | off |
| Advanced | Enable Logging | toggle | off |

The mock starts with Enable PSN Internet Mode on so internet screens can be shown; the app default is off. Button map: Left/Right or Confirm change, Up/Down row, L/R group, Cancel (pane to groups, then Home). Hints: `[Confirm Toggle|Next] [D-pad Change (choice)] [L R Group] [Cancel Back]`. Touch: tap group, tap row, tap chevrons, swipe pane. `#settings-circle` turns Circle Button Confirm on: every glyph in the hint row swaps and Circle becomes confirm (use the toolbar toggle to try any screen).

### 3.7 Profile (`#profile`, `#profile-connection*`, `#profile-psn*`, ...)
Page shell, title "Profile". Groups: Account, Connection, PlayStation Network. **Identity block** (compact, always visible under the group list at y 376, 48 high): avatar (48 px circle, 2 px `LINE`, profile icon 28), PSN Account ID (T16, ellipsis, "Not Set" when empty) with "PlayStation Network" T16 TEXT_3 under it.

| Group | Rows |
|---|---|
| Account | Account ID (T16, full value), action **Refresh Account ID** (toast "Account ID refreshed from system profile" or "Could not refresh Account ID") |
| Connection | per the state table below |
| PlayStation Network | PSN Auth status row, then actions by state |

**Connection state table.** Streaming metrics (Latency, Bitrate, Packet Loss) are removed from Profile. Rows follow today's code (`ui_screens.c`, connection card) except Status, which is a deliberate fix (Flags).

| Situation | Network Type | Console | Console IP | Status | Quality | Deep link |
|---|---|---|---|---|---|---|
| Console found on the local network, ready | Local Wi-Fi | name | address | Ready | current preset | `#profile-connection` |
| Same, in rest mode | Local Wi-Fi | name | address | Standby | preset | `#profile-connection-standby` |
| ~~Same, not paired~~ (#330: an unpaired console can no longer be the selected console, row removed) | | | | | | |
| Internet (PSN) console | PSN Internet | name | address when known (usually none) | Ready | preset | `#profile-connection-psn` |
| Manually added host | Manual Host | name | address | Ready | preset | not in the mock |
| Console selected, no discovery, PSN or manual route applies | Unavailable | name | address if known | Unavailable | preset | `#profile-connection-unavailable` |
| No console selected | Unavailable | Not selected | - | None | preset | `#profile-connection-none` |

Rules: Network Type is Local Wi-Fi when the console is discovered, else PSN Internet when its source is PSN, else Manual Host when manually added, else Unavailable. Console is the display name, else the hostname, else "Not selected". Console IP shows whenever a hostname/address is known. **Quality always shows** (it is the user's setting: 360p or 540p). **Status uses the console's real state with the Home list's words** (Ready, Standby, Unavailable; "None" only when no console is selected).

PSN Auth states (`#profile-psn-<state>`): Disabled (`profile-psn-disabled`, Log in disabled, description "Enable PSN internet mode in Settings", pressing it toasts "PSN internet mode is disabled in Settings"), Authenticated (`profile-psn`: Refresh hosts, Log out), Refreshing token (`profile-psn-refresh`, no actions), Awaiting browser sign-in (`profile-login`), Token expired (`profile-psn-expired`), Not authenticated (`profile-psn-none`), error text (`profile-psn-error`, e.g. "Login failed: invalid redirect URL"). Not authenticated, Token expired and error offer Log in. **Error styling**: the three red states show the value in `ERR` with a 20 px warning icon before it and a 2 px `ERR` rule at the row's left edge, so they read as errors without relying on colour alone.
**Log out**: first Confirm turns the row into "Press [Confirm] again to confirm log out" in `WARN`; second Confirm within 3.0 s logs out (toast "PSN login removed"); otherwise it resets (`#profile-logout`). Touch: two taps.
**Phone login** (`#profile-login`, `#profile-login-hidden`): the pane is replaced by "Phone Login Assist": C18 at the left (216 slot) and, in the column to its right (x pane + 232, up to the pane's right edge), three steps with the Code line ("Paste redirect URL/code" until a code exists) and the short sign-in URL under them (C18); two buttons (Enter code, Cancel login) on a row below. Buttons: Start show/hide QR, Confirm opens the system keyboard "Paste full redirect URL" (`#keyboard-paste`; the field shows the **tail** of long text, clipped inside the field), Square cancels. Touch: tap the QR to toggle (toast feedback, C18); the three hints are tappable. Hints: `[Confirm Enter code] [Start QR] [Square Cancel login]`. The Vita browser is not offered: it can no longer open Sony's sign-in page (#346).
Toasts: representative `#toast-account` and `#toast-login-complete`; full list in the copy deck.

**As built (#304).**
- Status for a manually added console that is not discovered reads "Unavailable", not the table's "Ready". Profile and Home share one rule (`ui_console_connection_words()`), and Home's list classifies such a console as Unavailable. A PSN console without a valid token also reads Unavailable.
- The login pane's two buttons (Enter code, Cancel login) are touch targets only; the face buttons do the work. While the login pane shows, Cancel goes back to Home and the login keeps running.
- The toast "Scan QR on phone, then press [Confirm] ..." names the button in words (Cross, or Circle with Circle Button Confirm), because the toast draws no glyphs.
- New toast copy, for sign-off: "Could not draw the QR code. Cancel and try again." It shows when the code cannot be encoded or does not fit the 216 px slot at 1 px per module (#346).
- The armed Log out label and its glyph are drawn in WARN.
- The toast stays fully visible for 3.0 s, then fades over 300 ms.

### 3.8 Controller
Page shell, title "Controller" (zone views: "Front Touch" / "Rear Touch" with the preset as T16 sub).

| Id | View | Notes |
|---|---|---|
| `#controller` | Summary page 1 "Buttons": front diagram with the mapped zones as labelled blocks, L1 / R1 callouts, footer = preset description and "Page 1/2 · Buttons" | |
| `#controller-back` | Summary page 2 "Back Touch": rear diagram with zone labels and a "Left" and "Right" label either side of it (the Rear Touch zone view has them too) | footer shows zone count |
| `#controller-front`, `#controller-rear` | Zone views | cursor on one cell |
| `#controller-multi` | multi-selection while Confirm is held | 4 cells picked |
| `#controller-full` | Triangle: whole surface popup | |
| `#mapping-popup`, `#mapping-multi`, `#mapping-shoulder` | mapping popup, size L | |

The whole-surface popup (Triangle) shows the real current value in its subtitle ("Full Front Touch · Touchpad") and ticks it; when the 18 zones differ it reads "· Mixed" and nothing is ticked. Same rule for a multi-selection.

Mapping popup: titles "Shoulder Mapping" / "Front Touch Mapping" / "Rear Touch Mapping"; subtitle = slot name ("Front C2", "Rear A1", "Left Shoulder (L1)", "Right Shoulder (R1)", "Full Front Touch", "Full Rear Touch") or "N Zones Selected"; 11 rows: Options, Share, Touchpad, L1, L2, L3, R1, R2, R3, PS, None; current value ticked; hints `[Confirm Assign] [Cancel Cancel]`.
Summary buttons: Left/Right preset (saved at once), L/R page, Up/Down choose L1 or R1 (page 1), Confirm opens the shoulder popup (page 1) or the rear zones (page 2), **Triangle opens the zone view for the current page (new; today the zone views open by touch only, confirmed as wanted)**, Square clears the current side, Circle Home. Hints `[D-pad Preset] [L R Page] [Up-down L1 / R1] [Confirm Shoulder] [Triangle Zones] [Square Clear] [Cancel Back]` (page 2 omits the two shoulder hints).
Zone view buttons: D-pad moves the cursor; hold Confirm and move to add cells, release opens the popup (a plain tap assigns one cell); Triangle assigns the whole surface; Square clears the side; Circle returns to the summary. Hints `[D-pad Move] [Confirm Assign] [Triangle Whole surface] [Square Clear] [Cancel Back]`.
Touch: tap the preset chevrons or the label; tap a callout; tap the diagram to enter the zone view; in the zone view finger-paint cells (backtrack removes), tap one cell; tap hints. Callouts have a 48 px high hit rect that wins over the diagram. Presets seed from one shared default (`vita/src/controller.c`, used by the page and the stream alike): front all zones Touchpad, rear left three columns L2 and right three R2, L1/R1 as themselves (L+Square = L3, R+Circle = R3 and Select+Start = PS are combos that remain and have no editor).

**As built (#305, Summary page 1).** The page opens on the preset Home's item names (Custom 1 to 3), which makes that preset the current one, as Left/Right would. Left/Right switch preset on the press only (no hold-repeat, because each switch saves the config), Up picks L1 and Down picks R1 (no wrap). The diagram is the procedural/art diagram unchanged except that its old pill callouts, page text and front zone labels are gone; the page draws the callouts. A callout grows past 136 px when its text is wider, keeping its outer edge. The leader runs from the middle of the callout's underline. The mapping popup focuses the ticked row, or the first row when nothing is ticked. A never-saved preset is seeded from the defaults the first time it is shown, as before.

**As built (#305, the rest of the page).** The page is `ui_controller_page.c` with the zones in `ui_controller_zones.c`, the popup in `ui_controller_mapping.c`, the data in `ui_controller_model.c` and the rules in `ui_controller_rules.c`.
- **What a zone and a side show is what a touch does in the stream.** A touch fires the side's whole-surface input and the output of the zone under the finger (`host_input.c`), and the two add up. A zone's value is its own output plus the side's whole-surface output; when they disagree, or the whole-surface input holds L2 or R2 (which presses the wrong buttons there), the zone is Mixed. The side's value ("whole surface") is the common value of its 18 zones, or Mixed.
- **Editing keeps the page equal to the stream.** Assigning to zones first folds the side's whole-surface output into the zones that have none of their own and clears it. Assigning the whole surface (Triangle in a zone view, or Whole surface) writes all 18 zones and clears the whole-surface input. Clear is a whole-surface assign of None.
- **With the shared default the front reads Touchpad (18 zones) and the rear reads L2 on the left three columns and R2 on the right three (18 zones).** The rear L2/R2 sit on the rear grid zones the stream reads on a rear touch, and a never-saved preset is the same on the page and in the stream (flag 17, resolved by #319). Presets already saved with the old seed are not repaired.
- **A Mixed cell shows "+"** (new copy, sign-off pending). The popup subtitle for several cells is "N Zones Selected · <value or Mixed>"; for one cell "Front C2"; for a whole side "Full Front Touch · <value or Mixed>".
- **The footer** reads the preset description on page 1 and 2, "Zone C2" or "N Zones Selected" in a zone view, and the page label at the right ("Page 2/2 · Back Touch · N zones" on page 2). The small Clear button (and Whole surface in a zone view) sit in the footer band, 32 px high with a 48 px hit (the 4.1 touch controls). The hint row is Preset, Page, L1 / R1 and Shoulder (page 1 only), Zones, Clear and Back on a Summary page, and Move, Assign, Whole surface, Clear and Back in a zone view.
- **Home** shows the same zone counts for "Front touch" and "Rear touch" (`ui_controller_mapped_zones()`), and its Custom 1 to 3 open the page on that preset and make it the active one.
- **Circle** in a zone view returns to its Summary page; Circle on a Summary page returns to Home on the preset the page was on.
- **Flag 5 shipped:** the rear art is `vita/res/assets/controller_back_clean.png`.
- **The wave sidebar is removed** (`ui_navigation.c` and its state); the Controller was the last screen that drew it.

---

### 3.9 Splash (`splash.html`, issue #366) - approved by the CEO 2026-10-09
Built in #366 (`ui_splash.c`); awaiting the CEO's check on hardware.

Black screen, the VitaRPS5 logo big and centred, rebuilt from particles that fly in from around the screen, dim at first and brighter as they arrive. Fills the about 2.4 s of load before Home. Preview: `splash.html` (replay on click, scrub, Home fade toggle).

| Item | Spec |
|---|---|
| Logo art | `Vita_RPS5_Logo.png` (688x297, the same file the app ships at `vita/assets/Vita_RPS5_Logo.png` and draws in the TopBar C23), drawn at 640x276, centred (x 160, y 134). Background pure black (#000000), no wave, no top bar, no hints. |
| Particles | **980** in the build, sampled from the shipped logo with nearest-neighbour sampling (the mock gives 973 with the browser's smoothing; budget 800 to 1500). Sampled once at splash start from the logo rendered at display size: one random pick per 9x9 px cell, among pixels with alpha > 200 and red > 200 (so the thin dark outline is skipped), and only for cells where more than 35% of the pixels qualify. Fixed seed 366 (the mock's LCG) so every launch is identical. Each particle is a solid white square, 2 px (55%) or 3 px (45%), whole-pixel positions. |
| Start points | A random side of the four, from 10 px inside to 60 px beyond the screen border. |
| Motion | Time-based. Stagger delay 0 to 0.30 s (random), then a 0.60 s flight with `easeOutCubic`, so the last particle lands at 0.90 s. A sideways bow of up to 70 px either way that dies to 0 on arrival (`4k(1-k)*curve`, curve in -70..70). |
| Brightness | Alpha rises from 0.12 to 1.0 with the particle's eased progress. |
| Arrival | Each particle flashes 1 px larger and fades out over 0.12 s. |
| Logo | The crisp logo is drawn under the particles from the start. Its alpha is smoothstep(mean linear flight progress of all particles) raised to the power 1.3, and 1 from 1.0 s on. A bare stipple never shows. |
| Glow | None (removed from the first version). |
| Assembled | At 1.0 s. |
| Hold | The logo stays until loading is done. **About 2.4 s today** (log 19445058006). If loading ends sooner, the animation still plays to 1.0 s and never cuts off. |
| Exit | 0.3 s fade: splash opacity 1 to 0 over Home. |
| Skip | Any button or a tap jumps to the 1.0 s state. It never skips loading. |
| Draws | 3 per frame: black rectangle, logo, and all particles batched in one triangle list. |
| Frames during load | The splash is drawn from right after graphics init, with one frame between loading steps. Time-based, so a slow step only drops frames. Particle arrays are allocated once at splash start and freed at the end. |

## 4. Input model

Physical Vita buttons are mapped to logical actions once, at the top of the frame.

| Physical | Logical |
|---|---|
| Cross | `CONFIRM` (or `CANCEL` when Circle Button Confirm is on) |
| Circle | `CANCEL` (or `CONFIRM` when Circle Button Confirm is on) |
| Triangle | `OPTIONS` (consoles) / zone view and whole surface (Controller) |
| Square | `CLEAR` (digit, side, cancel login) |
| Start | `FILTER` (Home consoles), `QR` (login assist) |
| Select | Unused (the login assist's browser action was removed, #346; no Select toast either) |
| L, R | category or group or page, per screen. In-stream: hold L + R + Start to exit |
| D-pad | move focus; Left/Right also change choices |

Rules:
1. Swap applies to every screen and every glyph, including the hold-to-select on the zone view and the PIN screen. The hint row reads the setting; screens never test it.
2. Focus: one focused item per screen layer; a popup is modal and owns input. Up/Down do not wrap in lists or popups; a choice value wraps.
3. Hold-repeat on D-pad (today's helper). Hold timing for the exit combo is 1 s.
4. Touch: front panel mapped to screen pixels.
   - **Home rows (C02)**: first tap focuses, second tap activates (they open other screens). **Settings and Profile rows (C08)**: the first tap acts (toggle flips, choice cycles, action runs) and takes focus.
   - **Swipes** (Home: start anywhere and lock to one axis, #331; the others start only on the zones named; thresholds measured from the touch-down point, focus follows like the D-pad): Home list vertical, 96 px per row; Home category strip horizontal, 96 px per category (swipe left = next); page pane vertical, 48 px per row; list popup vertical, 48 px per row. A drag over zone cells paints a selection instead (C21).
   - A touch that moves more than 8 px is a swipe or a paint and never also a tap.
   - Tap outside a popup or the Options column cancels. Hit rects, visible rects and overlap priority are defined in 2.0; every hit rect is at least 48 x 48 (category icons 64).
5. Rear touch is only used by the Controller feature, never for UI.
6. Persist: Settings and Controller values save on change (today).

---

### 4.1 Touch parity (button hints hidden, or never tapped)

The hint row is tappable, but it can be hidden, so every action must be reachable by touch without it. Audit:

| Action (hint) | Touch path without the hint row |
|---|---|
| Confirm / Select / Toggle / Open / Connect / Pair | tap the row, button or value |
| Options (Triangle, Consoles) | **new: long-press (0.5 s) a console row**; the Options rows are tappable |
| Filter | tap the Filter row |
| Category L / R | tap a category icon or swipe the strip |
| Group L / R (pages) | tap a group |
| Preset (Controller) | tap the preset chevrons or the preset label |
| Page L / R (Controller summary) | **new: tap the page label** at the footer's right ("Page 1/2 · Buttons", hit 48 high) to switch pages |
| Back / Cancel (Circle) | pages and Controller: **new back chevron at the left of the title row (hit 48 x 48 at x 0..48)**; popups: tap outside; Options: tap outside; Connecting and PIN: Cancel button |
| Clear (Square), Controller | **new small Clear button** centred in the footer (hit 48 high) |
| Whole surface (Triangle), zone views | **new small Whole surface button** next to Clear |
| Enter code, Cancel login (Profile login) | **new two buttons** under the login panel (Enter code, Cancel login); QR toggle is the QR tap |
| PIN Clear digit, Register, Cancel | existing buttons |
| Stream exit | L + R + Start only (an overlay hint with its own setting, never a hint-row action) |

Items marked new exist so that nothing is reachable only through a hint tap. They are visible controls (the chevron and the small buttons stay visible even when hints are on); the long-press has no visible affordance, so Options also stays discoverable through the hint row.

**As built (#307).** Every row of the table above is now reachable by touch without the hint row: the long-press, the strip swipe, the list swipe and the shared back chevron are built (the other rows were built with their screens in #303 to #306). The long-press follows a "consumed touch" rule (`ui_input_consume_touch()`): only a long-press that acted (it opened Options) marks the touch used, so its release is not a tap. A finger held for 500 ms or more that nobody acted on (the Filter row, the empty state, a page) is still a tap when it lifts. A swipe is never a tap.

## 5. Copy deck

Wording changes: glyphs replace "X/O/Cross/Circle" text; "Streaming Settings" becomes "Settings"; "internet" badge becomes "Internet"; "PS5 Console Registration" is model aware; the old numbered login steps use glyphs; "Press X" labels become hint-row verbs; "Select toast" gone. Items marked (new) are not in today's app and are for CEO sign-off.

**Home / consoles**
- Categories: Consoles, Settings, Controller, Profile
- Verbs: Connect, Wake, Pair, Please wait, Options, Filter, Clear filter, Category
- States: Ready, Standby, Unavailable (new label), Error (new), Retrying (new), Please wait... ; route label: Internet
- Detail: Address, Route (Local Network, Internet, Local Network + Internet, Not reachable), Pairing (Paired, Unpaired), Unknown (new)
- Filter: Filter: "<text>" (N found); Start Filter
- Empty: Searching for consoles... ; No consoles match filter
- Banner (one line, only the reason may be shortened): Streaming stopped: <reason> - Please wait a few moments ; reasons: Console entered sleep mode, Console disconnected, or the raw reason
- Status messages (info panel only). **Error** (red): Wake signal failed. Check pairing and network. / Remote Play already active on console / Console Remote Play crashed - wait a moment / Missing console credentials. Re-pair may be required. / Enable PSN internet mode in settings. / PSN login required for internet remote play. / PSN session expired. Re-authenticate in Profile. / Could not determine host address. **Retrying** (amber): Wake signal failed; attempting connection anyway. / Console releasing session... ready in Ns / Console busy - retrying in Ns... / Waiting for console network link... / Video references unstable - requesting keyframe / Rebuilding stream at safer bitrate / Persistent video desync - rebuilding session / Packet loss burst - requesting keyframe
- Network Unstable
- Options: Connect, Wake and connect, Connect via, Re-pair, Pair, Change icon; header "Options"
- Connect via: title Connect via; rows Local Network, Internet
- Re-pair (new): Re-pair <name>? / You will need to enter a new 8-digit PIN from the console. / Cancel, Re-pair
- Change icon (new): Change icon; TV, Living room, Bedroom, Dorm, Office, Another place; hints Choose, Cancel

**PIN and results**
- <PS5|PS4> Console Registration; <name> (<ip>); Enter the 8-digit session PIN displayed on your <PS5|PS4>:
- Buttons: Clear digit, Cancel, Register; hints Digit, Change, Clear digit, Register, Cancel; prompt while the attempt runs: Pairing... (new)
- Results (new): Console paired / <name> is paired. You can connect to it now. / OK ; Pairing failed / <name> did not accept the PIN. Check the code on the console and try again. | <name> could not be reached. Check that it is on and on the same network. | <name> did not answer in time. Open Link Device on the console again and retry. / Close, Try again ; Could not connect / <name>: <reason> / Close, Try again

**Connecting**
- Titles: Waking Console, Starting Internet Remote Play, Starting Remote Play; hint Cancel; via Local Network, via Internet
- Stages and details: section 3.4
- Reconnecting: Optimizing Stream, Recovering from packet loss, Retrying at X.XX Mbps, Attempt N, Please wait...

**Stream**: Back to menu: Hold L + R + Start ; Stream Stats, Latency, FPS (N ms, in / target, N/A) ; Network Unstable

**Home list additions (round 8)**: Filter... ; Filter: "<text>" ; <N> found · [Square] to clear ; info panel: Filter / Find a console by name or IP address. / <N> consoles ; "<text>": <N> found of <M> ; hints Filter, Clear. **Touch controls (new)**: Clear, Whole surface (Controller), Enter code, Cancel login (Profile login), back chevron (no label).

**Settings**: group names Video, Network, Display, Appearance, Controls, Advanced; Appearance rows: Background, Theme, Background Blur (values as in 3.6; descriptions in `xmb-data.js`); row labels and values in section 3.6; descriptions (new, draft for sign-off): Quality Preset "Video resolution requested from the console."; Latency Mode "Sets the target bitrate. Higher looks better but needs a stronger connection."; FPS Target "Frame rate requested from the console."; Force 30 FPS Output "Output video at 30 FPS."; Fill Screen "Stretch the video to fill the whole screen."; Auto Discovery "Find consoles on your network automatically. Takes effect the next time the app starts."; Enable PSN Internet Mode "Connect to your consoles over the internet with your PSN account."; Show Latency "Show latency and frame rate in the stream overlay."; Show Network Alerts "Show a badge when the connection becomes unstable."; Show Exit Shortcut Hint "Show how to leave the stream when it starts."; Circle Button Confirm "Use Circle to confirm and Cross to go back, on every screen."; Clamp Soft Restart Bitrate "Limit the bitrate when the stream restarts after packet loss."; Motion during loss "Keep motion going while packets are lost. May show visual artifacts."; Enable Logging "Write diagnostic logs on the Vita for troubleshooting."; Show Button Hints (new) "Show the button hints along the bottom of menus."; Background Blur (new) "Blur the background waves behind menus. Strong and Dark are softer and calmer." with values None, Soft, Strong, Dark; On, Off; hints Toggle, Next, Change, Group, Back, Open

**Profile**: Account, Connection, PlayStation Network; identity block: PSN Account ID, PlayStation Network; Account ID, Not Set, Refresh Account ID ("Read the Account ID again from the system profile." new); Network Type (Local Wi-Fi, PSN Internet, Manual Host, Unavailable), Console (Not selected), Console IP, Status (Ready, Standby, Unavailable, None), Quality; PSN Auth; Disabled, Authenticated, Refreshing token, Awaiting browser sign-in, Token expired, Not authenticated, <error text>; Log in, Log out, Refresh hosts; Press [Confirm] again to confirm log out ; Phone Login Assist; 1 Press [Start] to show or hide the QR code; 2 Scan the QR code with your phone and sign in; 3 Press [Confirm] and paste the redirect URL or code; Code: Paste redirect URL/code; URL: my.account.sony.com/sso/ca/authorize (short form) ; QR hidden ; hints Enter code, QR, Cancel login, Confirm log out, Refresh
- Descriptions: "Enable PSN internet mode in Settings" (today); "Sign in with your phone. Needed for internet Remote Play." (new); "Reload your internet-capable consoles." (new); "Remove the saved PSN login from this Vita." (new)
- Toasts (all): Account ID refreshed from system profile / Could not refresh Account ID / PSN internet mode is disabled in Settings / Scan QR on phone, then press [Confirm] to paste the full redirect URL / PSN login could not start / PSN internet host list refreshed / PSN internet host refresh failed / PSN login canceled / QR shown. Scan it with your phone. (reworded from "QR shown...") / QR hidden. Press Start to show it again. (reworded) / Could not draw the QR code. Cancel and try again. / Could not open text input / No URL/code entered / PSN login complete / PSN login failed / PSN login removed
- System keyboard titles: Filter Consoles; Paste full redirect URL

**Controller**: Controller; Front Touch; Rear Touch; Custom 1/2/3; Your first / second / third custom mapping; Page 1/2 · Buttons; Page 2/2 · Back Touch; N zones; Zone <A1..F3>; N Zones Selected; callouts "L1 -> <output>", "R1 -> <output>"; popup titles and subtitles in section 3.8; outputs Options, Share, Touchpad, L1, L2, L3, R1, R2, R3, PS, None; hints Preset, Page, L1 / R1, Shoulder, Zones, Clear, Back, Move, Assign, Whole surface, Assign, Cancel

---

## 6. Flags for CEO

Decided (no action): red error text stays as is (CEO: reads fine); Background Blur defaults to None; Roboto Mono dropped; rounded shapes;  Show Navigation Labels dropped; the third Profile card was dead code and is removed; Triangle / Cross on page 2 open the zone views; Roboto Light is loaded; top bar keeps Wi-Fi, battery and clock, with no time-of-day tint; the internet title no longer flips mid-flow; overlay deviations accepted (fixed stats slot, 16 px margin, 3.0 s toast); Profile streaming metrics removed; result popups kept.

Open:
1. **Show Latency default and scope (CEO pending).** With Profile metrics gone, Show Latency only controls the stream overlay stats panel. Default stays off (today); say so if you want it on, or if it should be renamed (for example "Show Stream Stats").
2. **Pairing results need backend work.** The result popups need registration to report finished OK, PIN not accepted, console unreachable or timeout to the UI (3.3). Today there is no success or failure UI and the registration code may not distinguish these. Timeout length proposed at 30 s. If the code cannot tell "unreachable" from "PIN not accepted", collapse to one failure copy.
3. **Login URL shown in a short form** (`my.account.sony.com/sso/ca/authorize`). Today the app prints up to 2 lines of the full authorize URL and silently drops the tail; the full URL stays in memory for the QR. Typing the full URL by hand was never practical.
4. **Profile Status is a deliberate fix.** Today it says "Ready" or "Ready / Not Registered" even for a console that is asleep or unreachable. The spec shows the console's real state (Ready, Standby, Unavailable, None) in the same words as the Home list. Everything else in the Connection group matches today's code.
5. **Rear controller art edited (shipped in #305 as `vita/res/assets/controller_back_clean.png`).** `controller_back_clean.png` removes the tiny "Sony Computer Entertainment Inc" line (unreadable at that size, off-brand for our app). Ship the clean copy; the original stays in `assets/`.
6. **New copy needs sign-off**: all Settings descriptions, Profile action descriptions, Re-pair confirm, Change icon popup, the pairing and connection result popups, the three pairing failure reasons.
7. **Room icon needs storage**: one small integer per console in the host config (default TV).
8. **PIN title and prompt are model aware** ("PS4"/"PS5"); today they say PS5 always.
9. **Unpaired consoles** no longer appear on Home (#330), so they have no Options. Cooldown disables Connect and Connect via.
10. **Console type is only in the detail panel**, not on list rows (locked). The earlier "Last session" line is removed (the app has no such data).
11. **System reads for the top bar**: Wi-Fi state, battery percent, local time, polled about once per second.
12. **Errors left the list; transient states are not errors (my call, please confirm).** Rows show only a status label. New statuses: **Error** (red) for failures that need the user, **Retrying** (amber) for things the app is already retrying or waiting on. Classification: Error = wake failed (check pairing), Remote Play already active, Remote Play crashed, missing credentials, PSN mode off, PSN login required, PSN session expired, no host address. Retrying = wake failed but connecting anyway, console releasing session, console busy, waiting for network link, and the four stream-recovery messages. "Console releasing session... ready in Ns" could read as "Waiting"; I kept one amber label. Two related changes: **Cooldown (Please wait...) is now amber**, not red, because it is transient; **Unavailable** now has a label (it had none; Profile already used the word) and is deliberately neutral (grey dot and label, room icon at 55% opacity) so it cannot be confused with Error.
13. **Build note: two new config fields** in the config TOML, both read at startup and written on change like every setting: `background_blur` (integer, 0 None, 1 Soft, 2 Strong, 3 Dark; default 0) and `show_button_hints` (boolean; default true / 1). None is the default blur (CEO decision, round 7); Soft is the recommended trade if the CEO wants a legibility gain (FEASIBILITY.md section 8).
14. **Type and shape changes (round 7).** Roboto Mono is gone (5 faces from Light and Regular at that time, 6 since #329; T14 for the hint row already exists in today's atlas; T40 Light is a new face for the PIN digits). Rounded shapes need baked 9-slice and 3-slice textures; see FEASIBILITY.md for the draw-call and texture cost.
15. **Round 8 changes to confirm.**
    - **Console rings and badges are gone** from the list: a console is its flat white room icon (38 px) plus the status dot and label. Unavailable reads by its neutral dot and label with the icon at 55% (Unpaired is no longer a Home state, #330); check it reads at a glance on the device. The big ring on the Connecting screen is kept as a progress indicator, without a badge; it looks fine next to the plain icons, but say if you want it dropped too.
    - **Item icon size 38 px** (about 21% smaller than the 48 px category icons) for consoles, settings groups, Profile groups, presets and Filter. 11 new item icons plus the magnifier (`icons/`), about 0.07 MB of atlas. The page group lists stay text-only.
    - **Item titles smaller** using existing atlas entries only: focused T20 Light, unfocused T16 Regular, status T16 / T14. No atlas growth. Rows are 64 high with a 16 px gap after the focused row, so the list is denser (4 full rows plus a faded fifth preview, instead of 3 plus a preview).
    - **Filter is a list item** (first row, shown only with more than 4 consoles or an active filter, on purpose: with 4 or fewer consoles there is nothing worth filtering and Start still works); Start stays a shortcut but is no longer in the hint row; the old separate filter line and its hint above the logo are gone.
    - **Show Button Hints** setting (default On). New config field `show_button_hints` (boolean, default true), listed with `background_blur` in flag 13.
    - **Touch parity (4.1) added new controls**: long-press for Options, a back chevron on pages and the Controller, Clear and Whole surface buttons on the Controller, two buttons under the Profile login panel. Confirm you want them visible even when hints are on.
16. **Mock-only affordances** (not app behaviour): Esc leaves the stream; the toolbar Circle toggle; the pairing trigger digits (first digit 0 or 9); the drawn stand-in for the stream picture (a CSS gradient scene, no photo); the illustrative QR; the stand-in for the system keyboard.
17. **Rear touch seeds did nothing in the stream (resolved by #319, was #305).** The seeded rear L2/R2 sat on quadrant inputs the stream never reads, and the page and the stream seeded a never-saved preset differently. Now one shared default in `vita/src/controller.c` puts L2 on the rear grid's left three columns and R2 on the right three for both; the old shoulder+rear-half L2/R2 combo is dropped, and presets already saved with the old seed are not repaired. New copy still to sign off: the "+" label of a Mixed cell.
18. **#330 Pair new device: resolved by the CEO, 2026-10-09.** Decided: (1) Show Only Paired is removed, the Settings row and its config key; (2) the Unpaired row state is removed from Home and kept only in the popup; (3) discoverability: keep the `[Up] Pair new device` hint and judge on the Vita; moving the item to the last row is the fallback if it still feels hidden; (4) with 5+ consoles the item sits above the Filter row; (5) the PS4 instruction line is added, as one line, and its copy is approved; (6) Enter IP address sends a unicast discovery probe and waits 5 s; (7) Cancel on the PIN screen goes to Home with the item focused; (8) Auto Discovery off shows a note in the popup; (9) the new copy is approved, with the shortened PS4 line; (10) the item icon is a plus in a ring; (11) the popup Filter row shows above 4 found consoles, or while a filter is active; (12) order is alphabetical by host name, case-insensitive, IP as tie-break (owner first is impossible, discovery has no owner field); (13) L and R page the popup list; (14) Re-pair stays under Triangle. Still to verify: the item's discoverability on hardware (3), and the backend needs the probe and the discovery list (6).

**Pair new device (#330, copy approved 2026-10-09)**: item: Pair new device, <N> found nearby, Searching..., Link a console; detail: Link a PS5 or PS4 to this Vita. Turn the console on and join the same network., Paired, <N> console(s), Found nearby, <N> unpaired, Searching..., None, Discovery off; empty: No paired consoles yet; hint: Pair, Pair new device. Popup: Pair new device / Found on your network / Searching your network... / No unpaired consoles found / Auto Discovery is off. Turn it on in Settings > Network, or enter the IP address. / Enter IP address / Filter... / Filter: "<text>" / <N> of <M> / <N> found / Clear / No consoles match filter / keyboard title Filter Found Consoles / hints Pair, Enter IP, Filter, Clear, Page, Close. IP flow: keyboard title Console IP address; Looking for console / Contacting the console... / Cancel; Console not found / Nothing answered at <ip>. Check the address, and that the console is on and on the same network.; Not an IP address / Use four numbers separated by dots, like 192.168.1.20.; Close, Try again.
