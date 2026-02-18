# PS Button Dual-Mode Feasibility (Issue #52 Follow-up)

## Summary

Dual-mode is partially active in VitaRPS5, but current app-level interception is not reliable enough to prevent the Vita shell PS action in all runs.

Latest evidence from testing logs:
- `21570378498_vitarps5-testing.log:603` -> `PS dual mode init: config=1 intercept_original=0`
- `21570378498_vitarps5-testing.log:909` -> `PS dual mode: first tap armed`
- No subsequent `single tap timeout` or `double tap confirmed` markers in this run.

This means the state machine starts, but the user-observed system behavior is still wrong (single press can still trigger Vita UI).

## What Adrenaline Actually Does

Adrenaline does not rely on userland-only pad polling:
- Reads PS button through a kernel-assisted path (`kuCtrlPeekBufferPositive`) in user code:
  - `/tmp/Adrenaline/user/utils.c:100`
  - `/tmp/Adrenaline/user/utils.c:224`
- Provides that API from kernel module code:
  - `/tmp/Adrenaline/kernel/main.c:138`
- Uses double-click detection based on that source:
  - `/tmp/Adrenaline/user/utils.c:217`
  - `/tmp/Adrenaline/user/menu.c:519`
- Also locks shell PS behavior in runtime paths:
  - `/tmp/Adrenaline/user/main.c:373`
  - `/tmp/Adrenaline/user/main.c:395`

## Key Gap vs VitaRPS5

VitaRPS5 currently only uses:
- `sceCtrlSetButtonIntercept()` / `sceCtrlGetButtonIntercept()` in `vita/src/host_input.c`

It does not currently use shell lock APIs:
- `sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN / _PS_BTN_2)` from VitaSDK (`psp2/shellutil.h`)

This is a critical gap because shell lock is the closest userland mechanism to explicitly suppress system PS action.

## Feasibility Matrix

1. **Current app-only intercept (existing)**
- Pros: no new dependency, no install friction
- Cons: observed unreliable; fails user expectation in real runs
- Verdict: insufficient alone

2. **Userland + shell lock (recommended first implementation)**
- Pros: no kernel plugin required; uses official Vita userland API
- Cons: behavior may vary by firmware/shell state; must be carefully unlocked on exit
- Verdict: best next step with lowest complexity/risk

3. **Optional kernel plugin backend (taiHEN-style)**
- Pros: highest control, closest to Adrenaline model
- Cons: major support/install burden, firmware/homebrew constraints, larger maintenance/test surface
- Verdict: fallback path only if shell-lock approach still cannot guarantee behavior

## Recommended Implementation Blueprint (Next PR)

### Scope
Implement **shell lock hardening** first, keep existing dual-mode state machine, and add explicit diagnostics.

### File Changes
1. `vita/src/host_input.c`
- Include `<psp2/shellutil.h>`.
- Add helper wrappers:
  - lock PS shell action
  - unlock PS shell action
- When dual mode is active, lock PS shell action while stream input thread is active.
- On thread shutdown and on dual-mode disable path, always unlock (guarded cleanup).
- Keep `sceCtrlSetButtonIntercept()` logic and existing state-machine behavior intact.
- Add diagnostics for lock/unlock success/failure and active state transitions.

2. `vita/CMakeLists.txt`
- Link `SceShellSvc_stub` (required for `sceShellUtilLock/Unlock`).

### Runtime Rules
- Default behavior remains unchanged when `ps_button_dual_mode=false`.
- When `ps_button_dual_mode=true`:
  - enable intercept path (existing)
  - engage shell PS lock during active streaming/input handling
  - preserve existing single/double press semantics
  - release lock on stream stop/teardown

### Validation Plan
Build:
- `./tools/build.sh --env testing`

On-device tests:
1. Dual mode OFF:
- PS button behaves as current default behavior.

2. Dual mode ON, single press:
- Expected logs:
  - init config=1
  - first tap armed
  - single tap timeout, sending remote PS
- Expected behavior:
  - Vita app switch UI does not appear
  - remote PS action is sent

3. Dual mode ON, double press:
- Expected logs:
  - second press detected, passthrough active
  - double tap confirmed (Vita local), restoring intercept
- Expected behavior:
  - local Vita PS action works on double press

4. Stream stop / disconnect:
- Confirm shell lock is always released (no sticky lock after app exit/stop).

## Escalation Criteria for Plugin Path

Move to optional plugin backend only if all are true:
1. Shell lock + intercept still leaks single-press Vita UI in reproducible runs.
2. Failures are observed across multiple sessions with clear logs.
3. User agrees to optional plugin install/support model.

If escalation is needed, define a minimal plugin interface (single/double PS event bridge) and keep app fallback behavior for non-plugin users.
