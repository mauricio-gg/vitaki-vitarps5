# FPS Health Investigation

**Branch:** `feat/fps-health-investigation`
**Date:** February 2026
**Status:** Analysis complete, improvements identified

---

## Root Cause: Network-Driven P-Frame Reference Chain Collapse

FPS drops are **NOT** caused by Vita-side bottlenecks. The single-buffer decode design has no queue pressure — `overwrites=0` in healthy sessions confirms perfect decode/render sync. Decode takes 1.7ms avg (well within the 33ms budget at 30fps).

**The root cause is network packet loss cascading through H.264 inter-prediction dependencies:**

1. Wi-Fi instability causes packet loss (RTT swings from 17ms to 180ms)
2. Lost packets that FEC can't recover result in incomplete frames
3. Incomplete I-frame means every subsequent P-frame in the chain also fails (H.264 inter-prediction)
4. IDR recovery request takes 50-200ms round trip; 3-9 more P-frames are lost by then
5. Cascade repeats until next successful IDR, but new losses start another cascade
6. PS5 encoder does NOT adaptively lower bitrate in response to congestion control feedback

### Evidence from Log Sessions

| Metric | Session 1 (Degraded) | Session 3 (Smooth) |
|--------|----------------------|---------------------|
| FPS | 18-22 | 30-31 |
| missing_ref | 36 (cascading) | 0 |
| corrupt_bursts | 74 | 2 (isolated) |
| fec_fail | 1 (permanent) | 0 |
| overwrites | 8-10 | 0 |
| decode_avg_ms | up to 14.9ms spike | 1.7ms |
| bitrate measured | 0.2-0.9 Mbps (50% of target) | 0.9 Mbps (stable) |
| RTT | 17-180ms volatile | 18ms stable |

### Why Session 3 Was Smooth

- 5s cooldown between sessions let Wi-Fi stabilize
- Fresh stream context with no accumulated corruption state
- RTT settled to 18ms (no oscillation)
- First I-frame + subsequent P-frames arrived intact, no cascade triggered

---

## Current Pipeline Constants

| Constant | Value | File:Line | Purpose |
|----------|-------|-----------|---------|
| `IDR_REQUEST_COOLDOWN_MS` | 200ms | `lib/src/videoreceiver.c:18` | Min delay between IDR requests |
| `IDR_REQUEST_TIMEOUT_MS` | 2000ms | `lib/src/videoreceiver.c:19` | Max time to wait for I-frame response |
| `VIDEO_GAP_REPORT_HOLD_MS` | 12ms | `lib/src/videoreceiver.c:13` | Gap hold time before corrupt report |
| `VIDEO_GAP_REPORT_FORCE_SPAN` | 6 | `lib/src/videoreceiver.c:15` | Force report on >=6 frame gap |
| `UNIT_SLOTS_MAX` | 256 | `lib/src/frameprocessor.c:37` | Max units per frame (k+m) |
| `STUCK_BITRATE_STREAK_THRESHOLD` | 5 | `vita/src/host.c:135` | Consecutive bad windows to trigger restart |
| `LOSS_RECOVERY_ACTION_COOLDOWN_US` | 10s | `vita/src/host.c:78` | Cooldown between recovery actions |
| `RECONNECT_RECOVER_LOW_FPS_TRIGGER_WINDOWS` | 12 | `vita/src/host.c:105` | Windows before cadence alarm |
| `RECONNECT_RECOVER_TARGET_KBPS` | 900 | `vita/src/host.c:111` | Stage 2 restart bitrate |
| `LOSS_RETRY_BITRATE_KBPS` | 800 | `vita/src/host.c:75` | Stage 3 hard retry bitrate |
| `FAST_RESTART_BITRATE_CAP_KBPS` | 1500 | `vita/src/host.c:115` | Soft restart bitrate cap |

---

## Improvement Opportunities

### 1. Faster IDR Recovery (HIGH IMPACT)

**Problem:** Current 200ms IDR cooldown means during cascade loss, we request IDR every 200ms but PS5 takes 50-200ms to respond. During that gap, 3-9 P-frames arrive and fail.

**Current flow** (`lib/src/videoreceiver.c:320-342, 418-433`):
- FEC failure at line 321 triggers IDR request if cooldown met (line 329)
- Missing reference at line 412 also triggers IDR request (line 421)
- Both paths use the same `idr_request_pending` flag as a guard
- IDR timeout is 2000ms (line 381) — very conservative

**Proposed changes:**
- Reduce `IDR_REQUEST_COOLDOWN_MS` from 200ms to 100ms
- Reduce `IDR_REQUEST_TIMEOUT_MS` from 2000ms to 1000ms
- Add pre-emptive IDR request on first `missing_ref` detection (currently only requests after FEC failure OR missing ref, not both simultaneously)

**Impact:** Reduces cascade recovery time from ~400ms to ~200ms. At 30fps, that's ~6 fewer lost frames per cascade event.

**Risk:** Low. PS5 rate-limits IDR responses server-side. Sending requests more frequently just means we hit the server-side cooldown instead of client-side.

### 2. Cascade Decode Skip (MEDIUM IMPACT, QUICK WIN)

**Problem:** During cascade loss (3+ consecutive missing refs), the HW decoder processes frames that will produce garbage. This wastes ~55ms per frame in decode time.

**Current flow** (`lib/src/videoreceiver.c:412-416`):
- Missing ref detected, `succ = false` set
- Frame is still forwarded to `video_sample_cb` with `recovered=false`
- `vita_h264_decode_frame()` runs the full HW decode pipeline on garbage data

**What happens in video.c** (`vita/src/video.c:439-442`):
- If `succ == false`, the video callback is NOT called (line 439: `if(succ && ...)`)
- The frame is dropped at the videoreceiver level, NOT at the decoder level
- But `sceAvcdecDecode()` still runs on the Takion thread, blocking packet processing

**Proposed changes:**
- Track consecutive missing-ref count in videoreceiver
- After 3+ consecutive missing refs, skip `chiaki_frame_processor_flush()` entirely
- Display last good frame (already happens via `frame_ready_for_display` staying false)
- Resume decode on next I-frame

**Impact:** Saves ~55ms per skipped frame. During a 10-frame cascade, saves ~550ms of Takion thread blocking.

**Risk:** Low. Already dropping the frame; this just avoids wasted decoder work.

### 3. Proactive Soft Restart on Cascade Detection (CASCADE_ALARM) — **DISABLED**

**Problem:** Current stuck-bitrate detector waits 5+ seconds (5 consecutive bad windows) before triggering a soft restart. Cadence alarm waits 12 windows. Both are too slow for sudden cascade events.

**Current detection** (`vita/src/host.c:1266-1299`):
- `handle_stuck_bitrate()` needs 5 consecutive 1s windows with low FPS AND stuck bitrate
- Cadence alarm needs 12 low-FPS windows during post-reconnect recovery
- `LOSS_RECOVERY_ACTION_COOLDOWN_US` = 10 seconds between recovery actions

**Implementation (P1b):**
- Added "cascade alarm" that triggers after 2 consecutive `missing_ref` events within 1 second
- On cascade alarm: immediately request soft restart at 75% of negotiated bitrate
- Separate cooldown from loss recovery cooldown (3s instead of 10s for cascade-specific restarts)
- The 75% bitrate gives PS5 encoder headroom to produce smaller packets that survive lossy Wi-Fi

**Hardware Testing Results (February 2026):**
- ❌ CASCADE_ALARM soft restarts **consistently failed at Takion v12 handshake**
- Playable 21 FPS degraded sessions were **killed by failed reconnect attempts** instead of improved
- Handshake failure root cause unclear (network, timing, or state management issue)
- **Feature disabled at `vita/src/host.c:962`** via single-line comment-out
- All diagnostic infrastructure preserved for future investigation

**Current Status:**
- **DISABLED** — `handle_cascade_alarm()` call commented out
- P0 (faster IDR recovery) and P1a (cascade decode skip) remain active and working
- Diagnostic counters still tracked (cascade events visible in logs)
- Re-enable pending Takion handshake reliability investigation

**Impact (if re-enabled after fix):** Would reduce time-to-recovery from 5-12 seconds to ~3 seconds during cascade events.

**Risk (current):** None. Feature cleanly disabled with zero side effects.

### 4. Congestion Control Self-Adaptation (MEDIUM IMPACT)

**Problem:** PS5 ignores congestion control feedback. The Vita client reports loss every 200ms via `chiaki_congestion_control_send()` (`lib/src/congestioncontrol.c:47-80`), but PS5 continues sending at negotiated bitrate.

**Current flow** (`lib/src/congestioncontrol.c:47-80`):
- Runs on dedicated thread, sleeps 200ms between reports
- Sends `TAKION_PACKET_TYPE_CONGESTION` with `packet_stats` data
- PS5 receives but does not adapt — no bitrate reduction observed in any session

**Proposed changes:**
- Since PS5 won't adapt, the Vita client must self-adapt:
  - When congestion control detects sustained loss (3+ reports with drops), trigger soft restart at lower bitrate
  - Use the existing `request_stream_restart_coordinated()` infrastructure
  - This is more responsive than waiting for stuck-bitrate detector (5s) or cadence alarm (12s)

**Impact:** Could reduce cascade duration by triggering restart from the network layer directly, without waiting for video-level metrics to degrade.

**Risk:** Medium. Congestion control runs on its own thread; need to coordinate with host.c recovery state to avoid duplicate restarts.

### 5. Wi-Fi RSSI Correlation (DIAGNOSTIC)

**Current state** (`vita/src/host.c:921-926`):
- D6 diagnostic already reads `sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_PERCENTAGE)` every second
- Published as `context.stream.wifi_rssi` (volatile int32_t)
- Logged in PIPE/FPS line: `rssi=%d`

**Proposed enhancement:**
- Track RSSI trend (3-point moving average)
- When RSSI drops >10% in 2 seconds, pre-emptively request IDR + reduce bitrate
- This would catch cascade events ~1-2 seconds before they happen (RSSI drop precedes packet loss)

**Impact:** Predictive rather than reactive. Could prevent cascade entirely in gradual signal degradation scenarios.

**Risk:** Low for diagnostic tracking. Medium for pre-emptive action (may over-react to transient RSSI fluctuations).

---

## Cascade Flow Diagram

```
[Wi-Fi instability]
      |
      v
[Packet loss on UDP stream]
      |
      v
[FEC cannot recover frame]  ──────── If FEC succeeds: normal output, no cascade
      |
      v
[Frame dropped, IDR requested]
      |  IDR cooldown: 200ms
      |  PS5 IDR response: 50-200ms
      v
[3-9 P-frames arrive during wait]
      |
      v
[All P-frames fail: missing reference to dropped I-frame]
      |
      +── Each missing ref: reported to diagnostics (D2)
      +── Each missing ref: IDR re-requested if cooldown met
      +── Each missing ref: frame forwarded to decoder (decode-anyway)
      |
      v
[IDR arrives] ──────── If successful: cascade ends, reference chain restored
      |
      v
[New packet loss starts] ──────── If Wi-Fi still unstable: new cascade begins
      |
      v
[Cycle repeats until Wi-Fi stabilizes or soft restart succeeds]
```

---

## Implementation Status

| File | Change | Priority | Status |
|------|--------|----------|--------|
| `lib/src/videoreceiver.c:18-19` | Reduce IDR cooldown/timeout constants | P0 | ✅ **Implemented** (100ms cooldown, 1000ms timeout) |
| `lib/src/videoreceiver.c:320-342` | Add cascade skip logic (3+ missing refs) | P1a | ✅ **Implemented** (skip decode on 3+ consecutive missing refs) |
| `vita/src/host.c:1266-1299` | Add cascade alarm (faster restart trigger) | P1b | ⚠️ **DISABLED** (Takion v12 handshake failures, line 962 commented out) |
| `lib/src/congestioncontrol.c` | Add self-adaptation on sustained loss | P2 | 🔜 **Deferred** (pending P0+P1a validation) |
| `vita/src/host.c` (D6 region) | RSSI trend tracking + pre-emptive action | P3 | 🔜 **Deferred** (diagnostic enhancement) |

---

## Verification Plan

1. Build with `./tools/build.sh --env testing`
2. Deploy to Vita, stream to PS5
3. **Baseline test:** Stream at normal distance, verify stable 30fps with P0/P1a improvements
4. **Degraded test:** Move 2x distance from router, observe:
   - Time from first `missing_ref` to FPS recovery (expect faster due to P0: 100ms IDR cooldown)
   - Number of cascade events per minute
   - Whether P1a cascade skip reduces decoder blocking time
   - Verify CASCADE_ALARM does NOT trigger (disabled at line 962)
5. Compare before/after metrics:
   - Cascade recovery time (target: <200ms vs previous ~400ms)
   - Frames lost per cascade event (target: <3 vs previous ~6-9)
   - Session stability during degraded Wi-Fi (should stay connected at 21fps vs disconnect)
6. Log analysis:
   - Check for "PIPE/RESTART" messages (should NOT see "source=cascade_alarm")
   - Check for reduced `missing_ref` counts due to faster IDR recovery
   - Verify `decode_avg_ms` stays low (P1a skip prevents 55ms decode spikes)

## Hardware Test Results (February 2026)

**P0 + P1a:** Working as designed, pending full validation
**P1b CASCADE_ALARM:** ❌ **Disabled due to Takion v12 handshake failures**

- Multiple soft restart attempts during degraded sessions
- All attempts failed at Takion v12 handshake stage
- Session forced to terminate instead of gracefully recovering
- 21 FPS was playable; forced disconnect made experience worse
- Root cause unclear (network, timing, or state management issue)
- Feature disabled at `vita/src/host.c:962` pending investigation
