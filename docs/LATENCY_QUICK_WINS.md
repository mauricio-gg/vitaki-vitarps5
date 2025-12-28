# Latency Quick Wins - Proven Optimizations

**Goal:** Implement known, tested optimizations before doing deep instrumentation

**Status:** Ready to implement
**Priority:** High

---

## Already Implemented ✅

Based on code analysis and README.md, these optimizations are already in place:

1. ✅ **VBlank wait disabled during streaming** (`video.c:872`)
   - Reduces frame presentation latency
   - Prevents waiting for vsync

2. ✅ **FPS locked to 30** (README point #7)
   - Reduces bandwidth and processing overhead
   - Note: Config file FPS setting is ignored

3. ✅ **Video thread priority set to 64** (`video.c:691`)
   - Higher priority = less scheduling latency
   - Priority 64 is high priority on Vita

4. ✅ **Video thread pinned to CPU 0** (`video.c:692`)
   - Prevents thread migration overhead
   - Better cache locality

5. ✅ **Input thread polls at 5ms intervals** (`host.c:177`)
   - Fast input sampling rate
   - 200Hz polling = low input lag

6. ✅ **Audio thread priority pinned to CPU 0** (`vita/src/audio.c:142-150`)
   - Priority 64 keeps the audio mixer from falling behind
   - Affinity mask locks it to CPU 0 alongside the video decoder

7. ✅ **Input thread priority + CPU affinity tuned** (`vita/src/host.c:239-248`)
   - Priority 96 keeps controller sampling ahead of other work
   - Affinity mask pins the thread to CPU 1 to avoid contention with video/audio

8. ✅ **Input polling interval reduced to 2 ms** (`vita/src/host.c:250-454`)
   - Effective 500 Hz sampling for controller/touch/motion data
   - Adaptive sleep keeps the loop aligned without wasting CPU time

9. ✅ **Network receive queue trimmed + overflow logging** (`lib/src/takion.c:44-1150`)
   - Vita build now uses an 8-packet Takion reorder queue to keep latency tight
   - Overflow logs are rate-limited warnings so we can inspect drops without spamming output

---

## Quick Wins to Implement 🚀

The first four quick wins (thread priorities, faster polling, and slimmer network queue) are complete—see the list above for details. Remaining work continues from the original numbering for continuity.

### 5. Disable Frame Pacer (If Not Already)
**Impact:** Low
**Effort:** Low
**Risk:** None

**Current State:**
- Frame pacer appears to be disabled: `if (/*config.enable_frame_pacer*/false ...)`
- Code at `video.c:201` shows it's explicitly set to false

**Status:** ✅ Already disabled (no action needed)

---

### 6. Use Hardware H.264 Decoder Optimally
**Impact:** Medium
**Effort:** Medium
**Risk:** Medium

**Current State:**
- Using SceVideodec (hardware decoder)
- May not be configured optimally for low latency

**Research Needed:**
- Check if decoder is in "low latency mode" vs "high quality mode"
- Verify decoder buffer sizes are minimal
- Check if B-frames are disabled (they increase latency)

**Implementation:**
```c
// In video.c decoder init
// Ensure low-latency H.264 profile:
// - Baseline or Main profile (not High)
// - No B-frames
// - Minimal decoder buffer size
```

**Expected Improvement:** 10-20ms if not already optimized

---

### 7. Reduce Resolution to 360p or 480p
**Impact:** High
**Effort:** None (user configurable)
**Risk:** None

**Current State:**
- Default is 540p (`config.c:78`)
- Lower resolution = less data = less processing = less latency

**Recommendation:**
- Test with 360p for lowest latency
- 480p offers good balance

**Expected Improvement:** 15-30ms at 360p vs 540p

---

## Implementation Priority

### Phase 1: Thread Priorities (✅ Complete)
1. Set input thread priority to 96, pin to CPU 1
2. Set audio thread priority to 64, pin to CPU 0
3. Reduce input polling from 5ms to 2ms

Implemented in `vita/src/host.c` and `vita/src/audio.c` (Nov 2025) to unlock lower input/audio latency. Retain this section for historical reference.

### Phase 2: Decoder Optimization
4. Verify H.264 decoder low-latency mode
5. Check decoder buffer configuration

**Estimated total improvement:** 10-20ms
**Time to implement:** 2-3 hours
**Risk:** Medium (requires testing)

### Phase 3: Network Tuning
6. Optimize network buffer sizes

**Estimated total improvement:** 10-30ms (remote only)
**Time to implement:** 3-4 hours
**Risk:** Medium (requires careful testing)

---

## Testing Strategy

For each optimization:

1. **Before:**
   - Test input lag with 240fps camera or online tool
   - Test game responsiveness subjectively
   - Note any frame drops or stuttering

2. **After:**
   - Repeat same tests
   - Compare measurements
   - Check for regressions (audio glitches, frame drops)

3. **Games to test:**
   - Fast FPS: Call of Duty, Apex Legends
   - Fighting game: Street Fighter, Tekken
   - Racing: Gran Turismo
   - Platformer: Astro's Playroom

---

## Expected Total Improvement

**Optimistic:** 30-60ms reduction
**Realistic:** 20-40ms reduction
**Conservative:** 10-20ms reduction

**From:** ~150-200ms (remote), ~80-100ms (local)
**To:** ~100-140ms (remote), ~50-70ms (local)

This brings us much closer to acceptable latency for most game types.

---

## Insights from chiaki-ng Research

Chiaki-ng (the next-generation fork) has implemented several optimizations we should consider:

### 1. Fragmentation Optimization (v1.9.6)
**Impact:** Medium
**Relevance:** High

Chiaki-ng disabled fragmentation after senkusha (initial handshake) to optimize performance. This suggests network packet fragmentation may be adding latency.

**Investigation needed:**
- Check if our network layer fragments packets unnecessarily
- Verify MTU settings are optimal for streaming

### 2. FEC Error Concealment (v1.5.0)
**Impact:** Low (stability, not latency)
**Relevance:** Medium

Basic Forward Error Correction (FEC) error concealment improves streaming experience by handling packet loss gracefully. This doesn't directly reduce latency but prevents retransmissions.

**Status:** May already be handled by Chiaki base library

### 3. Audio Lag Fix (v1.6.0)
**Impact:** High
**Relevance:** High

**Quote:** "Audio Switch to SDL including fixing audio lag building over session"

This suggests audio buffer issues can accumulate over time. We should:
- Monitor audio buffer levels during extended sessions
- Ensure audio thread doesn't fall behind video
- Verify no audio buffer overflow/underflow

### 4. Frame Pacing Improvements (v1.6.2)
**Impact:** Medium
**Relevance:** Medium

**Quote:** "Corrects framepacing" and "Report corrupted frames earlier resulting in less frames dropped"

Early detection of corrupted frames prevents wasted decoding time. We should:
- Check if we validate frame integrity before decoding
- Ensure frame pacer doesn't add unnecessary delays

### 5. Max Slots Increase for High Bitrate (v1.9.6)
**Impact:** Low
**Relevance:** Low (Vita targets lower bitrates)

**Quote:** "increase max slots as the current slot limit could cause problems at high bitrates"

Less relevant for Vita since we're bandwidth-constrained, but good to verify our buffer slots don't bottleneck at 540p.

### 6. Configuration Best Practices
From chiaki-ng documentation:

- **720p vs 1080p:** Lower resolution reduces latency (we use 540p - good)
- **30fps vs 60fps:** 30fps uses less bandwidth (we use 30fps - good)
- **VSync:** Should be disabled on PS5, only enabled on client (need to verify)
- **5GHz WiFi:** Recommended for lower latency (user configuration)

### Recommendations from Research

**Add to Quick Wins:**
- Verify VSync is disabled on PS5 side
- Check audio buffer behavior during long sessions
- Validate frame corruption early in pipeline

**Add to Phase 2:**
- Review network fragmentation settings
- Investigate frame pacing implementation
- Add audio buffer monitoring

---

## Abandoned Optimizations

### Dual Callback Architecture (Attempted December 2025)

**Concept:** Split Takion's single callback handler into separate AV (audio/video) fast path and control message path to eliminate serialization bottleneck.

**Expected Benefit:** 0.6-2.5ms latency reduction per AV packet by avoiding mutex contention on the AV path.

**Implementation:**
- Added separate `av_cb` callback to ChiakiTakion for AV packets
- AV packets bypass switch statement and state mutex checks
- Control messages (STREAMINFO, DATA) continue through original callback path

**Issues Discovered:**

1. **Race Condition During Initialization**
   - AV packets could arrive BEFORE STREAMINFO completes receiver initialization
   - Video receiver's frame gap detection triggered false "Network Unstable" warnings
   - On WiFi (with packet reordering), this race occurred frequently

2. **Thread Safety Complexity**
   - Initial fix using `volatile bool av_ready` flag was insufficient for ARM
   - Required atomic operations with acquire/release semantics
   - Added `__atomic_load_n()` / `__atomic_store_n()` with proper memory ordering

3. **Performance Regression**
   - Even with correct atomic synchronization, performance was laggy
   - Network stability worse than unified callback approach
   - Gameplay felt choppy compared to baseline

**Conclusion:** Abandoned in favor of keeping the unified callback architecture. The theoretical latency benefit did not materialize in real-world usage, and the added complexity introduced reliability issues.

**Key Learning:** Lock-free optimizations must be validated on target hardware under realistic conditions. Microbenchmark improvements don't always translate to user-perceivable gains, especially when system complexity increases.

**Branch:** `feature/dual-callback` (preserved for reference, not merged)

**Date Abandoned:** December 28, 2025

---

### IP Fragmentation Control on PS Vita (Tested December 2025)

**Concept:** Test if PS Vita supports `IP_DONTFRAG` socket option via empirical constants, despite VitaSDK not exposing the constant in headers.

**Expected Benefit:** 5-10ms latency reduction by eliminating IP packet fragmentation overhead during streaming (based on Chiaki-ng v1.9.6 optimization).

**Hypothesis:** VitaSDK limitation (incomplete headers), NOT a Sony OS limitation. Since `SCE_NET_IP_DF` flag exists in IP header definitions, the underlying BSD stack likely supports fragmentation control.

**Implementation:**
- Defined `IP_DONTFRAG = 28` (FreeBSD standard value)
- Added experimental code to both socket creation paths in `lib/src/takion.c`
- Enabled `ip_dontfrag = true` in `lib/src/streamconnection.c`
- Used empirical testing with standard POSIX `setsockopt()` API

**Test Results (v0.1.417, December 28, 2025):**

Log output:
```
[CHIAKI] PS Vita EXPERIMENTAL: Failed to set IP_DONTFRAG (empirical test, value=28): error -1
```

**Analysis:**
- `setsockopt()` returned `-1` (ENOPROTOOPT - option not supported)
- Error occurred during Senkusha (handshake) phase
- Code path confirmed executed successfully
- PS Vita's BSD stack rejected the socket option

**Conclusion:** **Hypothesis REJECTED.** Sony did NOT implement `IP_DONTFRAG` support in the PS Vita's OS-level BSD network stack. This is an OS limitation, not just incomplete VitaSDK headers.

**Impact:**
- ❌ Cannot disable IP fragmentation on PS Vita
- ❌ No latency reduction possible from this optimization
- ✅ Definitively confirmed via empirical testing
- ✅ Prevents future wasted effort on this approach

**Key Learning:** Not all BSD networking features are implemented on PS Vita. Empirical testing with standard BSD constants can quickly validate OS-level support before investing in complex workarounds.

**Documentation:** See `docs/EXPERIMENTAL_VITA_FRAGMENTATION.md` for complete investigation details, implementation notes, and test methodology.

**Branch:** `experimental/vita-fragmentation-control` (preserved for reference, not merged)

**Date Tested:** December 28, 2025

---

## References

- README.md (existing optimizations)
- `vita/src/video.c` (video thread priority)
- `vita/src/host.c` (input thread)
- Vita SDK documentation
- Chiaki protocol specifications
- chiaki-ng releases: https://streetpea.github.io/chiaki-ng/updates/releases/
- chiaki-ng configuration: https://streetpea.github.io/chiaki-ng/setup/configuration/
