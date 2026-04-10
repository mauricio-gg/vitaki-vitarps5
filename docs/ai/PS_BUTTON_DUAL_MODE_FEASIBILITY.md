# PS Button Dual-Mode Feasibility (Issue #52 Follow-up)

## Summary

The current direction is no longer the original bridge-first experiment.
This branch now targets a DSVita-style privileged userland path:

- build the main Vita app SELF with elevated app attributes
- use shell lock while streaming is active
- read `SCE_CTRL_PSBUTTON` from normal pad polling
- single tap waits out a short double-tap window and then sends remote `PS`
- double tap yields PS handling to the Vita shell so the user can leave the app normally

This is a better fit for VitaRPS5 than immediate recapture after the second tap,
because immediate recapture hijacks the user's attempt to exit the app.

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

## What DSVita Actually Proves

DSVita shows that successful PS-button capture does not require a bespoke PS kernel bridge in the main path.

The relevant pieces are:
- elevated app SELF packaging (`-a 0x2800000000000001 -na`)
- `sceShellUtilLock(...)` / `sceShellUtilUnlock(...)`
- direct polling of `SCE_CTRL_PSBUTTON` from normal controller reads

That makes the kernel-bridge path a fallback, not the preferred first solution.

## Feasibility Matrix

1. **Intercept-only**
- Pros: low complexity
- Cons: already shown unreliable in real runs
- Verdict: not enough

2. **Privileged userland + shell lock (current target)**
- Pros: closest to DSVita, no separate plugin install, keeps distribution simple
- Cons: requires elevated app SELF packaging and hardware validation
- Verdict: preferred implementation path

3. **Kernel bridge fallback**
- Pros: stronger control if privileged polling still leaks
- Cons: more complexity and support burden
- Verdict: keep only as fallback material

## Recommended Implementation Blueprint (Next PR)

### Scope
Implement the privileged userland route first and make double tap yield to the Vita shell instead of re-capturing immediately.

### File Changes
1. `vita/src/host_input.c`
- Use shell lock as the primary PS capture mechanism while streaming.
- Detect single-vs-double tap from raw PS edges seen through normal pad polling.
- On single tap timeout, emit one remote `PS` button event.
- On double tap, unlock shell handling and keep it released long enough for the Vita shell to take over cleanly.
- Do not immediately re-lock on second-tap release; defer relock until after a grace period.
- On thread shutdown and dual-mode disable, always unlock and clear state.

2. `vita/CMakeLists.txt`
- Build the app SELF with DSVita-style elevated app attributes and `NOASLR`.

### Runtime Rules
- Default behavior remains unchanged when `ps_button_dual_mode=false`.
- When `ps_button_dual_mode=true`:
  - engage shell PS lock during active streaming/input handling
  - single press waits for the double-tap window, then sends remote `PS`
  - double press yields to the Vita shell so the user can exit the app
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
  - second press detected, yielding to Vita shell
- Expected behavior:
  - Vita shell/app-switch UI appears
  - user can leave the app without dual mode immediately stealing PS back

4. Stream stop / disconnect:
- Confirm shell lock is always released (no sticky lock after app exit/stop).

## Escalation Criteria for Plugin Path

Move to optional plugin backend only if all are true:
1. Privileged shell-lock path still leaks single-press Vita UI in reproducible runs.
2. Failures are observed across multiple sessions with clear logs.
3. User agrees to optional plugin install/support model.

If escalation is needed, define a minimal plugin interface (single/double PS event bridge) and keep app fallback behavior for non-plugin users.
