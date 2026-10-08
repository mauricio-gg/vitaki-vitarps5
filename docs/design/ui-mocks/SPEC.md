# VitaRPS5 XMB build spec (issue #271, round 6)

Baseline for engineers. The HTML mock in this folder is the visual reference (`xmb.html`, deep links in section 3); this file is the contract. Where they disagree, fix the mock. Native 960x544, one 8 px grid. Nothing here is built yet.

- Theme source of truth: `tokens-xmb.css`. Component CSS: `xmb.css`. Mock logic: `xmb-app.js` (read it for exact behaviour).
- vita2d cost numbers: `FEASIBILITY.md`. Draw-call budget stays about 80 per frame on Home.
- Round 6 scope changes applied: Logs category removed; Add item removed; in-stream has no menu (today's overlay only); triangle options per console; one result/error popup; re-pair asks first; everything works by touch; persistent hint row; Settings grouped; "Show Navigation Labels" dropped (Flags section).

---

## 1. Theme and tokens

One header (`ui_theme.h`). No colour or size literal anywhere else. Alpha is part of the token.

### 1.1 Colour

| Token | Value | Used for | Replaces (today) |
|---|---|---|---|
| `TEXT` | #FFFFFF | focused text, values, titles, glyph tint | `UI_COLOR_TEXT_PRIMARY` |
| `TEXT_2` | #DDE3F0 | labels, unfocused rows, hint row labels, body copy | `UI_COLOR_TEXT_SECONDARY` |
| `TEXT_3` | #B3BDD2 | captions, disabled labels, empty text, mono hints | `UI_COLOR_TEXT_TERTIARY` |
| `OK` | #4EE09A | Ready dot and ring, success icon, authenticated, loss "Stable" | `UI_STATUS_ACTIVE`, green literals |
| `WARN` | #FFBE4D | Standby, cooldown banner rule, armed log out, loss amber | amber literals |
| `ERR` | #FF8080 | errors, unavailable, red hint lines, Network Unstable dot, loss red | `UI_STATUS_ERROR`, `RGBA8(0xF4,0x43,0x36)` |
| `INTERNET` | #A9B2FF | "Internet" route label | new |
| `IDLE` | #C9D0DF | Unpaired ring and dot | grey literals |
| `ACCENT` | #6DB4FF | mapped touch zones only (fill at 28%, border at 60%) | `UI_COLOR_PRIMARY_BLUE` (#3490FF), removed everywhere else |
| `GLOW` | #FFFFFF @ 70%, radius 12 | focus glow on text, icons, ring | new (baked texture) |
| `PANEL` | rgba(6,12,28,.92) | popups, toast, options column ramp, stats panel | `UI_COLOR_CARD_BG` |
| `SCRIM` | rgba(2,5,14,.55) | behind popups | popup dim literals |
| `SCRIM_STRONG` | rgba(2,5,14,.82) | system keyboard stand-in only (not drawn by the app) | n/a |
| `HUD` | rgba(6,10,22,.60) | overlay pill and badges | `RGBA8(0,0,0,180/200)` |
| `PAGE_WASH` | rgba(3,6,16,.60) | dims the wave behind pages, Controller, PIN, Connecting | new |
| `FILL_FOCUS` | white @ 12% | focused row, focused button | blue focus rings |
| `FILL_ON` | white @ 26% | toggle on, selected zone, cursor zone | blue fills |
| `LINE` | white @ 26% | page rule, popup and toast border, button border | grey 1 px lines |
| `LINE_FAINT` | white @ 14% | row dividers, kv rows | grey 1 px lines |
| `BG_*` | wave palette below | background | `UI_COLOR_BACKGROUND`, particles |

Alpha levels in use (the whole set): 12, 14, 26, 28 (zone), 55, 60, 82, 92 percent. Everything else is opaque.

Wave palette (time of day from the RTC; 4 sets, each: top, mid, bottom, 3 ribbon RGBs, horizon glow). `night` #04081A/#0B1634/#16275A, `dawn` #150D2C/#321A4A/#6A2D5C, `day` #06204A/#0F4585/#245F9C, `dusk` #1A0C1C/#4A2018/#A8502A. Hours: 21-5 night, 5-9 dawn, 9-17 day, 17-21 dusk. Exact ribbon RGBs are in `xmb-wave.js`.

124 hard-coded RGBA8 literals in today's code all collapse into the table above.

### 1.2 Type

Roboto and Roboto Mono, pre-rendered. **4 sizes, 6 faces** (today: 7 sizes, 2 families).

| Face | Size / line | Weight | Used for | Replaces |
|---|---|---|---|---|
| `T28` | 28 / 32 | Light 300 | page titles, focused list row name, popup title, Connecting current stage, focused group | `FONT_SIZE_HEADER` 28, `HOME_HEADER` 24 |
| `T20` | 20 / 24 | Light 300 | list row name, setting label and value, buttons, popup rows, callouts, prompts | `CARD_TITLE` 20, `SUBHEADER` 18 |
| `T16` | 16 / 24 | Regular 400 | hints, captions, status lines, descriptions, kv rows, pills, toast sub | `BODY` 16, `SMALL` 14 |
| `MONO16` | 16 / 24 | Regular | IDs, IP, codes, URL, kv values, stats values | Roboto Mono |
| `MONO28` | 28 / 40 | Regular | Reconnecting retry bitrate | new |
| `MONO40` | 40 | Regular | PIN digits | `PIN_DIGIT` 40 |

Roboto Light is not loaded by the app today (only Regular and Mono). Adding it costs one TTF (170 KB) and about 0.3 MB of atlas. See Flags.

### 1.3 Spacing, layout, lines, motion

| Token | Value |
|---|---|
| Grid | 8 px. Scale `S1..S6` = 8, 16, 24, 32, 40, 48 |
| `MARGIN_X` | 48 (left and right of every page, top bar, hint row) |
| `TOP_Y` | 16 (top bar, height 32) |
| `TITLE_Y` | 64 (page title row, height 48) |
| `RULE_Y` | 120 (page divider) |
| `BODY_Y` | 136 (page content top) |
| `HINT_Y` | 496 (hint row, height 48) |
| `ROW_H` | 48 (setting row, popup list row); 56 for group, options and list-row base; 64 icon box |
| `TAP_MIN` | 48 (every tap target is at least 48 x 48; chevrons are 48 x 48 boxes around 16 px art) |
| `LW1`, `LW2` | 1 px (hairlines, borders), 2 px (ring, focus underline, toggle, spinner) |
| Radius | 0 for every rectangle. Only rings, dots, spinners and the toggle-free badge are round |
| `D1`, `D2`, `D3` | 150, 300, 600 ms. Easing `cubic-bezier(.22,.7,.2,1)` (ease-out), linear for spinners. Toggle knob keeps today's 180 ms. List cascade: row i starts after 45 ms x min(i,6). Spinner 1.8 s/turn (ring) and 0.8 s (inline 16 px). Reduce motion is not a Vita concept; skip |
| Overlay timers | exit hint 5.0 s visible + 0.5 s fade (today's values); unstable badge 5.0 s (today); toast 3.0 s + 0.3 s fade (today 2.0 s, see Flags); log out confirm window 3.0 s (today) |

Today's tokens that go away: `PRIMARY_BLUE` and every blue focus ring, `card_with_shadow` (all shadows), rounded radii 6/8/10/12, 6 modal sizes, `NAV_*` constants (side nav deleted), particles (replaced by the wave).

---

## 2. Component catalogue

### 2.0 Pattern (applies to every component)

Each component is a plain C struct plus three functions. No inheritance, no vtables, no callbacks stored per frame, no allocation after init.

```
typedef struct { ...data..., ...state... } UiThing;
void ui_thing_init(UiThing*, const UiThingSpec*);        // set data, compute rects once
void ui_thing_draw(const UiThing*, const UiTheme*);      // no state change, no allocation
UiEvent ui_thing_input(UiThing*, const UiInput*);        // returns NONE / MOVED / ACTIVATED / CANCELLED
```

`UiInput` is one per-frame snapshot: logical buttons `pressed/down/released` (already swapped by Circle Button Confirm, section 4), D-pad repeat, and touch `{x, y, down, pressed, released, dragged}` mapped to screen pixels. Screens own their components as struct members and forward input to the focused one. Draw order is the screen's responsibility. Rects are stored in the struct at init so a hit test and a draw use the same numbers (fixes today's mapping popup, where touch geometry does not match the drawn rows). Text is drawn from the 6 pre-rendered faces; focus glow and rings are baked textures tinted at draw time (FEASIBILITY.md).

### C01 CategoryBar
| | |
|---|---|
| Purpose | Horizontal Consoles / Settings / Controller / Profile row. Replaces the wave sidebar. |
| Anatomy | Icon box 64 (art 48). Centre y 104. Focused centre x 256, scale 1.25, glow, label T16 centred 16 below the box (y 152-176). Right neighbours at x 384, 496, 608 (focused + 128, then +112). Left neighbours at 256 - 112 per step, scale 0.75. Unfocused opacity 62%. Icons: `icon_play.png`, `icon_settings.png`, `icons/controller.svg`, `icons/profile.svg`. |
| States | focused, unfocused, dimmed to 12% while Options column is open |
| Input | Left/Right or L/R changes category (slide 300 ms). Touch: tap an icon focuses it; horizontal swipe over the bar moves one step. Tap target 64 x 64. |
| Used by | Home |
| Replaces | `ui_navigation.c` wave sidebar and `NAV_*` |

### C02 XmbList (+ ListRow)
| | |
|---|---|
| Purpose | Vertical item column under the focused category. |
| Anatomy | Viewport x 224, y 184, w 384, h 312, bottom 48 px fades out. Row: icon box 64 at x 224 (scale 0.8 unfocused, 1.1 focused), text at x 304, max w 272. Focused row top y 192, height 88; unfocused height 56; +24 when the row has a hint line. Row gap 24. Name T28 focused / T20 unfocused, status line T16, hint line T16 in `ERR`. Rows above the focus slide up 80 px per step and fade to 0. Cascade-in on category change. |
| States | focused, unfocused, dimmed (opacity 55%, cooldown), hidden (above focus) |
| Input | Up/Down moves (no wrap). Cross or tap on the focused row activates. Tap on another row focuses it, second tap activates. Vertical swipe scrolls one row per 56 px. Tap target = the row, 56 high. |
| Used by | Home (consoles, setting groups, presets, profile groups) |
| Replaces | console card list (`ui_console_cards.c`), 4 duplicated card+focus-ring pieces |

### C03 StatusRing
| | |
|---|---|
| Purpose | Console identity and state in one glyph: user room icon inside a status ring plus corner badge. |
| Anatomy | Size S (56 in rows). 2 px ring in the state colour, 12 px soft glow, room icon at 50% of S, badge circle at 40% of S at (-4,-4) from the bottom-right, 2 px border in the state colour, glyph inside at 70%. Unpaired ring is dashed with no glow. Room icons: tv (default), sofa, bed, bunk, desk, house (`icons/*.svg`). |
| States | Ready (OK ring, check badge), Standby (WARN, moon), Unpaired (IDLE dashed, lock), Internet (OK ring, globe badge), Unavailable (ERR, warning), Cooldown (ERR, clock; row dimmed) |
| Input | none (display) |
| Used by | Home list, Connecting art (S 128) |
| Replaces | card status dot, "internet" badge, PS5/PS4 logo on card |
| Cost | ring and badge are baked textures tinted per state, 3 draws per item |

### C04 DetailPanel
| | |
|---|---|
| Purpose | Facts about the focused item, no card. |
| Anatomy | x 608, y 192, w 304. Console: type logo (`PS5_logo.png` 176 x 48, `ps4.png` crop 200 x 48), white, 16 below it the name T28, status line T16 (dot + text), 16 gap, kv rows h 32 (label T16 TEXT, value MONO16 right, `LINE_FAINT` hairline): Address, Route, Pairing. Settings group: the group's rows and values. Preset: L1, R1, Front touch, Rear touch. Profile group: its first 4 rows. |
| States | per item kind; re-animates (rise 300 ms) when focus changes |
| Input | none |
| Used by | Home |
| Replaces | the in-card IP/status text |

### C05 OptionsColumn
| | |
|---|---|
| Purpose | Triangle options for a console. |
| Anatomy | Slides in from the right, w 352 (x 608-960), left edge feathered into `PANEL`. Padding 48. Console name T28 at y 88, "Options" T16 at y 120, rows from y 160, h 56, T20, 1 px `LINE_FAINT` divider. Focused row: TEXT, 2 px white underline, glow. Home dims behind it. |
| Items | Connect (Wake and connect on standby) / Connect via (only when Local and Internet both exist) / Re-pair / Change icon. Unpaired consoles get Pair / Change icon. Cooldown disables Connect and Connect via. |
| Input | Triangle opens, Triangle or Circle closes. Up/Down, Cross. Touch: tap a row; tap outside closes. |
| Used by | Home (Consoles) |

### C06 HintRow
| | |
|---|---|
| Purpose | Persistent bottom row. Replaces the Select toast. |
| Anatomy | y 496, h 48, x 48..912, gap 32. Each hint: glyph (h 24) + 8 + label T16 TEXT_2. Optional right slot (margin-left auto) holds the Network Unstable pill. Glyphs: the 4 PNG symbols plus baked flat glyphs for D-pad (all, left-right, up-down), L, R, L+R, Start, Select (new assets, 24 px high). |
| Confirm swap | Hints are declared as `CONFIRM` / `CANCEL`; the row resolves them to Cross or Circle from the setting, so every screen swaps with one switch. |
| States | normal, dim (45%, action not available) |
| Input | Touch: tapping a hint triggers the same logical action (tap target = hint width x 48). |
| Used by | every screen except in-stream; popups replace the row contents while open |
| Replaces | Select hints toast, per-screen hint strings, `NAV_TOAST_*` |

### C07 PageShell
| | |
|---|---|
| Purpose | Common frame for Settings, Profile, Controller, PIN, Connecting, Reconnecting. |
| Anatomy | Top bar (C23) always. Title row y 64 h 48: icon 32, gap 16, title T28 at x 48. `LINE` rule at y 120, x 48..912. Wave dimmed by `PAGE_WASH`. Optional right slot in the title row (Controller preset switcher). Settings/Profile body: group list x 48 w 256 from y 136, rows h 56, T20 (focused-active group T28 + glow; current group has a 2 px white bar at its left edge); pane x 336 w 576 y 136 h 288 (6 rows); description x 336 y 440 w 576 h 48, T16, max 2 lines; scroll indicator (C24). |
| Input | Up/Down, Left/Right moves between group list and pane; L/R switches group; Circle returns (pane to groups, groups to Home). Touch: tap a group, tap a row, swipe the pane. |
| Used by | Settings, Profile, Controller, PIN, Connecting, Reconnecting |

### C08 SettingRow (+ C09 Toggle, C10 ChoiceValue)
| | |
|---|---|
| Purpose | One row: label left, control or value right. Kinds: toggle, choice, info, action. |
| Anatomy | h 48, pad 16, label T20, `LINE_FAINT` divider. Focused: `FILL_FOCUS`, TEXT, white divider, glow on the label. Disabled: 50%. Armed (log out): label and value in `WARN`. **Toggle**: 48 x 24 track, 2 px border, 16 x 16 knob inset 2, travel 24, off = TEXT_2, on = white border + `FILL_ON`, label "On"/"Off" T16 (24 wide) 16 right of the track. **ChoiceValue**: chevron boxes 48 x 48 (art 16, 50% opacity, 100% when row focused) around a 224 wide centred value T20. **Info**: value T20 or MONO16, colour by status. **Action**: label only, chevron-right 20 when enabled. |
| States | normal, focused, pressed (FILL_ON for 150 ms), disabled, armed |
| Input | Toggle: Cross or tap flips. Choice: Left/Right or tap the chevrons; Cross or tap elsewhere = next, wraps. Action: Cross or tap. Tap on an unfocused row focuses it first only for info rows; toggle/choice/action act on the first tap and focus. |
| Used by | Settings, Profile |
| Replaces | settings list rows, `toggle_switch` (kept, restyled), `dropdown` (replaced by ChoiceValue), Profile text rows |

### C11 PopupShell
| | |
|---|---|
| Purpose | The only modal frame. 3 fixed sizes, all 480 wide, centred horizontally at x 240. |
| Sizes | **S** 480 x 256 at y 144 (confirm, result, 2-row list). **M** 480 x 352 at y 96 (icon picker grid). **L** 480 x 432 at y 56 (mapping list, 6 visible rows). |
| Anatomy | `PANEL` fill, 1 px `LINE` border, padding 32. Title T28 (optional 32 px icon, gap 16), subtitle T16, body T16 (8 below the title/sub). `SCRIM` behind. Button bar (S only): TextButtons h 48, right aligned, gap 16, each up to 208 wide, anchored to the bottom padding. Enter: rise 16 px + fade, 300 ms. |
| Input | Modal: the popup owns input. Circle or tap on the scrim = cancel. Tap target for rows 48 high. |
| Used by | all popups |
| Replaces | 6 modal sizes (520x280, 560x290, 400x160, 360x340, 700x450, 640x360/380) |

### C12 ListPopup
Rows h 48 (T20, optional right label T16 TEXT_3, optional check icon for the current value), `LINE_FAINT` dividers, focused row `FILL_FOCUS` + 2 px underline. Scrolls when rows > visible (L shows 6), selected row kept near the middle, 2 px scroll indicator on the right edge. **Grid variant** (icon picker): 3 x 2 cells, each 128 x 104, gap 16, icon 48 (70% opacity, 120% scale and glow when focused), label T16, current value marked by a 2 px underline. Input: Up/Down (grid: all four), Cross, tap a row (selects and activates), drag-scroll vertically. Used by: Mapping popup (L), Connect via (S, 2 rows), Change icon (M grid). Replaces: the mapping popup (whose touch geometry is wrong today), long-press connect-via popup.

### C13 ConfirmPopup
Size S, title (with icon), 1 to 3 lines body, two TextButtons (Cancel left, action right). Default focus Cancel. Left/Right switches, Cross confirms, Circle cancels. Used by: Re-pair.

### C14 ResultPopup
Size S, tone icon (OK check or ERR warning, 32), title, 1 to 3 lines body, one or two TextButtons. Default focus on the right-most (primary) button. One component for pairing success, pairing failure and connection failure. Used by: pairing result, connection failure. Replaces: the debug-only error popup.

### C15 Toast
h 48, top y 432, centred, padding 24, optional 24 px icon (OK check or ERR warning) + 16 gap, T20, `PANEL` fill, 1 px `LINE`, max w 720, single line. Rise 300 ms, 3.0 s, fade 300 ms. One at a time (a new one replaces). Used by: Profile messages. Replaces: 4 pill/toast implementations.

### C16 Spinner and ProgressSteps
**Spinner**: 2 px ring arc, 270 degrees, TEXT, ring-size 176 (Connecting art) or 16 (inline, current step). **ProgressSteps**: x 464..912, from y 152. Row min h 40, padding 8 vertical, gap 16, T20. Done: 12 px `OK` dot. Current: T28 with detail line T16 TEXT_2 under it, glow, 16 px spinner in the marker column. Pending: MONO16 number in TEXT_3. Only the stages that apply to the flow are listed. Used by: Connecting. Replaces: `ui_draw_spinner` card layout.

### C17 PinField
Eight digit boxes 56 x 72, margin 4 (gap 8), MONO40, 2 px underline (TEXT_3; focused white + `FILL_FOCUS` + glow). Focused box shows up/down chevron boxes 56 x 40 above and below (art 20). Empty focused box shows a blinking 2 x 40 cursor (1 s). Row top y 232 (chevron top), so digits sit at y 272-344, centred horizontally. Input: Left/Right moves, Up/Down changes the digit (empty -> 0 on Up, 9 on Down, wraps), Square clears the digit. Touch: tap a box, tap its chevrons. Used by: PIN screen. Replaces: `pin_digit`.

### C18 QrPanel
QR art 160 x 160 on a #FAFAFA plate with 8 px quiet zone (176 x 176 box), modules from `ui_qr`. Hidden state: 176 box at 55% black with "QR hidden" T16 TEXT_3 centred. Beside it a text column (steps, code, URL). Used by: Profile login assist. Replaces: `draw_profile_login_assist_panel` (reuse the `ui_qr` encoder unchanged).

### C19 Pill
h 32, padding 0 16, `HUD` fill, T16 TEXT, square. Variants: **warn** (2 px `WARN` left rule: cooldown banner), **unstable** (12 px `ERR` dot, pulse 1.4 s: Network Unstable), **plain** (exit hint). Used by: stream overlay (exit hint top-right, Network Unstable bottom-right at 16 px margins), hint-row right slot (Network Unstable on menus), top bar centre (cooldown banner). Replaces: overlay `draw_pill`, `UI_LOSS_INDICATOR_*`.

### C20 ControllerDiagram + Callout
Diagram image (`controller_front.png` 874 x 396, `controller_back.png` 720 x 327) tinted white (front inverted; back as is). Summary front: x 164, y 176, w 630. Summary back: x 170, y 176, w 620. Zone views: front x 120, y 144, w 720; back x 140, y 152, w 680. Callout: h 32 at y 136, T20, 1 px underline (focused: 2 px white + glow), text "L1 -> <output>" with the arrow glyph; left callout x 64 w 136, right callout right edge 896; 1 px leader line from the callout centre to the shoulder (10% from the top edge, 10% / 90% from the sides of the image). Footers: T16, y 464, h 24, left x 48 (preset description or zone name), right x 912 (page label). Input: Up/Down picks L1/R1, Cross opens its popup; touch: tap a callout or the diagram. Used by: Controller. Replaces: procedural diagram drawing is kept as is (`ui_controller_diagram.c`); only chrome changes.

### C21 ZoneGrid
6 x 3 cells (columns A-F, rows 1-3) over the screen rect (front: x +178/874, y +30/396, 526 x 298 source px) or the rear pad rect (x +139/720, y +45/327, 444 x 188 source px), scaled with the diagram. Cell: 1 px border white 22%, label T16 (OPT, SHR, TP, L1...; blank for None), mapped = `ACCENT` 28% fill + 60% border, cursor = white border + `FILL_ON` + inner glow, picked = `FILL_ON`. Input: D-pad moves; hold Confirm + move adds cells to the selection (popup on release); touch: finger paint across cells, backtracking one cell removes the last, release opens the popup; tap = one cell. Used by: Controller summary page 2 (read-only display; tap opens Rear zones), Front and Rear zone views.

### C22 TextButton
h 48, min w 128, padding 0 24, T20, 1 px `LINE` border, no fill. Focused: `FILL_FOCUS`, white border, glow. Disabled: 45%. Pressed: `FILL_ON`. Used by: popups, PIN (Clear digit, Cancel, Register), Connecting (Cancel). Replaces: `text_button`.

### C23 TopBar
y 16, h 32, x 48..912: logo (h 32, `Vita_RPS5_Logo.png`) left; right group Wi-Fi icon 24, battery icon 24 + percent T16, clock T20. Centre slot: Pill for the cooldown banner (Home only). Hidden in-stream. Needs Wi-Fi, battery and RTC reads (Flags).

### C24 ScrollIndicator
2 px track (`LINE_FAINT`) with a 2 px white thumb, at x 920 beside the pane (page) or the right edge of the list popup. Shown only when rows exceed the viewport. Replaces: 2 scrollbar implementations.

### C25 StatsPanel
`PANEL` fill, padding 8 x 16, min w 176, at right 16, top 64. Title "Stream Stats" T16 TEXT_2, rows (label T16 TEXT_3, value MONO16 white right aligned): Latency "N ms" ("N/A" when stale), FPS "in / target". Shown only when Show Latency is on. Replaces: `draw_stream_stats_panel`.

### C26 EmptyState
Single line T20 TEXT_2 at x 304, y 208, with a 16 px inline spinner for Searching. Used by: Home consoles.

### C27 Background (wave)
5 ribbons, 36 dust points, palette by time of day, CPU vertex update at 30 Hz (freeze or halve while Connecting). FEASIBILITY.md has the costing. Replaces: `ui_particles`.

### C28 FilterLine
x 608, y 144, h 32, T16 TEXT_2. Either `[Start] Filter` (when more than 4 consoles) or `Filter: "text" (N found)` plus a 32 x 32 clear box (x glyph). Tap opens the keyboard; tap the box clears.

---

## 3. Screens

Deep links: `xmb.html#<id>`. The jump menu in the mock lists all of them. Button names are logical (Confirm and Cancel swap, section 4).

### 3.1 Home (XMB)
Layers: C27, top bar C23, C01, C02, C04, C28, C05, hint row C06. Category row y 104, list from y 192, detail x 608.

| Category | Items | Detail | Confirm |
|---|---|---|---|
| Consoles | console rows, registered first, then by name | C04 console | per state, below |
| Settings | Video, Network, Display, Controls, Advanced | the group's rows and values | opens Settings page at that group |
| Controller | Custom 1, Custom 2, Custom 3 (+ description line) | L1, R1, Front touch, Rear touch | opens Controller summary on that preset |
| Profile | Account, Connection, PlayStation Network | first rows of the group | opens Profile page at that group |

Console states (rows and detail):

| State | Dot / ring | Status text | Confirm does | Deep link |
|---|---|---|---|---|
| Ready | OK / check | Ready | Connecting (local) | `#home` |
| Standby | WARN / moon | Standby | Waking, auto-connects | `#consoles-standby` |
| Unpaired | IDLE dashed / lock | Unpaired | PIN screen | `#consoles-unpaired` |
| Internet only (PSN, valid token) | OK / globe | Ready, "Internet" in `INTERNET` | Connecting (internet) | `#consoles-psn` |
| Unavailable (not discovered, no route) | ERR / warning | none | tries to connect, result popup on failure | `#consoles-unavailable` |
| Cooldown | ERR / clock, row 55% | Please wait... | nothing; Connect hint dimmed; banner in the top bar | `#consoles-cooldown` |

Per-console hint lines (T16 `ERR`, under the status line, row grows 24 px): `#hints` shows three. Full list in the copy deck. Network Unstable shows in the hint row's right slot on any menu (`#home-unstable`). Empty: `#empty-searching`, `#empty-nomatch`. Filter: `#filter`; Start opens the system keyboard (`#keyboard`), or clears an active filter.

Button map: Up/Down item, L/R or Left/Right category, Confirm activate, Triangle options (consoles), Start filter (Consoles with more than 4 consoles or an active filter). Hints: `[Confirm verb] [Triangle Options] [Start Filter|Clear filter] [L R Category]`; verb = Connect, Wake, Pair, or "Please wait" (dim). Touch: tap category, tap row (focus then activate), swipe, tap hint, tap filter line.
Transitions: category change slides the bar 300 ms and cascades the list; focus change slides rows 300 ms; detail rises 300 ms.

Sub-screens from Home:

| Id | What | Component |
|---|---|---|
| `#options`, `#options-unpaired` | Triangle options column | C05 |
| `#icon-picker` | Change icon, 3 x 2 grid | C11 M + C12 grid |
| `#connect-via` | Connect via: Local Network (IP) / Internet (PSN) | C11 S + C12 |
| `#repair` | "Re-pair <name>?" Cancel / Re-pair | C13 |
| `#keyboard` | system keyboard "Filter Consoles" (stand-in dim + caption; the real IME is the Vita's) | stand-in |

Categories deep links: `#xmb-settings`, `#xmb-controller`, `#xmb-profile`.

### 3.2 PIN (`#pin`, `#pin-partial`, `#pin-full`)
Page shell. Title "<PS5|PS4> Console Registration" with the console name and IP as a T16 sub. Prompt T20 centred at y 160: "Enter the 8-digit session PIN displayed on your <PS5|PS4>:". C17 at y 232. TextButtons at y 424: Clear digit, Cancel, Register (disabled until all 8 filled; Register is the focused default then).
Buttons: Left/Right digit, Up/Down change, Square clear digit, Confirm register (only when 8 filled), Cancel back to Home. Hints: `[D-pad Digit] [Up-down Change] [Square Clear digit] [Confirm Register (dim)] [Cancel Cancel]`. Touch: tap a digit box, tap its chevrons, tap the three buttons.
Outcome: Register opens ResultPopup. Mock rule: a PIN starting with 0 fails, anything else succeeds (`#result-paired`, `#result-pair-failed`). Success: row becomes Paired and the list re-sorts.

### 3.3 Result and error popups
One C14, three uses. `#result-paired` (OK tone, one button OK), `#result-pair-failed` (ERR, Close / Try again), `#result-connect-failed` (ERR, Close / Try again). Cancel closes.

### 3.4 Connecting and Reconnecting
Page shell, no groups. Title set by the current stage: stage 0 "Waking Console"; stages 1, 2, 3, 5, 6 "Starting Internet Remote Play"; stages 4 and 7 "Starting Remote Play" (today's code, see Flags). Left art: ring S 128 inside a 176 spinner and a soft halo (280), x 68..388, y 168..368, centred; type logo h 32 at y 384, name T28, route T16 ("via Local Network" / "via Internet"). Right: C16 steps. Cancel TextButton right-aligned at y 440. Circle or tap Cancel stops the connect and returns Home. Standby console: wake polls until awake, then auto-connects (steps continue).

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
**Reconnecting** (`#reconnecting`): same shell, title "Optimizing Stream"; spinner art only; right column: "Recovering from packet loss" T20, "Retrying at 1.80 Mbps" MONO28, "Attempt 2" and "Please wait..." T16 TEXT_3. No input, no hint row, not cancellable.

### 3.5 Stream overlay (no menu)
Full-bleed video. Only three things draw, all C19/C25, all gated by settings:

| Element | Position | Rule |
|---|---|---|
| Exit hint pill "Back to menu: Hold [L] + [R] + [Start]" | right 16, top 16 | Show Exit Shortcut Hint; visible 5.0 s then 0.5 s fade |
| Stream Stats panel | right 16, top 64 (fixed slot) | Show Latency |
| Network Unstable pill | right 16, bottom 16 | Show Network Alerts; shown 5.0 s after each event, fading |

Exit: hold L + R + Start about 1 s (today). Select = Share and Start = Options still go to the PS5. Deep links: `#stream`, `#stream-stats`, `#unstable`, `#stream-quiet` (after the hint faded). The mock also lets Esc leave (mock only).

### 3.6 Settings (`#settings`, `#settings-network`, `#settings-display`, `#settings-controls`, `#settings-advanced`, `#settings-circle`)
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
| Network | Show Only Paired | toggle | off |
| Display | Show Latency | toggle | off |
| Display | Show Network Alerts | toggle | on |
| Display | Show Exit Shortcut Hint | toggle | on |
| Controls | Circle Button Confirm | toggle | system default (Cross on a Western unit) |
| Advanced | Clamp Soft Restart Bitrate | toggle | on |
| Advanced | Motion during loss (artifacts) (Experimental) | toggle | off |
| Advanced | Enable Logging | toggle | off |

The mock starts with Enable PSN Internet Mode on so internet screens can be shown; the app default is off. Button map: Left/Right or Confirm change, Up/Down row, L/R group, Cancel (pane to groups, then Home). Hints: `[Confirm Toggle|Next] [D-pad Change (choice)] [L R Group] [Cancel Back]`. Touch: tap group, tap row, tap chevrons, swipe pane. `#settings-circle` turns Circle Button Confirm on: every glyph in the hint row swaps and Circle becomes confirm (use the toolbar toggle to try any screen).

### 3.7 Profile (`#profile`, `#profile-connection`, `#profile-streaming`, `#profile-psn`, ...)
Page shell, title "Profile". Groups: Account, Connection, PlayStation Network.

| Group | Rows |
|---|---|
| Account | Account ID (MONO16, "Not Set" when empty), action **Refresh Account ID** (toast "Account ID refreshed from system profile" or "Could not refresh Account ID") |
| Connection | Network Type (Local Wi-Fi, PSN Internet, Manual Host, Unavailable), Console ("Not selected" when none), Console IP (only when meaningful), Status (Ready, Ready / Not Registered, Not Registered, None, or Streaming), Quality; while streaming and Show Latency: Latency, Bitrate, Packet Loss (Stable or a figure, colour OK / WARN / ERR). 8 rows scroll in the 6-row pane |
| PlayStation Network | PSN Auth status row, then actions by state |

PSN Auth states (`#profile-psn-<state>`): Disabled (`psn-disabled`, Log in disabled, description "Enable PSN internet mode in Settings", pressing it toasts "PSN internet mode is disabled in Settings"), Authenticated (`profile-psn`: Refresh hosts, Log out), Refreshing token (`psn-refresh`, no actions), Awaiting browser sign-in (`profile-login`), Token expired (`psn-expired`), Not authenticated (`psn-none`), error text (`psn-error`, e.g. "Login failed: invalid redirect URL"); the last three offer Log in.
**Log out**: first Confirm turns the row into "Press [Confirm] again to confirm log out" in `WARN`; second Confirm within 3.0 s logs out (toast "PSN login removed"); otherwise it resets (`#profile-logout`). Touch: two taps.
**Phone login** (`#profile-login`, `#profile-login-hidden`): the pane is replaced by "Phone Login Assist": C18 and four steps, Code line ("Paste redirect URL/code" until a code exists) and the sign-in URL (up to 2 lines, never ellipsised). Buttons: Start show/hide QR, Select open the Vita browser, Confirm opens the system keyboard "Paste full redirect URL" (`#keyboard-paste`), Square cancels. Hints: `[Confirm Enter code] [Start QR] [Select Browser] [Square Cancel login]`. Touch: tap the QR to toggle; the four hints are tappable.
Toasts: representative `#toast-account` and `#toast-login-complete`; full list in the copy deck.

### 3.8 Controller
Page shell, title "Controller" (zone views: "Front Touch" / "Rear Touch" with the preset as T16 sub).

| Id | View | Notes |
|---|---|---|
| `#controller` | Summary page 1 "Buttons": front diagram, L1 / R1 callouts, footer = preset description and "Page 1/2 · Buttons" | |
| `#controller-back` | Summary page 2 "Back Touch": rear diagram with zone labels | footer shows zone count |
| `#controller-front`, `#controller-rear` | Zone views | cursor on one cell |
| `#controller-multi` | multi-selection while Confirm is held | 4 cells picked |
| `#controller-full` | Triangle: whole surface popup | |
| `#mapping-popup`, `#mapping-multi`, `#mapping-shoulder` | mapping popup, size L | |

Mapping popup: titles "Shoulder Mapping" / "Front Touch Mapping" / "Rear Touch Mapping"; subtitle = slot name ("Front C2", "Rear A1", "Left Shoulder (L1)", "Right Shoulder (R1)", "Full Front Touch", "Full Rear Touch") or "N Zones Selected"; 11 rows: Options, Share, Touchpad, L1, L2, L3, R1, R2, R3, PS, None; current value ticked; hints `[Confirm Assign] [Cancel Cancel]`.
Summary buttons: Left/Right preset (saved at once), L/R page, Up/Down choose L1 or R1 (page 1), Confirm opens the shoulder popup (page 1) or the rear zones (page 2), **Triangle opens the zone view for the current page (new, today it is touch only)**, Square clears the current side, Circle Home. Hints `[D-pad Preset] [L R Page] [Up-down L1 / R1] [Confirm Shoulder] [Triangle Zones] [Square Clear] [Cancel Back]` (page 2 omits the two shoulder hints).
Zone view buttons: D-pad moves the cursor; hold Confirm and move to add cells, release opens the popup (a plain tap assigns one cell); Triangle assigns the whole surface; Square clears the side; Circle returns to the summary. Hints `[D-pad Move] [Confirm Assign] [Triangle Whole surface] [Square Clear] [Cancel Back]`.
Touch: tap the preset chevrons or the label; tap a callout; tap the diagram to enter the zone view; in the zone view finger-paint cells (backtrack removes), tap one cell; tap hints. Presets seed from defaults: front all zones Touchpad, rear left three columns L2 and right three R2, L1/R1 as themselves (today's seeds; L+Square = L3, R+Circle = R3 and Select+Start = PS are combos that remain and have no editor).

---

## 4. Input model

Physical Vita buttons are mapped to logical actions once, at the top of the frame.

| Physical | Logical |
|---|---|
| Cross | `CONFIRM` (or `CANCEL` when Circle Button Confirm is on) |
| Circle | `CANCEL` (or `CONFIRM` when Circle Button Confirm is on) |
| Triangle | `OPTIONS` (consoles) / zone view and whole surface (Controller) |
| Square | `CLEAR` (digit, side, cancel login) |
| Start | `FILTER` (Home consoles), `QR` (login assist) |
| Select | `BROWSER` (login assist). Unused elsewhere (no Select toast any more) |
| L, R | category or group or page, per screen. In-stream: hold L + R + Start to exit |
| D-pad | move focus; Left/Right also change choices |

Rules:
1. Swap applies to every screen and every glyph, including the hold-to-select on the zone view and the PIN screen. The hint row reads the setting; screens never test it.
2. Focus: one focused item per screen layer; a popup is modal and owns input. Up/Down do not wrap in lists or popups; a choice value wraps.
3. Hold-repeat on D-pad (today's helper). Hold timing for the exit combo is 1 s.
4. Touch: front panel mapped to screen pixels. Tap = focus and activate (rows) or activate (buttons). First tap focuses an unfocused XMB row, second tap activates. Swipe scrolls lists and the category bar; drag across zones paints a selection. Tap outside a popup or the Options column cancels. Every tap target is at least 48 x 48 (C01 icons 64).
5. Rear touch is only used by the Controller feature, never for UI.
6. Persist: Settings and Controller values save on change (today).

---

## 5. Copy deck

Wording changes: glyphs replace "X/O/Cross/Circle" text; "Streaming Settings" becomes "Settings"; "internet" badge becomes "Internet"; "PS5 Console Registration" is model aware; the old numbered login steps use glyphs; "Press X" labels become hint-row verbs; "Select toast" gone. Items marked (new) are not in today's app and are for CEO sign-off.

**Home / consoles**
- Categories: Consoles, Settings, Controller, Profile
- Verbs: Connect, Wake, Pair, Please wait, Options, Filter, Clear filter, Category
- States: Ready, Standby, Unpaired, Please wait... ; route label: Internet
- Detail: Address, Route (Local Network, Internet, Local Network + Internet, Not reachable), Pairing (Paired, Unpaired), Unknown (new)
- Filter: Filter: "<text>" (N found); Start Filter
- Empty: Searching for consoles... ; No consoles match filter
- Banner: Streaming stopped: <reason> - Please wait a few moments ; reasons: Console entered sleep mode, Console disconnected, or the raw reason
- Status hints: Wake signal failed; attempting connection anyway. / Wake signal failed. Check pairing and network. / Console releasing session... ready in Ns / Remote Play already active on console / Console Remote Play crashed - wait a moment / Console busy - retrying in Ns... / Missing console credentials. Re-pair may be required. / Enable PSN internet mode in settings. / PSN login required for internet remote play. / PSN session expired. Re-authenticate in Profile. / Could not determine host address. / Waiting for console network link... / Video references unstable - requesting keyframe / Rebuilding stream at safer bitrate / Persistent video desync - rebuilding session / Packet loss burst - requesting keyframe
- Network Unstable
- Options: Connect, Wake and connect, Connect via, Re-pair, Pair, Change icon; header "Options"
- Connect via: title Connect via; rows Local Network, Internet
- Re-pair (new): Re-pair <name>? / You will need to enter a new 8-digit PIN from the console. / Cancel, Re-pair
- Change icon (new): Change icon; TV, Living room, Bedroom, Dorm, Office, Another place; hints Choose, Cancel

**PIN and results**
- <PS5|PS4> Console Registration; <name> (<ip>); Enter the 8-digit session PIN displayed on your <PS5|PS4>:
- Buttons: Clear digit, Cancel, Register; hints Digit, Change, Clear digit, Register, Cancel
- Results (new): Console paired / <name> is paired. You can connect to it now. / OK ; Pairing failed / <name> did not accept the PIN. Check the code on the console and try again. / Close, Try again ; Could not connect / <name>: <reason> / Close, Try again

**Connecting**
- Titles: Waking Console, Starting Internet Remote Play, Starting Remote Play; hint Cancel; via Local Network, via Internet
- Stages and details: section 3.4
- Reconnecting: Optimizing Stream, Recovering from packet loss, Retrying at X.XX Mbps, Attempt N, Please wait...

**Stream**: Back to menu: Hold L + R + Start ; Stream Stats, Latency, FPS (N ms, in / target, N/A) ; Network Unstable

**Settings**: group names Video, Network, Display, Controls, Advanced; row labels and values in section 3.6; descriptions (new, draft for sign-off): Quality Preset "Video resolution requested from the console."; Latency Mode "Sets the target bitrate. Higher looks better but needs a stronger connection."; FPS Target "Frame rate requested from the console."; Force 30 FPS Output "Output video at 30 FPS."; Fill Screen "Stretch the video to fill the whole screen."; Auto Discovery "Find consoles on your network automatically. Takes effect the next time the app starts."; Enable PSN Internet Mode "Connect to your consoles over the internet with your PSN account."; Show Only Paired "Hide consoles that are not paired."; Show Latency "Show latency and frame rate during a stream, and live metrics on Profile."; Show Network Alerts "Show a badge when the connection becomes unstable."; Show Exit Shortcut Hint "Show how to leave the stream when it starts."; Circle Button Confirm "Use Circle to confirm and Cross to go back, on every screen."; Clamp Soft Restart Bitrate "Limit the bitrate when the stream restarts after packet loss."; Motion during loss "Keep motion going while packets are lost. May show visual artifacts."; Enable Logging "Write diagnostic logs on the Vita for troubleshooting."; On, Off; hints Toggle, Next, Change, Group, Back, Open

**Profile**: Account, Connection, PlayStation Network; Account ID, Not Set, Refresh Account ID ("Read the Account ID again from the system profile." new); Network Type, Console, Console IP, Status, Quality, Latency, Bitrate, Packet Loss, Stable; PSN Auth; Disabled, Authenticated, Refreshing token, Awaiting browser sign-in, Token expired, Not authenticated, <error text>; Log in, Log out, Refresh hosts; Press [Confirm] again to confirm log out ; Phone Login Assist; 1 Press [Start] to show or hide the QR code; 2 Scan the QR code with your phone and sign in; 3 Press [Confirm] and paste the redirect URL or code; 4 [Select] opens the Vita browser instead; Code: Paste redirect URL/code; QR hidden ; hints Enter code, QR, Browser, Cancel login, Confirm log out, Refresh
- Descriptions: "Enable PSN internet mode in Settings" (today); "Sign in with your phone. Needed for internet Remote Play." (new); "Reload your internet-capable consoles." (new); "Remove the saved PSN login from this Vita." (new)
- Toasts (all): Account ID refreshed from system profile / Could not refresh Account ID / PSN internet mode is disabled in Settings / Scan QR on phone, then press [Confirm] to paste the full redirect URL / PSN login could not start / PSN internet host list refreshed / PSN internet host refresh failed / PSN login canceled / QR shown. Scan it with your phone. (reworded from "QR shown...") / QR hidden. Press Start to show it again. (reworded) / Opened browser fallback. Phone QR is still recommended. / Could not open browser. Use the phone QR. / Could not open text input / No URL/code entered / PSN login complete / PSN login failed / PSN login removed
- System keyboard titles: Filter Consoles; Paste full redirect URL

**Controller**: Controller; Front Touch; Rear Touch; Custom 1/2/3; Your first / second / third custom mapping; Page 1/2 · Buttons; Page 2/2 · Back Touch; N zones; Zone <A1..F3>; N Zones Selected; callouts "L1 -> <output>", "R1 -> <output>"; popup titles and subtitles in section 3.8; outputs Options, Share, Touchpad, L1, L2, L3, R1, R2, R3, PS, None; hints Preset, Page, L1 / R1, Shoulder, Zones, Clear, Back, Move, Assign, Whole surface, Assign, Cancel

---

## 6. Flags for CEO

1. **Show Navigation Labels dropped.** It labels the old side nav; XMB always shows labels. The setting and its config key become dead; remove or leave unused.
2. **Controller zone editing has no button path today.** From the summary, front and rear zone views open only by tapping the diagram. The spec adds Triangle (and Confirm on page 2). Needed for a button-only user.
3. **Roboto Light is not in the app today** (Regular and Mono only). The XMB look uses Light 20 and 28. Costs one TTF and about 0.3 MB; fallback is Regular, which looks heavier.
4. **Top bar adds Wi-Fi, battery and clock, plus the time-of-day wave tint.** Not in today's app; needs `sceNetCtl`, `scePower`, RTC reads. Cheap, but new. Can be dropped without touching anything else.
5. **Connecting title flips mid-flow on internet connects.** Today's code picks the title per stage, so stage 4 (Preparing Remote Play) says "Starting Remote Play" between two "Starting Internet Remote Play" stages. The mock keeps this; suggest one title per flow.
6. **Stats panel gets a fixed slot** (top 64). Today it jumps up 44 px when the exit hint fades. One deliberate deviation from "exactly today's overlay".
7. **Overlay margin 16** instead of today's 18, and square pills instead of rounded; both for the 8 px grid.
8. **Toast time 3.0 s** (today 2.0 s) because the longest message ("Scan QR on phone...") needs it.
9. **Profile today has a third card** ("PSN Authentication", "Authenticated / Not authenticated", an "Add New" button, "Press X to register"). Not in the inventory; dropped because Add was removed and PSN Auth now lives in the PlayStation Network group. Confirm nothing depends on it.
10. **Console type is no longer on the list rows**, only in the detail panel (locked). The row status line carries state only.
11. **Unpaired consoles** show Pair and Change icon in Options (Connect and Re-pair would both just open the PIN screen). Cooldown disables Connect and Connect via.
12. **Last session line removed.** The earlier mock showed "Last session"; the app has no such data.
13. **New copy needs sign-off**: all Settings descriptions, Profile action descriptions, the Re-pair confirm, the Change icon popup and the three result popups. Pairing result needs a console-side success/failure signal the app does not surface today (inventory: "no success/failure UI today").
14. **Room icon needs storage**: one small integer per console in the host config (default TV).
15. **PIN title and prompt are model aware** ("PS4"/"PS5"); today they say PS5 always.
16. **Profile viewable while streaming?** The inventory lists streaming metrics on Profile, but in-stream has no menu. The mock shows them (`#profile-streaming`) for when the app is reachable mid-session (for example while Reconnecting); confirm when this is visible.
17. **Mock-only affordances** (not specified for the app): Esc leaves the stream; the toolbar Circle toggle; the fictional stream picture; the QR is illustrative.
