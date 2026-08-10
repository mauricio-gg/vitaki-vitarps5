# Latency Optimization Analysis

**Status:** In Progress
**Priority:** High
**Issue:** #5

---

## Problem Statement

Current streaming latency is higher than desired, especially on remote connections. This significantly impacts user experience, particularly for fast-paced games.

**Related Issues:**
- Upstream: https://github.com/ywnico/vitaki-fork/issues/12

---

## Current Pipeline Overview

```
PS5 → Network → Vita Receive → Video Decode → Display
                              → Audio Decode → Audio Output
              ← Input Encode ← Input Thread ←
```

---

## Areas to Investigate

### 1. Video Pipeline (`vita/src/video.c`)

**Current Implementation:**
- Uses hardware decoder (SceVideodec)
- H.264 bitstream processing
- Frame texture upload to vita2d

**Potential Optimizations:**
- [ ] Verify hardware acceleration is properly utilized
- [ ] Check buffer sizes and queuing
- [ ] Analyze frame pacing
- [ ] Review decode-to-display latency
- [ ] Check for unnecessary memcpy operations
- [ ] Verify optimal SceVideodec configuration

**Key Files:**
- `vita/src/video.c` - Main video decoding logic
- `vita/src/video.h` - Video interface

---

### 2. Audio Pipeline (`vita/src/audio.c`)

**Current Implementation:**
- Audio decoding and playback
- Buffer management

**Potential Optimizations:**
- [ ] Check audio buffer sizes
- [ ] Verify synchronization with video
- [ ] Analyze audio thread scheduling
- [ ] Check for buffer underruns/overruns
- [ ] Review audio latency configuration

**Key Files:**
- `vita/src/audio.c` - Audio handling

---

### 3. Network Layer

**Current Implementation:**
- Uses Chiaki network stack
- UDP-based streaming protocol

**Potential Optimizations:**
- [ ] Analyze packet receive buffering
- [ ] Check for network thread priority
- [ ] Review packet processing efficiency
- [ ] Verify optimal MTU settings
- [ ] Check for unnecessary data copies

**Key Files:**
- Network handling through Chiaki library
- `vita/src/host.c` - Connection management

**Implemented (GH #208, ENOBUFS session-freeze fix):**
- `takion_recv()` (`lib/src/takion.c`) previously treated a transient `ENOBUFS`
  from `recv()` on the Takion UDP socket as fatal, killing the recv thread.
  It now retries the recv with a short sleep (`TAKION_RECV_TRANSIENT_RETRY_DELAY_MS`,
  5ms), escalating to a hard failure only after `TAKION_RECV_TRANSIENT_MAX_CONSECUTIVE`
  (400) *consecutive* transient errors on the blocking recv call -- intentionally
  count-only rather than time-windowed, so long idle gaps between packets can't
  falsely trip escalation. The non-blocking drain-loop call stays a zero-cost
  poll: it counts the error but never sleeps.
- The resulting `DISCONNECT` event is now propagated mid-stream by
  `stream_connection_takion_cb()` (`lib/src/streamconnection.c`) instead of
  being silently ignored outside the initial connect handshake, so the session
  no longer freezes waiting on a dead transport. Sender threads racing the
  teardown are stopped by invalidating `takion->sock` before the fd is closed,
  instead of flooding `EBADF`.

---

### 4. Input Handling (`vita/src/host.c`)

**Current Implementation:**
- Polls at 5ms intervals
- Reads controller, touch, motion
- Sends to PS5

**Potential Optimizations:**
- [ ] Reduce input polling interval
- [ ] Optimize input encoding
- [ ] Verify input thread priority
- [ ] Check for input buffering delays
- [ ] Measure input-to-send latency

**Key Files:**
- `vita/src/host.c:170-450` - `input_thread_func()`

---

## Measurement Strategy

### Latency Components to Measure

1. **Input-to-Send:** Time from button press to network send
2. **Network RTT:** Round-trip time to PS5
3. **Video Receive-to-Decode:** Time from packet receive to decode start
4. **Video Decode-to-Display:** Time from decode complete to screen update
5. **Audio Receive-to-Play:** Time from packet receive to audio output

### Proposed Instrumentation

```c
// Add timing points:
uint64_t t_input_read = sceKernelGetProcessTimeWide();
uint64_t t_input_send = sceKernelGetProcessTimeWide();
uint64_t t_video_receive = sceKernelGetProcessTimeWide();
uint64_t t_video_decode_start = sceKernelGetProcessTimeWide();
uint64_t t_video_decode_end = sceKernelGetProcessTimeWide();
uint64_t t_video_display = sceKernelGetProcessTimeWide();
```

---

## Known Issues from Upstream

From vitaki-fork issue #12:
- Latency is especially bad on remote connections
- Local WLAN performance is better but still not optimal
- Fast-paced games are difficult to play

---

## Optimization Priorities

### Phase 1: Measurement (Current Phase)
- [ ] Instrument code with timing points
- [ ] Collect baseline measurements
- [ ] Identify bottlenecks
- [ ] Document findings

### Phase 2: Quick Wins
- [ ] Input polling interval reduction
- [ ] Buffer size tuning
- [ ] Thread priority optimization
- [ ] Remove unnecessary delays

### Phase 3: Deep Optimization
- [ ] Video pipeline optimization
- [ ] Audio/video sync improvements
- [ ] Network protocol efficiency
- [ ] Memory copy elimination

### Phase 4: Validation
- [ ] Test with various game types
- [ ] Measure improvements
- [ ] Test on remote connections
- [ ] User feedback collection

---

## Target Metrics

**Current (Estimated):**
- Total latency: ~150-200ms (remote), ~80-100ms (local)
- Input lag: ~20-30ms
- Video latency: ~60-80ms
- Network RTT: ~50-100ms (remote)

**Target:**
- Total latency: <100ms (remote), <50ms (local)
- Input lag: <10ms
- Video latency: <30ms
- Network RTT: (cannot control, but can optimize handling)

---

## Next Steps

1. Add latency measurement instrumentation
2. Create debug build with timing logs
3. Test on actual hardware
4. Analyze measurements to identify bottlenecks
5. Implement targeted optimizations
6. Iterate and validate

---

## References

- GitHub Issue #5
- Upstream issue: https://github.com/ywnico/vitaki-fork/issues/12
- `docs/INCOMPLETE_FEATURES.md`
- Vita SDK documentation on SceVideodec
- Chiaki protocol documentation
