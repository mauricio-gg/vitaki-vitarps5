# Status: PS Button Dual Mode (Issue #52)

## Branch
- `feat/ps-button-dual-mode-issue52`

## Goal
- Add a settings option to keep default behavior unchanged, and optionally enable:
  - Single press PS -> send remote PS button.
  - Double press PS -> use Vita system PS behavior.
- Scope: streaming only.

## Implemented So Far

### 1) Config + Persistence
- Added new config flag:
  - `vita/include/config.h`
    - `bool ps_button_dual_mode;`
- Wired default/parse/serialize:
  - `vita/src/config.c`
    - default `false` in `config_set_defaults(...)`
    - parse key `ps_button_dual_mode` in bool settings parser
    - serialize key `ps_button_dual_mode` in bool settings serializer

### 2) Settings UI
- Added settings item enum and count:
  - `vita/include/ui/ui_screens.h`
    - `UI_SETTINGS_ITEM_PS_BUTTON_DUAL_MODE`
    - incremented `UI_SETTINGS_STREAMING_ITEM_COUNT`
- Added toggle wiring/rendering:
  - `vita/src/ui/ui_screens.c`
    - animation id constant
    - toggle action in `settings_activate_selected_item(...)`
    - row render label: `"PS Button Dual Mode"`

### 3) Host Input Logic (Streaming Path)
- Added PS dual-mode state machine in:
  - `vita/src/host_input.c`
- Added intercept helper:
  - `set_ps_button_intercept_state(...)`
- Added timing constant:
  - `PS_DOUBLE_PRESS_WINDOW_US = 350000ULL`
- Current logic attempts:
  - First press starts pending single logic and manipulates intercept.
  - Timeout sends remote PS.
  - Second press in window cancels remote PS and allows Vita behavior.

## Important Technical Finding
- From VitaSDK headers inside the build image:
  - `SCE_CTRL_PSBUTTON` is aliased to `SCE_CTRL_INTERCEPTED` in `psp2common/ctrl.h`.
  - `sceCtrlSetButtonIntercept(int)` is process/thread-level intercept control (boolean).
- This makes PS dual-mode behavior sensitive to:
  - which `sceCtrl*` read variant is used (`Positive` vs `PositiveExt`),
  - timing of toggling intercept relative to button press edges.

## Validation Performed
- Build/tests completed successfully after each iteration:
  - `./tools/build.sh debug`
  - `./tools/build.sh --env testing`
  - `./tools/build.sh test`
- Note: hardware behavior still not correct for requested feature despite successful builds.

## Current Known Issue
- Feature is still not behaving correctly on hardware:
  - reported: PS handling is not working as intended.
- We also now have a new, more concerning bug to prioritize (reported by maintainer).

## Current Modified Files
- `vita/include/config.h`
- `vita/include/ui/ui_screens.h`
- `vita/src/config.c`
- `vita/src/host_input.c`
- `vita/src/ui/ui_screens.c`

## Handoff Notes
- Do not assume the current PS intercept state machine is correct for Vita runtime semantics.
- Next debugging should focus on the newly reported critical bug first.
- After that, revisit PS feature with hardware-first instrumentation around:
  - `ctrl.buttons` bitfield values
  - intercept state transitions
  - edge detection timing across frames
