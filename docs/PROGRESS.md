# Project Progress

Last Updated: 2026-08-10

## Current Focus

**Latency Reduction + Stability Hardening** - Addressing network-driven performance degradation (GH #206: Wi-Fi burstiness intrinsic; GH #208: ENOBUFS mid-stream freeze). Next phase: loss-report capping + burst absorption on client side.

## Status Log

| Date | Issue | PR | Status | Commit |
|------|-------|----|---------|-----------| 
| 2026-08-10 | GH #208 | #209 | Merged | 205eed56 |
| 2026-08-10 | GH #206 | Proximity A/B | Analyzed | (log 13382891119) |
| 2026-06-27+ | GH #188 | #199 | Merged | 135e8fb6 |

---

## Project: VitaRPS5

### Epic: Latency Reduction Initiative - In Progress
**Goal:** Reduce end-to-end streaming latency to <100ms on remote, <50ms on local; eliminate mid-stream freezes; optimize Vita Wi-Fi packet handling.

#### Network Recovery & Stability
- [x] GH #188: Decouple video decode from recv thread (merged 135e8fb6)
- [x] GH #208: Fix ENOBUFS session-freeze (merged 205eed56)
- [ ] GH #206: Address Wi-Fi burstiness via loss-report capping (pending – RSSI hypothesis refuted)
- [ ] Client-side burst absorption (network buffer tuning, jitter smoothing)
- [ ] Hardware validation of GH #208 fix

#### Capacity & Measurement
- [ ] IDR cooldown optimization (target: reduce cascade skip threshold)
- [ ] Jitter buffer tuning on Vita (current: 2–40ms range)
- [ ] Measure actual bitrate via `chiaki_stream_stats_bitrate()`
- [ ] Build instrumentation for latency traces (timing points in video pipeline)

### Epic: Modern UI Migration - Planned
**Goal:** Ship the modern VitaRPS5 interface (wave navigation, console cards, tabbed settings) documented in `docs/ai/UI_FINAL_SPECIFICATION.md`.

#### UI Components
- [ ] Wave navigation system
- [ ] Console card layout with status indicators
- [ ] Tabbed settings interface (video quality, network, input)
- [ ] Touch + controller input parity

---

## Known Constraints

- **Vita Hardware:** 960×544 display, limited RAM, Cortex-A9 CPU, Wi-Fi inherently bursty (confirmed 2026-08-10 proximity test)
- **PS5 Behavior:** Ignores congestion control feedback; throttles independently; no adaptive bitrate
- **IDR Cooldown:** 100ms, timeout 1000ms (verified 2026-05-01; cascade_skip_threshold=3)
- **Thread Model:** Recv thread (network), VitaDecode thread (sceAvcdecDecode), UI thread (rendering, input polling)

---

## Recent Changes & Rationale

- **GH #208 (205eed56):** ENOBUFS now retries instead of killing recv thread. Prevents transient buffer-full errors from causing session-freezes. Mid-stream DISCONNECT detection added.
- **GH #206 (Proximity A/B):** Confirmed Vita Wi-Fi burstiness is NOT due to signal strength. Shifts focus from antenna/RSSI to packet-absorbing buffering and loss-report strategy.

---

## Next Priorities

1. **Validate GH #208 on Hardware** — Ensure ENOBUFS retry works end-to-end; transport death detected cleanly; no crashes or residual EBADF.
2. **Loss-Report Capping** — Cap reported loss at ~10% (chiaki-ng approach) to prevent PS5 over-throttling.
3. **Burst Absorption** — Network buffer + jitter smoothing to handle Vita Wi-Fi packet clustering.
4. **UI Readiness Review** — Confirm modern UI spec (`docs/ai/UI_FINAL_SPECIFICATION.md`) covers all inputs and state.

---

## References

- `docs/LATENCY_ANALYSIS.md` — Full measurement strategy and GH #208 subsection
- `docs/LATENCY_QUICK_WINS.md` — Ready-to-ship optimizations
- `docs/WIFI_OPTIMIZATION.md` — End-user guidance
- `docs/ai/ROADMAP.md` — Macro migration phases
- `docs/INCOMPLETE_FEATURES.md` — Original backlog
- GitHub Issues: #5 (latency), #206 (proximity test), #208 (ENOBUFS), #188 (decode decouple)
- Memory: `session_handoff_gh188.md`, `psn_duid_websocket_403.md`, `psn_rudp_session_request.md`
