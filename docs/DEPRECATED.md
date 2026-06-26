# Deprecated Work

## 2026-06-26

### Epic: Dead-Stream Watchdog (PR #196) – Reverted
**Original Goal:** Detect streaming session death on PS Vita via socket-level monitoring and trigger graceful session teardown before PS5 releases the old session

**Reason Deprecated:** Critical regression identified during hardware A/B testing (log `20639381559`, build commit `6989b93`)
- **Root Cause 1 (Critical):** Torn 64-bit read on ARMv7. `last_decoded_frame_us` (`uint64_t`) is written by decode thread and read by UI thread with no lock/atomic on arm-vita-eabi (32-bit ARM). UI thread encountered a torn read where the high and low 32-bit words came from different write cycles, resulting in a timestamp value > now. Unsigned integer underflow computed stall as 18446744073709550 ms while stream was healthy at 30 fps.
- **Root Cause 2 (Secondary):** RP_IN_USE reconnect rejection. The spurious teardown triggered an immediate reconnect that PS5 rejected (`Remote is already in use`, code=4) because it hadn't released the old session yet — resulting in a self-inflicted ~14 second outage on a healthy stream.

**Why This Approach Failed:**
- App-level teardown based on frame timing is inherently racy on asymmetric threading models
- 32-bit ARM platforms cannot safely do 64-bit non-atomic reads across threads
- PS5 session release is asynchronous; reconnect attempts before release fail with RP_IN_USE

**Alternative Approach:** Implement lib-side suspend/resume detection via transport-layer socket monitoring (ENOBUFS/EBADF escalation in Takion send path; DISCONNECT-during-streaming path in streamconnection.c) to detect socket death and drive a clean session teardown. This moves detection to the network layer where socket state is already observable without thread-safe timestamp comparisons.

**Planned Tasks (Not Completed):**
- App-level frame stall watchdog implementation
- PS5 session reconnect logic
- UI messaging for detected freezes

**Branch:** `fix/dead-stream-watchdog` (kept on remote for reference)

**Pull Request:** PR #196 (closed without merge on 2026-06-26)

---

## Previous

No deprecated epics or tasks at this time.

All planned work from SCOPING_UI_POLISH.md has been either completed or is in active development. Panel transition animations deferred to next phase but not deprecated (awaiting input from design review).
