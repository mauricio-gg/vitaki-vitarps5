# Motion Macroblocking Investigation

**Date:** 2026-06-25  
**Status:** Analysis Complete — Two-PR Fix Plan Ready  
**Reporter:** Reddit user (session onset: ~10 seconds, motion trigger)

---

## 1. Symptom

A user reports heavy macroblocking (pixelation/distortion) that appears immediately upon initiating player movement in PS5/PS4 Remote Play sessions, but remains absent during static scenes:

- **Static scenes:** Consistently sharp video, no artifacts
- **Motion starts:** Immediate heavy macroblocking across the entire frame
- **Max bitrate setting:** No effect — problem persists even at highest settings
- **Reproducibility:** Occurs across PS4 and PS5 Remote Play, suggesting a network-layer root cause, not a platform-specific decoder issue
- **Timeline:** Typically appears after ~10 seconds of session start

This behavior is distinct from gradual quality degradation or bitrate-driven compression artifacts. The instant onset upon motion and instant recovery upon stillness suggests a reference-chain collapse in the video decoder, not a bandwidth limitation.

---

## 2. Root Cause Mechanism

Motion-triggered macroblocking stems from a **P-frame reference-chain collapse** in the H.264 video stream. The mechanism unfolds as follows:

### Static Scenes → Sharp Video
- Static scenes generate small P-frames (typically 1–3 UDP packets)
- Burst packet loss (even 10–20% on Vita WiFi) rarely exceeds the PS5-dictated FEC parity ratio for these small frames
- FEC successfully reconstructs any lost units → `chiaki_frame_processor_fec()` in `lib/src/frameprocessor.c:259-267` succeeds
- P-frame reference chain remains unbroken → HW decoder in `vita/src/video.c:500-564` produces sharp output

### Motion Starts → Macroblocking Cascade
1. Large motion generates large P-frames (20–50+ UDP packets, each ~1000 bytes)
2. Burst packet loss (inevitable on congested Vita WiFi) now exceeds PS5-chosen FEC parity
3. `chiaki_frame_processor_fec()` (`lib/src/frameprocessor.c:259-267`) fails with `CHIAKI_ERR_FEC_FAILED` (line 266)
4. Corrupted/incomplete frame is dropped from the decode pipeline
5. Next P-frame arrives, but its reference (the dropped frame) is missing
6. Either:
   - The receiver (`lib/src/videoreceiver.c:462-493`) silently rewrites to an alternate reference, but if that reference is also evicted from the HW decoder, the alternate is stale
   - Or if the frame is marked unrecoverable, an IDR is requested, but cooldown (`IDR_REQUEST_COOLDOWN_MS`, line 18) delays recovery by 100ms
7. HW decoder receives a P-frame against a wrong or missing reference → silent corruption → macroblocking output
8. Macroblocking persists through all dependent P-frames until the next IDR keyframe arrives (1000ms timeout window)

This is the same root mechanism documented in `docs/ai/FPS_HEALTH_INVESTIGATION.md` (FPS drops from reference-chain collapse), but manifests as visual corruption instead of frame loss.

---

## 3. Constant Quick-Reference Table

| Constant | Value | File:Line | Purpose |
|---|---|---|---|
| `IDR_REQUEST_COOLDOWN_MS` | 100 ms | `lib/src/videoreceiver.c:18` | Minimum wait between consecutive IDR requests to avoid flooding |
| `IDR_REQUEST_TIMEOUT_MS` | 1000 ms | `lib/src/videoreceiver.c:19` | Maximum time to tolerate missing reference before forcing IDR |
| `CASCADE_SKIP_THRESHOLD` | 3 | `lib/src/videoreceiver.c:20` | Count of consecutive missing references before triggering IDR |
| Reference frame slots (receiver) | 16 | `lib/src/videoreceiver.c:22-46` | Max reference frames tracked by `add_ref_frame()` / `have_ref_frame()` |
| `REF_FRAMES` (Vita HW decoder) | 8 | `vita/include/video.h:35` | Max reference frames the Vita hardware decoder retains in memory |
| `TAKION_JITTER_MAX_THRESHOLD_US` | 100,000 μs (100 ms) | `lib/src/takion.c:62` | Jitter buffer ceiling on Vita (vs. 20 ms on desktop) |
| `CONGESTION_CONTROL_INTERVAL_MS` | 200 ms | `lib/src/congestioncontrol.c:5` | Loss reporting interval to PS5 (PS5 ignores for bitrate adjustment) |
| FEC parity ratio | PS5-dictated per packet | `lib/src/frameprocessor.c:259-267` | Determines max erasures recoverable via Jerasure/Reed-Solomon |
| Reorder queue size | 256 entries | `lib/src/takion.c` | RTP packet reorder queue capacity |

---

## 4. Candidate Root Causes (Ranked by Priority)

### #1 — Reference Frame Count Mismatch (Strongest Lead)

**The Problem:**
- The Chiaki receiver (`lib/src/videoreceiver.c:22-46`) maintains a 16-slot reference frame tracking system via `add_ref_frame()` and `have_ref_frame()`.
- The Vita hardware H.264 decoder (`vita/include/video.h:35`) retains only **8 reference frames** (`REF_FRAMES = 8`).
- A P-frame can pass receiver validation (reference frame is in the receiver's tracked slots 9–16) but the HW decoder has already evicted that reference from its 8-frame buffer.

**Consequence:**
The receiver has no visibility into which references the HW decoder has actually retained. It validates a P-frame against a reference that no longer exists in the decoder's memory. The HW decoder is forced to decode against a corrupted or bogus reference, producing macroblocking without any error flag or recovery trigger on the receiver side.

**Evidence:**
- `vita/include/video.h:33-35` comment: "Lower values cause decoder freeze/corruption with PS5 streams" — implies the receiver's 16-slot window was never aligned with the HW decoder's true retention.
- Motion → large P-frames → higher reference density → mismatch becomes visible.

**Proposed Fix (PR #1):**
Lower the receiver's reference-frame tracking window from 16 slots to 8, matching the HW decoder. This ensures the receiver's missing-ref detection (line 462–493) activates for the same frames the decoder would corrupt.

**Risk Level:** Low. Structural fix; no behavioral change if the HW decoder is truly limited to 8 frames.

---

### #2 — Passive Missing-Reference Handling

**The Problem:**
- On detection of a missing reference in `lib/src/videoreceiver.c:462-493`, the receiver does **not** immediately request an IDR.
- Instead, it increments `consecutive_missing_ref` counter and optionally rewrites the P-frame to reference an alternate (if available).
- An IDR is only requested after `CASCADE_SKIP_THRESHOLD = 3` consecutive missing references are detected.
- Additionally, the IDR request carries a 100ms cooldown (`IDR_REQUEST_COOLDOWN_MS`, line 18) to avoid flooding the PS5.

**Consequence:**
- During a motion burst with packet loss, the first 2 missing references are silently worked around.
- Only on the 3rd missing ref is an IDR requested.
- The IDR request then waits 100ms before being sent (cooldown), and another ~500–1000ms before the IDR keyframe arrives (network + PS5 processing latency).
- Total recovery time: **600–1100ms** of macroblocking before clean video resumes.

**Evidence:**
- User reports macroblocking appears immediately upon motion and persists visibly.
- This timeline aligns with a 3-frame miss threshold + cooldown + network latency.

**Proposed Fix (PR #2):**
Request an IDR on the **first unrecoverable missing reference** (not just after cascade count of 3). This shortens recovery from ~600–1100ms to ~100–500ms.

**Alternative:** Reduce `CASCADE_SKIP_THRESHOLD` from 3 to 1, with similar effect but potentially more IDR traffic.

**Risk Level:** Medium. IDR requests consume bandwidth and delay other frames; tuning required to avoid over-requesting on transient loss.

---

### #3 — No Client-Side Loss/Bitrate Adaptation

**The Problem:**
- `lib/src/congestioncontrol.c:5` defines `CONGESTION_CONTROL_INTERVAL_MS = 200`, sending loss reports to the PS5 every 200ms.
- The PS5 Remote Play server **ignores client-reported loss for bitrate adjustment** (confirmed by chiaki-ng community; they cap reported loss at 10% to prevent PS5 over-throttling).
- No mid-session bitrate renegotiation exists (`vita/src/host.c:271-273`). Stream bitrate is fixed at session start.
- Recovery from a bitrate mismatch requires a full stream restart (`vita/src/host_recovery.c:30-50`).

**Consequence:**
- At max bitrate on Vita WiFi (which can sustain ~10–15 Mbps realistically, but Remote Play requests 25+ Mbps for high settings), the PS5-chosen FEC parity is insufficient for motion bursts.
- No adaptive response exists: bitrate cannot be renegotiated, and the client cannot signal "lower bitrate to improve FEC margin."

**Evidence:**
- User reports max bitrate has no effect; problem persists at all bitrate levels.
- This suggests the issue is not bitrate-driven compression (which would improve with max setting), but rather network loss exceeding FEC capability.

**Proposed Direction (Future):**
1. Log FEC failure rate and correlation with motion/bitrate to build a quantitative data case.
2. Explore whether a mid-session bitrate reduction could be negotiated (low probability, but worth investigating).
3. Alternatively, implement client-side bitrate capping for Vita when on WiFi (e.g., cap to 8–10 Mbps to ensure FEC margin).

**Risk Level:** High. No existing protocol mechanism; PS5 server design is opaque. Requires significant research and potentially firmware-level PS5 changes.

---

### #4 — Hardware Decoder Never Suppresses Corrupt Output

**The Problem:**
- `vita_h264_decode_frame()` in `vita/src/video.c:500-564` passes every assembled frame to the Vita hardware decoder, including frames with broken references.
- On hardware decode error, the function logs and returns 0 (drops the result).
- On a **corrupt-but-decodable frame** (decode succeeds, but output is garbage due to missing reference), the HW emits macroblocking with no error return.
- No frame-quality gating or corruption detection exists before the frame is passed to display.

**Consequence:**
- The HW decoder can silently produce garbage output; no signal back to the application layer.
- By the time the macroblocking is visible, the corrupted frame is already in the display pipeline.

**Evidence:**
- Macroblocking appears instantly when a large P-frame's reference is missing; the HW decoder emits corrupted pixels without signaling an error.

**Proposed Direction (Future):**
1. Investigate Vita SDK decoder return codes (`sceAvcdecDecodeAvailableSize`, etc.) to see if any error/warning flags indicate corruption.
2. If corruption cannot be detected at decode time, rely on receiver-side recovery (#1, #2) to starve the decode path of bad frames entirely.
3. As a last resort, implement client-side frame-quality heuristics (e.g., histogram analysis or temporal coherence checks) to detect and drop suspicious frames before display.

**Risk Level:** Very High. Decoder internals are proprietary; solutions are speculative. Focus on #1 and #2 first.

---

## 5. Recommended Fix Order

### Phase 1: Structural Alignment (High Confidence, Low Risk)
1. **Align receiver ref-frame window to HW decoder limit** (PR #1)
   - Reduce `have_ref_frame()` tracking from 16 slots to 8
   - Rationale: Eliminate the structural mismatch that blinds the receiver to frames the HW decoder has evicted
   - Implementation: ~10-line change in `lib/src/videoreceiver.c:22-46`
   - Testing: On-device validation with `./tools/build.sh --env testing`; measure time-to-recovery and macroblocking duration before/after

### Phase 2: Aggressive IDR Recovery (Medium Confidence, Medium Risk)
2. **Earlier IDR request on first unrecoverable missing reference** (PR #2)
   - Modify `lib/src/videoreceiver.c:462-493` to request IDR on first missing ref, not after 3 consecutive
   - Rationale: Reduce macroblocking duration from ~600–1100ms to ~100–500ms
   - Implementation: ~5-line conditional change; monitor for IDR request frequency increase
   - Testing: On-device validation; measure latency impact and IDR traffic overhead

### Phase 3: Data Collection & Adaptation (Future Work)
3. **Log FEC failure correlation with motion**
   - Add instrumentation to `lib/src/frameprocessor.c` to track FEC failure rate, motion magnitude, bitrate, packet loss
   - Rationale: Build quantitative case for either bitrate adaptation or receiver-side FEC-parity tuning
   - Testing: Collect statistics across 10+ motion-heavy sessions; analyze correlation

4. **Investigate HW decoder error reporting** (Low Priority)
   - Spike: Can Vita `sceAvcdec` API signal frame corruption?
   - If yes: Implement frame drop logic in `vita/src/video.c:500-564`
   - If no: Confirm Phase 1 + Phase 2 fixes are sufficient

---

## 6. On-Device Validation Protocol

Before/after measurements for each PR:

1. **Build & Deploy**
   ```bash
   ./tools/build.sh --env testing  # Include logging
   ./tools/build.sh deploy <vita_ip>  # Deploy to Vita hardware
   ```

2. **Test Scenario**
   - Launch PS5 Remote Play session (controller or analog stick motion)
   - Connect to a motion-heavy game (e.g., fast action game, camera pan heavy)
   - Measure time from motion onset to clean frame appearance
   - Note any idiosyncratic macroblocking patterns (e.g., specific frame types or motion vectors)

3. **Logs to Capture**
   - `PIPE/FEC` (FEC success/failure) — from `lib/src/frameprocessor.c:230-272`
   - `CHIAKI_LOGD` for IDR requests — from `lib/src/videoreceiver.c:462-493`
   - Jitter buffer statistics — from `lib/src/takion.c:1003-1050`
   - Video decode errors (if any) — from `vita/src/video.c:500-564`

4. **Success Criteria**
   - Macroblocking duration reduced by >50% after Phase 1 + Phase 2
   - No increase in stream reconnections or decoder hangs
   - IDR request frequency remains <5 per minute under normal motion

---

## 7. Related Documentation

- **`docs/ai/FPS_HEALTH_INVESTIGATION.md`** — Same reference-chain collapse mechanism, different symptom (frame drops instead of corruption)
- **`docs/LATENCY_ANALYSIS.md`** — Network and latency constraints on Vita
- **`docs/WIFI_OPTIMIZATION.md`** — User-facing guidance on Vita WiFi limits and best practices
- **`lib/src/videoreceiver.c:18-20`** — IDR cooldown and cascade constants
- **`lib/src/frameprocessor.c:259-267`** — FEC decode implementation
- **`vita/include/video.h:35`** — HW decoder reference frame limit

---

## Next Steps

1. Create PR #1: Align ref-frame window (8 slots, matching HW decoder)
2. On-device test PR #1; measure macroblocking duration reduction
3. Create PR #2: Earlier IDR request on first unrecoverable missing ref
4. On-device test PR #2; measure combined improvement
5. Merge both PRs if improvement > 50% and no regressions
6. File GitHub issues for Phase 3 (data collection + HW decoder investigation) if needed
