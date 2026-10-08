# VitaRPS5 XMB build spec (issue #271, round 6b)

Baseline for engineers. The HTML mock in this folder is the visual reference (`xmb.html`, deep links in section 3); this file is the contract. Where they disagree, fix the mock. Native 960x544, one 8 px grid. Nothing here is built yet.

- Theme source of truth: `tokens-xmb.css`. Component CSS: `xmb.css`. Mock logic: `xmb-app.js` (read it for exact behaviour). Canonical screens are lossless 960x544 PNGs in `screens/` (index.html), the device preview is the only JPG.
- vita2d cost numbers: `FEASIBILITY.md`. Draw-call budget stays about 80 per frame on Home.
- Scope decisions applied: Logs category removed; Add item removed; in-stream has no menu (today's overlay only); triangle options per console; one result/error popup; re-pair asks first; everything works by touch; persistent hint row; Settings grouped; "Show Navigation Labels" dropped; no time-of-day wave tint (one fixed palette); Profile has no streaming metrics (stream stats live only in the overlay).

---

## 1. Theme and tokens

One header (`ui_theme.h`). **Rule:** tokens cover colours (alpha included), type faces and sizes, the spacing scale, line widths, durations and easing. Geometry that belongs to one component (its widths, heights, offsets, hit boxes) is a named constant (`UI_<COMPONENT>_*`) listed in that component's section of this file; screen code uses those constants and never a raw number or colour.

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
| `PANEL` | rgba(6,12,28,.92) | popups, toast, options column ramp, stats panel | `UI_COLOR_CARD_BG` |
| `SCRIM` | rgba(2,5,14,.55) | behind popups | popup dim literals |
| `SCRIM_STRONG` | rgba(2,5,14,.82) | system keyboard stand-in only (not drawn by the app) | n/a |
| `HUD` | rgba(6,10,22,.60) | overlay pill and badges | `RGBA8(0,0,0,180/200)` |
| `PAGE_WASH` | rgba(3,6,16,.60) | dims the wave behind pages, Controller, PIN, Connecting | new |
| `FILL_FOCUS` | white @ 12% | focused row, focused button | blue focus rings |
| `FILL_ON` | white @ 26% | toggle on, selected zone, cursor zone | blue fills |
| `LINE` | white @ 26% | page rule, popup and toast border, button border | grey 1 px lines |
| `LINE_FAINT` | white @ 14% | row dividers, kv rows | grey 1 px lines |
| `BG_*` | fixed wave palette below | background | `UI_COLOR_BACKGROUND`, particles |

Effect colours (every other alpha-bearing colour the components use, all in `tokens-xmb.css`):

| Token | Value | Used for |
|---|---|---|
| `GLOW` | white @ 70%, radius 12 | focus glow on text, icons, ring (baked texture) |
| `GLOW_SOFT` | white @ 35% | type-logo glow, diagram halo |
| `GLOW_INNER` | white @ 45% | inner glow of picked and cursor zones |
| `SHADOW` | black @ 55% | text and icon drop shadow |
| `LEADER` | white @ 55% | callout leader lines and underlines |
| `ZONE_LINE`, `ZONE_MAPPED_LINE` | white @ 22%, #A0CDFF @ 60% | idle and mapped zone borders |
| `RING_FILL` | white @ 10% | status ring inner wash |
| `HALO` | #82AAFF @ 28% | Connecting halo |
| `BADGE_BG` | #071022 | ring badge disc |
| `QR_PLATE`, `QR_INK`, `QR_HIDDEN` | #FAFAFA, #0A0A0A, #1A1A1A @ 55% | QR panel |
| `PANEL_EDGE`, `EDGE_0` | rgba(4,8,20) @ 92% / 0% | Options column feathered edge |
| `VIG_*` | black/navy @ 32-55% | Home vignette (gradients) |

Alpha levels in use (the whole set): 10, 12, 14, 22, 26, 28, 32, 34, 35, 38, 40, 45, 50, 55, 60, 70, 82, 92 percent, always via a token above.

Wave palette (one fixed set, no time of day): top #06204A, mid #0F4585, bottom #245F9C, ribbons #96CDFF / #64B4F0 / #BEDCFF, horizon glow rgba(160,210,255,.28). Tokens `--wave-*`; geometry and speeds in `xmb-wave.js`.

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

Roboto Light is not loaded by the app today (only Regular and Mono). Decision: load it (one TTF, 170 KB, about 0.3 MB of atlas).

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
| Interactive (struct + init/draw/input) | C01 CategoryBar, C02 XmbList, C05 OptionsColumn, C07 GroupList (the left list of a page), C08 SettingRow list (with C09 Toggle and C10 ChoiceValue drawn inside), C11 Popup (one struct; C12 ListPopup, C13 ConfirmPopup, C14 ResultPopup are configurations of it), C17 PinField, C21 ZoneGrid, C22 TextButton, C28 FilterLine |
| Display-only (draw helper) | C03 StatusRing, C04 DetailPanel, C06 HintRow (draw + hit-test helper, items come from the screen), C07 page frame (title, rule, wash), C15 Toast (one static timer), C16 Spinner and ProgressSteps, C18 QrPanel (the screen owns the tap rect), C19 Pill, C20 ControllerDiagram and Callout (callout tap rect owned by the Controller screen), C23 TopBar, C24 ScrollIndicator, C25 StatsPanel, C26 EmptyState, C27 Background (one static ribbon state) |

`UiInput` is one per-frame snapshot: logical buttons `pressed/down/released` (already swapped by Circle Button Confirm, section 4), D-pad repeat, and touch `{x, y, down, pressed, released, dragged, dx, dy}` in screen pixels. Screens own their components as struct members and forward input to the focused one; draw order is the screen's job.

**Visible rect vs hit rect.** Interactive components store both at init. The hit rect is the visible rect grown to at least 48 x 48 (centred); drawing uses `visible`, hit tests use `hit`. Constants live in the component's section (`UI_<COMPONENT>_HIT_*`). Hit rects may overlap neighbouring visuals. **Overlap priority**, first match wins: popup, then Options column, then hint row, then the screen's small controls (buttons, chevrons, callouts, filter clear, chips), then rows and cells, then big backgrounds (diagram, list viewport, pane). Swipes start only on a list viewport, the category strip, a pane or a list popup. A touch that moves more than 8 px is a swipe or a paint and never also a tap.

Text is drawn from the 6 pre-rendered faces; focus glow and rings are baked textures tinted at draw time (FEASIBILITY.md).

### C01 CategoryBar (interactive)
| | |
|---|---|
| Purpose | Horizontal Consoles / Settings / Controller / Profile row. Replaces the wave sidebar. |
| Constants | `UI_CAT_BOX` 64 (art 48), `UI_CAT_Y` 104 (centre), `UI_CAT_X0` 256, `UI_CAT_FAR` 128 (first right neighbour), `UI_CAT_STEP` 112, `UI_CAT_SCALE_ON` 1.25, `UI_CAT_SCALE_LEFT` 0.75, `UI_CAT_STRIP` y 64 h 112 full width (swipe zone) |
| Anatomy | Focused centre x 256, scale 1.25, glow, label T16 centred 16 below the box (y 152-176). Right neighbours at x 384, 496, 608. Left neighbours at 256 - 112 per step, scale 0.75. Unfocused opacity 62%. Icons: `icon_play.png`, `icon_settings.png`, `icons/controller.svg`, `icons/profile.svg`. |
| States | focused, unfocused, dimmed to 12% while Options column is open |
| Input | Left/Right or L/R changes category (slide 300 ms). Touch: tap an icon focuses it (hit 64 x 64); horizontal swipe on the strip moves one category per 56 px. |
| Used by | Home |
| Replaces | `ui_navigation.c` wave sidebar and `NAV_*` |

### C02 XmbList + ListRow (interactive)
| | |
|---|---|
| Purpose | Vertical item column under the focused category. |
| Constants | `UI_LIST_X` 224, `UI_LIST_Y` 184, `UI_LIST_W` 368, `UI_LIST_H` 312 (bottom 48 fades), `UI_LIST_ICON` 64 (scale 0.8 / 1.1), `UI_LIST_TEXT_X` 304, `UI_LIST_TEXT_W` 288, `UI_LIST_FOCUS_Y` 192, `UI_LIST_ROW_H` 56, `UI_LIST_ROW_H_FOCUS` 88, `UI_LIST_GAP` 24, `UI_LIST_LINE_H` 24, `UI_LIST_SLIDE` 80 |
| Anatomy | Row: icon box at x 224, text at x 304, max w 288. Focused row top y 192, height 88; unfocused 56. Name T28 focused / T20 unfocused, status line T16, then the hint block (below). Rows above the focus slide up 80 px per step and fade to 0. Cascade-in on category change. |
| Hint block | A status hint (T16, `ERR`) wraps inside the 288 px column, **never truncated**, up to 2 lines; each line adds 24 px to that row's height, for focused and unfocused rows alike. All copy-deck hints fit in 2 lines at 288 px (longest: "Missing console credentials. Re-pair may be required.", 53 characters; measured). A hint longer than 2 lines is a copy bug, not a layout case. |
| States | focused, unfocused, dimmed (opacity 55%, cooldown), hidden (above focus) |
| Input | Up/Down moves (no wrap). Confirm or tap on the focused row activates. Tap on another row focuses it, second tap activates. Vertical swipe on the viewport: one row per 56 px, focus follows. Hit = the row, 56 high. |
| Used by | Home (consoles, setting groups, presets, profile groups) |
| Replaces | console card list (`ui_console_cards.c`), 4 duplicated card+focus-ring pieces |

### C03 StatusRing (display-only)
| | |
|---|---|
| Purpose | Console identity and state in one glyph: user room icon inside a status ring plus corner badge. `ui_draw_status_ring(x, y, size, room, state)`. |
| Constants | `UI_RING_W` 2, `UI_RING_GLOW` 12, `UI_RING_ICON` 50% of size, `UI_RING_BADGE` 40% of size at (-4, -4), badge glyph 70% |
| Anatomy | Sizes in use: 56 (rows), 128 (Connecting). Ring in the state colour, soft glow in the state colour at 45%, room icon (`icons/tv|sofa|bed|bunk|desk|house.svg`, default tv), badge disc `BADGE_BG` with a 2 px state-colour border. Unpaired ring is dashed with no glow. |
| States | Ready (OK, check), Standby (WARN, moon), Unpaired (IDLE dashed, lock), Internet (OK, globe), Unavailable (ERR, warning), Cooldown (ERR, clock; row dimmed) |
| Used by | Home list, Connecting art |
| Replaces | card status dot, "internet" badge, PS5/PS4 logo on card |
| Cost | ring and badge are baked textures tinted per state, 3 draws per item |

### C04 DetailPanel (display-only)
| | |
|---|---|
| Purpose | Facts about the focused item, no card. |
| Constants | `UI_DETAIL_X` 608, `UI_DETAIL_Y` 192, `UI_DETAIL_W` 304, `UI_DETAIL_LOGO_H` 48 (PS5 176 wide, PS4 200 wide), `UI_DETAIL_KV_H` 32 |
| Anatomy | Console: type logo (`PS5_logo.png`, `ps4.png` crop) white, 16 below it the name T28, status line T16 (dot + text), 16 gap, kv rows (label T16 TEXT, value MONO16 right, `LINE_FAINT` hairline): Address, Route, Pairing. Settings group: the group's rows and values. Preset: description line, then L1, R1, Front touch, Rear touch. Profile group: its first 4 rows. |
| States | per item kind; rises 300 ms when focus changes |
| Replaces | the in-card IP/status text |

### C05 OptionsColumn (interactive)
| | |
|---|---|
| Purpose | Triangle options for a console. |
| Constants | `UI_OPTS_X` 608 (w 352 to the right edge), `UI_OPTS_PAD` 48, `UI_OPTS_ROW_H` 56, first row y 160 |
| Anatomy | Slides in from the right 300 ms, left edge feathered into `PANEL_EDGE`. Console name T28 at y 88, "Options" T16 at y 120, rows T20 with 1 px `LINE_FAINT` divider. Focused row: TEXT, 2 px white underline, glow. Home dims behind it. |
| Items | Connect (Wake and connect on standby) / Connect via (only when Local and Internet both exist) / Re-pair / Change icon. Unpaired consoles get Pair / Change icon. Cooldown disables Connect and Connect via. |
| Input | Triangle opens, Triangle or Circle closes. Up/Down, Confirm. Touch: tap a row (hit 352 x 56); a tap anywhere outside closes the column and is consumed (it never also acts on what is underneath). While open, the category bar, list, detail panel, filter line and top bar dim to 25%; the column and the hint row stay bright. |

### C06 HintRow (display-only, with hit test)
| | |
|---|---|
| Purpose | Persistent bottom row. Replaces the Select toast. |
| Constants | `UI_HINT_Y` 496, `UI_HINT_H` 48, gap 32, glyph h 24, glyph-label gap 8, `UI_HINT_ALERT_W` 200 (right slot) |
| Anatomy | x 48..912. Each hint: glyph + label T16 TEXT_2. Glyphs: the 4 PNG symbols plus baked flat glyphs for D-pad (all, left-right, up-down), L, R, L+R, Start, Select (new assets, 24 px high). |
| Confirm swap | Hints are declared as `CONFIRM` / `CANCEL`; the row resolves them to Cross or Circle from the setting. |
| Alert slot and collapse rule | When Network Unstable is active on a menu, the right 200 px (x 712..912) are reserved for the alert pill and never overlap hints. Hints use x 48..696. If they do not fit, items flagged **low priority** are dropped from the right until they do (low priority: L R Category on Home, Clear and Preset on the Controller summary, Clear digit on PIN). Confirm, Cancel and the main action are never dropped. |
| Popups | While a popup is open the row shows the popup's hints. The Confirm hint carries the label of the **focused button** (for example "Re-pair", "Try again"); Cancel shows the popup's cancel label. A one-button popup shows Confirm only. If the focused button is the cancel button, only Confirm shows. |
| States | normal, dim (45%, action not available) |
| Input | Touch: tapping a hint triggers the same logical action (hit = hint width x 48). |
| Replaces | Select hints toast, per-screen hint strings, `NAV_TOAST_*` |

### C07 PageShell (frame is display-only, GroupList is interactive)
| | |
|---|---|
| Purpose | Common frame for Settings, Profile, Controller, PIN, Connecting, Reconnecting. |
| Constants | `UI_PAGE_ICON` 32 at x 48, `UI_PAGE_TITLE_X` 96, title row y 64 h 48, rule y 120, `UI_PAGE_GROUP_X` 48 w 256 row h 56, `UI_PAGE_PANE_X` 336 w 576 h 288 (6 rows), `UI_PAGE_DESC_Y` 440 h 48, `UI_PAGE_SCROLL_X` 920 |
| Anatomy | Top bar (C23) always. **Every page title has a 32 px icon at x 48 and the title at x 96**: Settings gear, Profile person, Controller pad, PIN lock, Connecting LAN / globe / moon (by flow), Reconnecting Wi-Fi. Title T28. `LINE` rule y 120. Wave dimmed by `PAGE_WASH`. Optional right slot in the title row (Controller preset switcher). Settings/Profile body: GroupList (rows h 56, T20; focused-active group T28 + glow; current group has a 2 px white bar at its left edge), pane, description line (T16, max 2 lines), scroll indicator (C24). |
| Input | Up/Down, Left/Right moves between group list and pane; L/R switches group; Circle returns (pane to groups, groups to Home). Touch: tap a group, tap a row, swipe the pane (one row per 48 px). |

### C08 SettingRow (interactive; C09 Toggle, C10 ChoiceValue are drawn inside it)
| | |
|---|---|
| Purpose | One row: label left, control or value right. Kinds: toggle, choice, info, action. |
| Constants | `UI_ROW_H` 48, `UI_ROW_PAD` 16, `UI_TOGGLE_W` 48, `UI_TOGGLE_H` 24, `UI_TOGGLE_KNOB` 16 (inset 2, travel 24, 180 ms), `UI_CHOICE_VALUE_W` 224, `UI_CHOICE_ARROW` 48 x 48 hit around a 16 px icon |
| Anatomy | Label T20, `LINE_FAINT` divider. Focused: `FILL_FOCUS`, TEXT, white divider, glow on the label. Disabled: 50%. Armed (log out): label and value in `WARN`. Error status row: `ERR` value with a 20 px warning icon in front and a 2 px `ERR` rule at the row's left edge. Toggle: track with 2 px border (off TEXT_2; on white border + `FILL_ON`), square knob, "On"/"Off" T16 16 right of the track. Choice: chevrons around a 224 wide centred value T20. Info: value T20 or MONO16. Action: label, chevron-right when enabled. |
| States | normal, focused, pressed (FILL_ON 150 ms), disabled, armed |
| Input | **First tap acts and takes focus**: toggle flips, choice cycles to the next value, action runs; info rows only take focus. Buttons: Confirm flips/cycles/runs; Left/Right changes a choice. Tap on a chevron steps that way. |
| Replaces | settings list rows, `toggle_switch` (restyled), `dropdown`, Profile text rows |

### C11 Popup (interactive), sizes
| | |
|---|---|
| Purpose | The only modal frame. 3 fixed sizes, all 480 wide, x 240. |
| Sizes | **S** 480 x 256 at y 144 (confirm, result, 2-row list). **M** 480 x 352 at y 96 (icon picker grid). **L** 480 x 432 at y 56 (mapping list, 6 visible rows). |
| Constants | `UI_POPUP_W` 480, `UI_POPUP_PAD` 32, `UI_POPUP_S_H` 256, `_M_H` 352, `_L_H` 432, button bar h 48 gap 16, max button w 208 |
| Anatomy | `PANEL` fill, 1 px `LINE` border, padding 32. Title T28 (optional 32 px icon, gap 16), subtitle T16, body T16 (8 below). `SCRIM` behind. S popup with a list: the rows are vertically centred in the space under the subtitle. Enter: rise 16 px + fade, 300 ms. |
| Input | Modal. Circle or tap on the scrim cancels. Hit for rows 48 high. |
| Replaces | 6 modal sizes (520x280, 560x290, 400x160, 360x340, 700x450, 640x360/380) |

### C12 ListPopup (configuration of C11)
Rows h 48 (T20, optional right label T16 TEXT_3, check icon for the current value), `LINE_FAINT` dividers, focused row `FILL_FOCUS` + 2 px underline. Scrolls when rows exceed the visible count (L shows 6), selected row kept near the middle, 2 px scroll indicator at the right edge. Vertical swipe on the list: one row per 48 px, focus follows. **Grid variant** (icon picker): 3 x 2 cells 128 x 104, gap 16, icon 48 (70% opacity), label T16. Focus and current are independent; matrix:

| | Not current | Current |
|---|---|---|
| Not focused | icon 70%, label TEXT_2, no marker | icon 70%, label TEXT_2, quiet check (16 px, `TEXT_3`) at the cell's top-right |
| Focused | radial `FILL_FOCUS`, icon 120% + glow, label TEXT | same plus the check in TEXT |

Input: Up/Down (grid: all four), Confirm, tap a row (selects and activates). Used by: Mapping popup (L), Connect via (S, 2 rows), Change icon (M grid).

### C13 ConfirmPopup (configuration of C11)
Size S, title with icon, 1 to 3 lines body, two TextButtons (Cancel left, action right). Default focus Cancel. Left/Right switches, Confirm presses the focused button, Circle cancels. Used by: Re-pair.

### C14 ResultPopup (configuration of C11)
Size S, tone icon (OK check or ERR warning, 32), title, 1 to 3 lines body, one or two TextButtons. Default focus on the right-most (primary) button. One component for pairing success, pairing failure (three reasons, section 3.3) and connection failure. Used by: pairing result, connection failure. Replaces: the debug-only error popup.

### C15 Toast (display-only, one static timer)
`UI_TOAST_Y` 432, h 48, centred, padding 24, optional 24 px icon + 16 gap, T20, `PANEL` fill, 1 px `LINE`, max w 720, single line. Rise 300 ms, 3.0 s, fade 300 ms. One at a time (a new one replaces). Used by: Profile messages. Replaces: 4 pill/toast implementations.

### C16 Spinner and ProgressSteps (display-only)
**Spinner**: 270 degree arc, 2 px, TEXT; sizes 176 (Connecting art) and 16 (inline). **ProgressSteps** `ui_draw_steps(x, y, steps[], count, current)`: x 464..912 from y 152. Row min h 40, padding 8, gap 16, T20. Done: 12 px `OK` dot. Current: T28 with detail T16 TEXT_2 under it, glow, 16 px spinner in the marker column. Pending: MONO16 number in TEXT_3. Only the stages of the current flow are listed. Used by: Connecting. Replaces: `ui_draw_spinner` card layout.

### C17 PinField (interactive)
| | |
|---|---|
| Constants | `UI_PIN_BOX_W` 56, `UI_PIN_BOX_H` 72, gap 8, `UI_PIN_CHEV_W` 56, `UI_PIN_CHEV_H` 48 (hit and visible box; art 20), row top y 224 (chevron top; digits y 272-344), `UI_PIN_BTN_Y` 424 |
| Anatomy | Eight boxes, MONO40, 2 px underline (TEXT_3; focused white + `FILL_FOCUS` + glow). The focused box shows up/down chevron boxes above and below. Empty focused box: blinking 2 x 40 cursor (1 s). |
| Focus | One focus only, in one of two zones: **digits** or **buttons** (Clear digit, Cancel, Register). Right on the last digit when all 8 are filled, or Down, moves to the buttons (Register when ready); Left/Right switch buttons; Up returns to the last digit. In the buttons zone no digit box shows focus. Confirm in the digits zone registers when all 8 are filled (today's behaviour). |
| Input | Up/Down change the digit (empty: Up gives 0, Down gives 9; wraps), Square clears it. Touch: tap a box (focus moves to digits), tap its chevrons, tap the buttons. |
| Used by | PIN screen. Replaces `pin_digit`. |

### C18 QrPanel (display-only)
QR art 160 x 160 on a `QR_PLATE` square with 8 px quiet zone (176 x 176), modules from `ui_qr`, ink `QR_INK`. Hidden state: 176 box in `QR_HIDDEN` with "QR hidden" T16 TEXT_3. The login screen owns a 176 x 176 tap rect over it: **tap toggles show/hide**, same as Start, with the same feedback (toast "QR shown. Scan it with your phone." / "QR hidden. Press Start to show it again."). Next to it a text column. The URL line is a **deliberate short display form** `my.account.sony.com/sso/ca/authorize` (MONO16, one line, never clipped, never ellipsised); the full authorize URL stays in memory for the QR and the browser. Replaces: `draw_profile_login_assist_panel` (reuse the `ui_qr` encoder unchanged).

### C19 Pill (display-only)
h 32, padding 0 16, `HUD` fill, T16 TEXT, square. Variants: **warn** (2 px `WARN` left rule: cooldown banner), **unstable** (12 px `ERR` dot, pulse 1.4 s), **plain** (exit hint). Used by: stream overlay (exit hint top-right, Network Unstable bottom-right at 16 px margins), hint-row alert slot, top-bar banner slot. Replaces: overlay `draw_pill`, `UI_LOSS_INDICATOR_*`.

### C20 ControllerDiagram + Callout (display-only)
| | |
|---|---|
| Images | `controller_front.png` 874 x 396, tinted white (front inverted; back as is); the rear art ships as `controller_back_clean.png` (the "Sony Computer Entertainment Inc" line erased; keep the original for reference). |
| Constants | Summary front: x 164, y 176, w 630. Summary back: x 170, y 176, w 620. Zone views: front x 120, y 144, w 720; back x 140, y 152, w 680. Callout visible h 32 at y 136 (hit 48: 8 above and below), T20, w 136, left x 64, right edge 896. Footers y 464 h 24. |
| Anatomy | Callout: text "L1 -> <output>" with the arrow glyph, 1 px underline (focused 2 px white + glow). The leader runs from the callout to the shoulder (10% from the top edge, 10% / 90% from the sides) and **ends in a 6 px dot at the shoulder**. Footers T16: left x 48 (preset description or zone name), right x 912 (page label). |
| Input | Up/Down picks L1/R1, Confirm opens its popup. Touch: tap a callout (its 48 high hit rect wins over the diagram) or the diagram. |
| Replaces | procedural diagram drawing is kept as is (`ui_controller_diagram.c`); only chrome changes. |

### C21 ZoneGrid (interactive)
6 x 3 cells (columns A-F, rows 1-3) over the screen rect (front: x +178/874, y +30/396, 526 x 298 source px) or the rear pad rect (x +139/720, y +45/327, 444 x 188 source px), scaled with the diagram. Cell label T16 (OPT, SHR, TP, L1...; blank for None). Constants: `UI_ZONE_COLS` 6, `UI_ZONE_ROWS` 3.

| State | Look | Precedence |
|---|---|---|
| mapped | `ACCENT` 28% fill, `ZONE_MAPPED_LINE` 1 px border | lowest |
| cursor | 2 px white border, `FILL_FOCUS` fill (mapped fill replaced) | over mapped |
| picked (multi-select) | 2 px white border, `FILL_ON` fill, 14 px inner `GLOW_INNER` | highest; a cell that is both cursor and picked draws as picked |

Input: D-pad moves; hold Confirm and move adds cells to the selection (popup on release; a plain tap assigns the one cell); touch (a single-cell gesture opens that cell's popup on release, and the click that follows is ignored): finger paint across cells, backtracking one cell removes the last, release opens the popup; tap = one cell. Used by: Front and Rear zone views; Summary page 2 shows the grid read-only (tap opens Rear zones).

### C22 TextButton (interactive)
h 48, min w 128, padding 0 24, T20, 1 px `LINE` border, no fill. Focused: `FILL_FOCUS`, white border, glow. Disabled: 45%. Pressed: `FILL_ON`. Hit = visible (already above 48). Used by: popups, PIN, Connecting (Cancel). Replaces: `text_button`.

### C23 TopBar (display-only)
y 16, h 32, x 48..912. Three slots: logo (h 32, `Vita_RPS5_Logo.png`) left; centre slot (flex, 24 px padding each side) holds the cooldown banner on Home; right group: Wi-Fi icon 24, battery icon 24 + percent T16, clock T20 (gap 24). **Banner rule:** while the banner shows, the Wi-Fi and battery items are hidden (clock stays), which gives the centre slot about 700 px; the banner pill is at most the slot width on one line, and if the reason string is too long only the reason is shortened with an ellipsis ("Streaming stopped:" and "- Please wait a few moments" always show). Hidden in-stream. System reads needed: link state (`sceNetCtl`), battery percent (`scePower`), local time (RTC), polled about once per second.

### C24 ScrollIndicator (display-only)
2 px track (`LINE_FAINT`), 2 px white thumb, at x 920 beside the pane or the right edge of a list popup. Shown only when rows exceed the viewport. Replaces: 2 scrollbar implementations.

### C25 StatsPanel (display-only)
`PANEL` fill, padding 8 x 16, min w 176, right 16, top 64. Title "Stream Stats" T16 TEXT_2; rows label T16 TEXT_3, value MONO16 white right aligned. **Latency** = `measured_rtt_ms`, shown as "N ms"; "N/A" when there is no value or the metrics are older than 3.0 s (today's rule). **FPS** = incoming frames per second measured over the last metrics window, shown as "in / target" (target = `target_fps`, else the negotiated fps; "in" alone when no target; "N/A" when none). Unit is whole frames per second. Cadence: the panel text is rebuilt **once per second** (not per frame) so numbers do not flicker; stale detection runs per frame. Shown only when Show Latency is on. Replaces: `draw_stream_stats_panel`.

### C26 EmptyState (display-only)
Single line T20 TEXT_2 at x 304, y 208, with a 16 px inline spinner for Searching. Used by: Home consoles.

### C27 Background (display-only, one static ribbon state)
5 ribbons, 36 dust points, one fixed palette (section 1.1), CPU vertex update at 30 Hz (freeze or halve while Connecting). FEASIBILITY.md has the costing. Replaces: `ui_particles`.

### C28 FilterLine (interactive)
x 608, y 144, h 32, T16 TEXT_2. Either `[Start] Filter` (more than 4 consoles) or `Filter: "text" (N found)` plus a clear box. The line (visible h 32) and the clear box (visible 32 px) both have a **hit rect of at least 48 x 48**. Tap the line opens the keyboard; tap the clear box clears.

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

Per-console hint lines (T16 `ERR`, under the status line): the **full message is always shown**, wrapped to at most 2 lines in the 288 px text column (C02); the row grows by 24 px per line. `#hints` shows three (one wrapping to 2 lines). Full list in the copy deck. Network Unstable shows as a pill in the hint row's reserved right slot (200 px) on any menu, hints never run under it; collapse rule in C06 (`#home-unstable`). The cooldown banner uses the top bar's centre slot (C23): `Streaming stopped: <reason> - Please wait a few moments`, one line, only the reason is shortened if it does not fit, Wi-Fi and battery hidden while it shows (`#consoles-cooldown`). Empty: `#empty-searching`, `#empty-nomatch`. Filter: `#filter`; Start opens the system keyboard (`#keyboard`), or clears an active filter.

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
Page shell (lock icon). Title "<PS5|PS4> Console Registration" with the console name and IP as a T16 sub. Prompt T20 centred at y 160: "Enter the 8-digit session PIN displayed on your <PS5|PS4>:". C17 at y 224. TextButtons at y 424: Clear digit, Cancel, Register (disabled until all 8 are filled).
**Focus**: one focus only, digits zone or buttons zone (C17). `#pin-full` opens with focus on Register and no digit highlighted. Buttons: Left/Right digit (digits zone) or button (buttons zone), Up/Down change the digit, Square clear digit, Confirm register (all 8 filled) or press the focused button, Cancel back to Home. Hints (digits): `[D-pad Digit] [Up-down Change] [Square Clear digit] [Confirm Register (dim until filled)] [Cancel Cancel]`; (buttons): `[D-pad Button] [Up-down Digits] [Confirm <focused button>] [Cancel Cancel]`. Touch: tap a digit box, tap its chevrons, tap a button.

### 3.3 Result and error popups, and the pairing result contract
One C14, used for pairing success, pairing failure and connection failure. `#result-paired` (OK tone, one button OK), `#result-pair-failed`, `#result-pair-timeout`, `#result-pair-unreachable` (ERR, Close / Try again), `#result-connect-failed` (ERR, Close / Try again). Circle closes; hints follow the focused button (C06).

**Build requirement (backend, not UI).** Today registration shows no outcome. For these popups the registration code must report one of exactly four results to the UI, each with the console it concerns:

| Result | Popup |
|---|---|
| finished OK | "Console paired" / "<name> is paired. You can connect to it now." / OK. The console becomes Paired and the list re-sorts |
| failed: PIN not accepted | "Pairing failed" / "<name> did not accept the PIN. Check the code on the console and try again." |
| failed: console unreachable | "Pairing failed" / "<name> could not be reached. Check that it is on and on the same network." |
| timeout | "Pairing failed" / "<name> did not answer in time. Open Link Device on the console again and retry." (the timeout length is an engineering choice; propose 30 s) |

Failure popups offer Close and Try again (Try again returns to the PIN screen). Connection failure shows the raw disconnect reason from the copy deck. **Mock-only trigger:** in the mock a PIN starting with 0 gives "PIN not accepted", starting with 9 gives "timeout", anything else succeeds; this is not app behaviour.

### 3.4 Connecting and Reconnecting
Page shell, no groups. Title is set **per flow**, not per stage: a local standby flow reads "Waking Console" on its first stage and "Starting Remote Play" after; a local ready flow reads "Starting Remote Play"; every internet flow reads "Starting Internet Remote Play" from first stage to last. (Today's code flips the title per stage; the flip is a bug and this is the fix.) Each title has a 32 px icon (C07). Left art: ring 128 inside a 176 spinner and a soft halo (280), x 68..388, y 168..368, centred; type logo h 32 at y 384, name T28, route T16 ("via Local Network" / "via Internet"). Right: C16 steps. Cancel TextButton right-aligned at y 440. Circle or tap Cancel stops the connect and returns Home. Standby console: wake polls until awake, then auto-connects (steps continue).

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
| Stream Stats panel | right 16, top 64 (fixed slot) | Show Latency. Latency in ms, FPS as "in / target" whole frames per second; rebuilt once per second; "N/A" when metrics are older than 3.0 s (C25) |
| Network Unstable pill | right 16, bottom 16 | Show Network Alerts; shown 5.0 s after each event, fading |

**Stream stats exist only here.** Profile has no streaming metrics (there is no menu while streaming). Exit: hold L + R + Start about 1 s (today). Select = Share and Start = Options still go to the PS5. Deep links: `#stream`, `#stream-stats`, `#unstable`, `#stream-quiet` (after the hint faded). The mock also lets Esc leave (mock only).

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
| Display | Show Latency | toggle | off (shows the stats panel in the stream overlay only; default pending CEO, see Flags) |
| Display | Show Network Alerts | toggle | on |
| Display | Show Exit Shortcut Hint | toggle | on |
| Controls | Circle Button Confirm | toggle | system default (Cross on a Western unit) |
| Advanced | Clamp Soft Restart Bitrate | toggle | on |
| Advanced | Motion during loss (artifacts) (Experimental) | toggle | off |
| Advanced | Enable Logging | toggle | off |

The mock starts with Enable PSN Internet Mode on so internet screens can be shown; the app default is off. Button map: Left/Right or Confirm change, Up/Down row, L/R group, Cancel (pane to groups, then Home). Hints: `[Confirm Toggle|Next] [D-pad Change (choice)] [L R Group] [Cancel Back]`. Touch: tap group, tap row, tap chevrons, swipe pane. `#settings-circle` turns Circle Button Confirm on: every glyph in the hint row swaps and Circle becomes confirm (use the toolbar toggle to try any screen).

### 3.7 Profile (`#profile`, `#profile-connection*`, `#profile-psn*`, ...)
Page shell, title "Profile". Groups: Account, Connection, PlayStation Network. **Identity block** (compact, always visible under the group list at y 376, 48 high): avatar (48 px circle, 2 px `LINE`, profile icon 28), PSN Account ID (MONO16, ellipsis, "Not Set" when empty) with "PlayStation Network" T16 TEXT_3 under it.

| Group | Rows |
|---|---|
| Account | Account ID (MONO16, full value), action **Refresh Account ID** (toast "Account ID refreshed from system profile" or "Could not refresh Account ID") |
| Connection | per the state table below |
| PlayStation Network | PSN Auth status row, then actions by state |

**Connection state table.** Streaming metrics (Latency, Bitrate, Packet Loss) are removed from Profile. Rows follow today's code (`ui_screens.c`, connection card) except Status, which is a deliberate fix (Flags).

| Situation | Network Type | Console | Console IP | Status | Quality | Deep link |
|---|---|---|---|---|---|---|
| Console found on the local network, ready | Local Wi-Fi | name | address | Ready | current preset | `#profile-connection` |
| Same, in rest mode | Local Wi-Fi | name | address | Standby | preset | `#profile-connection-standby` |
| Same, not paired | Local Wi-Fi | name | address | Unpaired | preset | `#profile-connection-unpaired` |
| Internet (PSN) console | PSN Internet | name | address when known (usually none) | Ready | preset | `#profile-connection-psn` |
| Manually added host | Manual Host | name | address | Ready | preset | not in the mock |
| Console selected, no discovery, PSN or manual route applies | Unavailable | name | address if known | Unavailable | preset | `#profile-connection-unavailable` |
| No console selected | Unavailable | Not selected | - | None | preset | `#profile-connection-none` |

Rules: Network Type is Local Wi-Fi when the console is discovered, else PSN Internet when its source is PSN, else Manual Host when manually added, else Unavailable. Console is the display name, else the hostname, else "Not selected". Console IP shows whenever a hostname/address is known. **Quality always shows** (it is the user's setting: 360p or 540p). **Status uses the console's real state with the Home list's words** (Ready, Standby, Unpaired, Unavailable; "None" only when no console is selected).

PSN Auth states (`#profile-psn-<state>`): Disabled (`profile-psn-disabled`, Log in disabled, description "Enable PSN internet mode in Settings", pressing it toasts "PSN internet mode is disabled in Settings"), Authenticated (`profile-psn`: Refresh hosts, Log out), Refreshing token (`profile-psn-refresh`, no actions), Awaiting browser sign-in (`profile-login`), Token expired (`profile-psn-expired`), Not authenticated (`profile-psn-none`), error text (`profile-psn-error`, e.g. "Login failed: invalid redirect URL"). Not authenticated, Token expired and error offer Log in. **Error styling**: the three red states show the value in `ERR` with a 20 px warning icon before it and a 2 px `ERR` rule at the row's left edge, so they read as errors without relying on colour alone.
**Log out**: first Confirm turns the row into "Press [Confirm] again to confirm log out" in `WARN`; second Confirm within 3.0 s logs out (toast "PSN login removed"); otherwise it resets (`#profile-logout`). Touch: two taps.
**Phone login** (`#profile-login`, `#profile-login-hidden`): the pane is replaced by "Phone Login Assist": C18 and four steps, a Code line ("Paste redirect URL/code" until a code exists) and the short sign-in URL (C18). Buttons: Start show/hide QR, Select open the Vita browser, Confirm opens the system keyboard "Paste full redirect URL" (`#keyboard-paste`; the field shows the **tail** of long text, clipped inside the field), Square cancels. Touch: tap the QR to toggle (toast feedback, C18); the four hints are tappable. Hints: `[Confirm Enter code] [Start QR] [Select Browser] [Square Cancel login]`.
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

The whole-surface popup (Triangle) shows the real current value in its subtitle ("Full Front Touch · Touchpad") and ticks it; when the 18 zones differ it reads "· Mixed" and nothing is ticked. Same rule for a multi-selection.

Mapping popup: titles "Shoulder Mapping" / "Front Touch Mapping" / "Rear Touch Mapping"; subtitle = slot name ("Front C2", "Rear A1", "Left Shoulder (L1)", "Right Shoulder (R1)", "Full Front Touch", "Full Rear Touch") or "N Zones Selected"; 11 rows: Options, Share, Touchpad, L1, L2, L3, R1, R2, R3, PS, None; current value ticked; hints `[Confirm Assign] [Cancel Cancel]`.
Summary buttons: Left/Right preset (saved at once), L/R page, Up/Down choose L1 or R1 (page 1), Confirm opens the shoulder popup (page 1) or the rear zones (page 2), **Triangle opens the zone view for the current page (new; today the zone views open by touch only, confirmed as wanted)**, Square clears the current side, Circle Home. Hints `[D-pad Preset] [L R Page] [Up-down L1 / R1] [Confirm Shoulder] [Triangle Zones] [Square Clear] [Cancel Back]` (page 2 omits the two shoulder hints).
Zone view buttons: D-pad moves the cursor; hold Confirm and move to add cells, release opens the popup (a plain tap assigns one cell); Triangle assigns the whole surface; Square clears the side; Circle returns to the summary. Hints `[D-pad Move] [Confirm Assign] [Triangle Whole surface] [Square Clear] [Cancel Back]`.
Touch: tap the preset chevrons or the label; tap a callout; tap the diagram to enter the zone view; in the zone view finger-paint cells (backtrack removes), tap one cell; tap hints. Callouts have a 48 px high hit rect that wins over the diagram. Presets seed from defaults: front all zones Touchpad, rear left three columns L2 and right three R2, L1/R1 as themselves (today's seeds; L+Square = L3, R+Circle = R3 and Select+Start = PS are combos that remain and have no editor).

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
4. Touch: front panel mapped to screen pixels.
   - **Home rows (C02)**: first tap focuses, second tap activates (they open other screens). **Settings and Profile rows (C08)**: the first tap acts (toggle flips, choice cycles, action runs) and takes focus.
   - **Swipes** (start only on the zones named, thresholds measured from the touch-down point, focus follows like the D-pad): list viewport vertical, 56 px per row; category strip (y 64-176) horizontal, 56 px per category (swipe left = next); page pane vertical, 48 px per row; list popup vertical, 48 px per row. A drag over zone cells paints a selection instead (C21).
   - A touch that moves more than 8 px is a swipe or a paint and never also a tap.
   - Tap outside a popup or the Options column cancels. Hit rects, visible rects and overlap priority are defined in 2.0; every hit rect is at least 48 x 48 (category icons 64).
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
- Banner (one line, only the reason may be shortened): Streaming stopped: <reason> - Please wait a few moments ; reasons: Console entered sleep mode, Console disconnected, or the raw reason
- Status hints: Wake signal failed; attempting connection anyway. / Wake signal failed. Check pairing and network. / Console releasing session... ready in Ns / Remote Play already active on console / Console Remote Play crashed - wait a moment / Console busy - retrying in Ns... / Missing console credentials. Re-pair may be required. / Enable PSN internet mode in settings. / PSN login required for internet remote play. / PSN session expired. Re-authenticate in Profile. / Could not determine host address. / Waiting for console network link... / Video references unstable - requesting keyframe / Rebuilding stream at safer bitrate / Persistent video desync - rebuilding session / Packet loss burst - requesting keyframe
- Network Unstable
- Options: Connect, Wake and connect, Connect via, Re-pair, Pair, Change icon; header "Options"
- Connect via: title Connect via; rows Local Network, Internet
- Re-pair (new): Re-pair <name>? / You will need to enter a new 8-digit PIN from the console. / Cancel, Re-pair
- Change icon (new): Change icon; TV, Living room, Bedroom, Dorm, Office, Another place; hints Choose, Cancel

**PIN and results**
- <PS5|PS4> Console Registration; <name> (<ip>); Enter the 8-digit session PIN displayed on your <PS5|PS4>:
- Buttons: Clear digit, Cancel, Register; hints Digit, Change, Clear digit, Register, Cancel
- Results (new): Console paired / <name> is paired. You can connect to it now. / OK ; Pairing failed / <name> did not accept the PIN. Check the code on the console and try again. | <name> could not be reached. Check that it is on and on the same network. | <name> did not answer in time. Open Link Device on the console again and retry. / Close, Try again ; Could not connect / <name>: <reason> / Close, Try again

**Connecting**
- Titles: Waking Console, Starting Internet Remote Play, Starting Remote Play; hint Cancel; via Local Network, via Internet
- Stages and details: section 3.4
- Reconnecting: Optimizing Stream, Recovering from packet loss, Retrying at X.XX Mbps, Attempt N, Please wait...

**Stream**: Back to menu: Hold L + R + Start ; Stream Stats, Latency, FPS (N ms, in / target, N/A) ; Network Unstable

**Settings**: group names Video, Network, Display, Controls, Advanced; row labels and values in section 3.6; descriptions (new, draft for sign-off): Quality Preset "Video resolution requested from the console."; Latency Mode "Sets the target bitrate. Higher looks better but needs a stronger connection."; FPS Target "Frame rate requested from the console."; Force 30 FPS Output "Output video at 30 FPS."; Fill Screen "Stretch the video to fill the whole screen."; Auto Discovery "Find consoles on your network automatically. Takes effect the next time the app starts."; Enable PSN Internet Mode "Connect to your consoles over the internet with your PSN account."; Show Only Paired "Hide consoles that are not paired."; Show Latency "Show latency and frame rate in the stream overlay."; Show Network Alerts "Show a badge when the connection becomes unstable."; Show Exit Shortcut Hint "Show how to leave the stream when it starts."; Circle Button Confirm "Use Circle to confirm and Cross to go back, on every screen."; Clamp Soft Restart Bitrate "Limit the bitrate when the stream restarts after packet loss."; Motion during loss "Keep motion going while packets are lost. May show visual artifacts."; Enable Logging "Write diagnostic logs on the Vita for troubleshooting."; On, Off; hints Toggle, Next, Change, Group, Back, Open

**Profile**: Account, Connection, PlayStation Network; identity block: PSN Account ID, PlayStation Network; Account ID, Not Set, Refresh Account ID ("Read the Account ID again from the system profile." new); Network Type (Local Wi-Fi, PSN Internet, Manual Host, Unavailable), Console (Not selected), Console IP, Status (Ready, Standby, Unpaired, Unavailable, None), Quality; PSN Auth; Disabled, Authenticated, Refreshing token, Awaiting browser sign-in, Token expired, Not authenticated, <error text>; Log in, Log out, Refresh hosts; Press [Confirm] again to confirm log out ; Phone Login Assist; 1 Press [Start] to show or hide the QR code; 2 Scan the QR code with your phone and sign in; 3 Press [Confirm] and paste the redirect URL or code; 4 [Select] opens the Vita browser instead; Code: Paste redirect URL/code; URL: my.account.sony.com/sso/ca/authorize (short form) ; QR hidden ; hints Enter code, QR, Browser, Cancel login, Confirm log out, Refresh
- Descriptions: "Enable PSN internet mode in Settings" (today); "Sign in with your phone. Needed for internet Remote Play." (new); "Reload your internet-capable consoles." (new); "Remove the saved PSN login from this Vita." (new)
- Toasts (all): Account ID refreshed from system profile / Could not refresh Account ID / PSN internet mode is disabled in Settings / Scan QR on phone, then press [Confirm] to paste the full redirect URL / PSN login could not start / PSN internet host list refreshed / PSN internet host refresh failed / PSN login canceled / QR shown. Scan it with your phone. (reworded from "QR shown...") / QR hidden. Press Start to show it again. (reworded) / Opened browser fallback. Phone QR is still recommended. / Could not open browser. Use the phone QR. / Could not open text input / No URL/code entered / PSN login complete / PSN login failed / PSN login removed
- System keyboard titles: Filter Consoles; Paste full redirect URL

**Controller**: Controller; Front Touch; Rear Touch; Custom 1/2/3; Your first / second / third custom mapping; Page 1/2 · Buttons; Page 2/2 · Back Touch; N zones; Zone <A1..F3>; N Zones Selected; callouts "L1 -> <output>", "R1 -> <output>"; popup titles and subtitles in section 3.8; outputs Options, Share, Touchpad, L1, L2, L3, R1, R2, R3, PS, None; hints Preset, Page, L1 / R1, Shoulder, Zones, Clear, Back, Move, Assign, Whole surface, Assign, Cancel

---

## 6. Flags for CEO

Decided (no action): Show Navigation Labels dropped; the third Profile card was dead code and is removed; Triangle / Cross on page 2 open the zone views; Roboto Light is loaded; top bar keeps Wi-Fi, battery and clock, with no time-of-day tint; the internet title no longer flips mid-flow; overlay deviations accepted (fixed stats slot, 16 px margin, square pills, 3.0 s toast); Profile streaming metrics removed; result popups kept.

Open:
1. **Show Latency default and scope (CEO pending).** With Profile metrics gone, Show Latency only controls the stream overlay stats panel. Default stays off (today); say so if you want it on, or if it should be renamed (for example "Show Stream Stats").
2. **Pairing results need backend work.** The result popups need registration to report finished OK, PIN not accepted, console unreachable or timeout to the UI (3.3). Today there is no success or failure UI and the registration code may not distinguish these. Timeout length proposed at 30 s. If the code cannot tell "unreachable" from "PIN not accepted", collapse to one failure copy.
3. **Login URL shown in a short form** (`my.account.sony.com/sso/ca/authorize`). Today the app prints up to 2 lines of the full authorize URL and silently drops the tail; the full URL stays in memory for the QR and browser. Typing the full URL by hand was never practical.
4. **Profile Status is a deliberate fix.** Today it says "Ready" or "Ready / Not Registered" even for a console that is asleep or unreachable. The spec shows the console's real state (Ready, Standby, Unpaired, Unavailable, None) in the same words as the Home list. Everything else in the Connection group matches today's code.
5. **Rear controller art edited.** `controller_back_clean.png` removes the tiny "Sony Computer Entertainment Inc" line (unreadable at that size, off-brand for our app). Ship the clean copy; the original stays in `assets/`.
6. **New copy needs sign-off**: all Settings descriptions, Profile action descriptions, Re-pair confirm, Change icon popup, the pairing and connection result popups, the three pairing failure reasons.
7. **Room icon needs storage**: one small integer per console in the host config (default TV).
8. **PIN title and prompt are model aware** ("PS4"/"PS5"); today they say PS5 always.
9. **Unpaired consoles** show Pair and Change icon in Options (Connect and Re-pair would both open the PIN screen). Cooldown disables Connect and Connect via.
10. **Console type is only in the detail panel**, not on list rows (locked). The earlier "Last session" line is removed (the app has no such data).
11. **System reads for the top bar**: Wi-Fi state, battery percent, local time, polled about once per second.
12. **Status hints are never truncated**: they wrap to two lines and rows grow. All 16 current messages fit; a longer future message needs rewording.
13. **Mock-only affordances** (not app behaviour): Esc leaves the stream; the toolbar Circle toggle; the pairing trigger digits (first digit 0 or 9); the fictional stream picture; the illustrative QR; the stand-in for the system keyboard.
