# Completed Work

## 2026-08-10

### GH #188 - Video Decode Thread Decoupling
- [x] Decouple `sceAvcdecDecode` from Takion recv thread - Moved H.264 decode to dedicated VitaDecode thread (USER_1); recv thread now copies compressed bitstream into SPSC queue and returns immediately. Reduces blocking in network receive path. Merged as PR #199.
- [x] Verify frame handoff safety - `frame_ready_for_display` volatile bool bridges decode thread → UI thread; tested with stress patterns.

### GH #208 - Session-Freeze ENOBUFS Fix
- [x] Fix transient ENOBUFS handling in takion_recv - `recv()` on Takion UDP socket no longer kills recv thread on transient buffer errors. Now retries with 5ms sleep, escalates to hard failure only after 400 consecutive errors (~2s). Prevents mid-stream freezes.
- [x] Propagate mid-stream DISCONNECT events - `stream_connection_takion_cb()` now handles DISCONNECT outside handshake, allowing transport-death detection while streaming. Session no longer freezes waiting on dead socket.
- [x] Eliminate EBADF flood - Socket invalidated before close; sender threads stopped cleanly; rate-limited send logging kills repeated error spam on closed fd.
- [x] Latency doc subsection added - GH #208 root-cause analysis and fix documented in `docs/LATENCY_ANALYSIS.md:86-100`.
- [x] Code review (code-guardian, 2 rounds) - 1 blocker + 6 required findings fixed, then APPROVED. Build v0.1.842.
- [x] Merged to main as commit 205eed56 (2026-08-10).

## Previous Batches

### 2026-05-01 (GH #188 predecessor work)
- [x] Profile network thread bottlenecks - Identified recv thread as critical path; video decode blocking network ingress.
- [x] Validate SPSC queue design - Decoupling safe via frame_ready_for_display handshake and Cortex-A9 cache coherency.
