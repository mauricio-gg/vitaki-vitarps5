# VitaRPS5 — Remote Play Smoothness Improvement Plan

## Context

VitaRPS5 streams PS5/PS4 Remote Play to a PS Vita. The recently added PSN-over-internet path is "the choppiest right now"; LAN is also worth tuning. The Vita is fixed hardware (single ~333 MHz ARM Cortex-A9 budget for app code at 444 MHz max userland clock, NEON SIMD, 256 MB RAM, 802.11g 2.4 GHz Wi-Fi only with ~15-25 Mbps practical UDP ceiling, dedicated H.264 hardware decoder, 3 user-accessible CPU cores). We want a curated, ranked list of plausible **userland-only** improvements that we can implement incrementally, validated against:

1. The current codebase (`lib/` is already rebased on streetpea's chiaki-ng).
2. PS5 Remote Play protocol reality (what the console actually accepts).
3. VitaSDK platform capabilities (`sceNet`, `sceVideodec`, `sceAvcdec`, `vita2d`, `sceKernel`).

Per repo workflow (CLAUDE.md §10), each item below is sized for the small-batch Investigation→Implementation→Review loop and references file:line so a future agent can pick it up cold.

---

## Verified ground truth (read this before picking a task)

These were unknowns I checked during exploration; the answers shape what's worth doing.

- **Memory said IDR cooldown was 200 ms / 2000 ms — actual is 100 ms / 1000 ms.** `lib/src/videoreceiver.c:18-19` (`IDR_REQUEST_COOLDOWN_MS=100`, `IDR_REQUEST_TIMEOUT_MS=1000`). Memory is stale; update before assuming the old values.
- **PS5 ignores client congestion reports in the upward direction.** Confirmed via chiaki-ng's `packet_loss_max` clamp (commit [`2a0dd5f`](https://github.com/streetpea/chiaki-ng/commit/2a0dd5f)). Reporting >10% loss makes PS5 throttle down permanently. Do NOT over-report; we already cap.
- **No mid-session bitrate renegotiation exists in the protocol** (chiaki-ng issue [#519](https://github.com/streetpea/chiaki-ng/issues/519), [#516](https://github.com/streetpea/chiaki-ng/issues/516)). The `BitratePayload bitrate_change` in `takion.proto` is dev-overlay only; no Remote Play client has gotten the PS5 to honor it.
- **Sony does not run a TURN relay.** The "DERIVED" candidate in `holepunch.c:263-268` is a NAT-rebound UDP probe, not a relay path. `STATIC/LOCAL/STUN/DERIVED` are the only candidate types; no `RELAY` enum.
- **Vita is hard-locked to 540p** by `vita/src/config_values.c:18-29` `normalize_resolution_for_vita()` and `vita/src/host.c:209-214`. PS5 supports 720p/1080p but Vita's panel is 960×544; pushing higher is decoder/bitrate cost without screen benefit.
- **Vita is already at max userland clocks** (`vita/src/main.c:53-56`: ARM 444, GPU 222, BUS 222, GPUXBAR 166). >444 MHz requires a taiHEN kernel plugin; out of scope.
- **Decoder is already decoupled from the network thread for rendering** (UI thread renders via `frame_ready_for_display` flag), but `sceAvcdecDecode` itself still runs synchronously on the Takion receive thread (`vita/src/video.c:490` called from `host_callbacks.c:60`). That's the real lever, not multi-instance decode.
- **Vita has NEON** and the vitasdk toolchain defaults to `-mfpu=neon -mfloat-abi=hard`. NEON code "just works."
- **Vita user cores are USER_0/1/2** (`SCE_KERNEL_CPU_MASK_USER_0..2`). USER_3 is system-reserved and homebrew-illegal without a kernel plugin.
- **`sceNet` exposes `SCE_NET_IP_TOS=3` and `SO_SNDBUF=0x1001`.** `SO_PRIORITY` does not exist; whether the Marvell SD8787 Wi-Fi firmware actually maps `IP_TOS` → 802.11e WMM tagging is undocumented and would need an over-the-air capture to verify.

---

## Bottleneck inventory (with citations)

A. **All hot threads share CPU 0.** `vita/src/video.c:505-507` pins decode to `USER_0`; `vita/src/audio.c:153-155` pins audio to `USER_0`; the Takion recv thread (`lib/src/takion.c:457-459` via `lib/src/thread.c:60-84`) runs at default priority with no affinity. They all preempt each other on a single ~333 MHz core.

B. **HW decode runs on the Takion recv thread.** `sceAvcdecDecode` is synchronous; while it blocks (~12-25 ms for 540p H.264 per community measurements), no UDP packets are drained. Path: `lib/src/takion.c:1036+` → `streamconnection.c:1138` → `videoreceiver.c:494` → `host_callbacks.c:60` → `vita/src/video.c:490`.

C. **`malloc(1500)` per packet on the recv hot path.** `lib/src/takion.c:1143, 1163` allocates and `:1149, 1169` frees, contended against newlib's single heap mutex.

D. **Single decoded-frame slot, silent overwrites.** `vita/src/video.c:74` `frame_ready_for_display` is one bool; `frame_overwrite_count` (`video.c:541-542`) already counts the drops. No double-buffering of the decoded surface.

E. **No `SO_SNDBUF` tuning.** `lib/src/takion.c:289-301, 388-401` only sets `SO_RCVBUF` to `TAKION_A_RWND` (512 KB on Vita).

F. **No QoS / DSCP markings.** `IP_DONTFRAGMENT` is intentionally skipped on Vita (`takion.c:325-329`); no `SCE_NET_IP_TOS`, no `SO_TPPOLICY` set.

G. **FEC has no SIMD.** `lib/src/fec.c` uses pure-C Jerasure / `jerasure_matrix_decode`; Cortex-A9 NEON is unused. K and N are per-frame from the AV header.

H. **PSN path uses LAN bitrate.** `vita/src/host.c:206-219` calls `chiaki_connect_video_profile_preset` once with no LAN-vs-PSN branch. Default 540p preset is 6000 kbps; typical home upstream + Vita Wi-Fi makes that the wrong target.

I. **WebSocket TLS keepalive runs through the whole session.** `lib/src/remote/holepunch.c:73, 968-971` (`WEBSOCKET_PING_INTERVAL_SEC=5`) keeps doing TLS work on the same CPU 0 alongside Takion. Mostly fine (TLS steady-state is cheap) but worth verifying once we move threads.

J. **Senkusha fallback `rtt_us = 1000` (1 ms!)** at `lib/src/session.c:670-672`. Wrong default for any path; survives if Senkusha probe fails.

K. **No PSN-aware diagnostics.** `vita/src/video_overlay.c:249-256` has only a binary "poor net" indicator; we already log selected NAT candidates but never surface them. `streamconnection.c:580-595` decodes `CONNECTIONQUALITY` from PS5 (target_bitrate, upstream_loss, RTT, loss) and **only logs** it.

L. **`force_psn_holepunch` flag is fragile.** Already had two leak fixes (#143, #144). State is global instead of a stream-scoped argument.

M. **AV in-order delivery has no per-stream timeout flush.** Our reorder queue is shared; if one packet is missing, head-of-line blocking hits everything until the adaptive jitter timer fires (`takion.c:1418-1534`, threshold = `clamp(2.5×jitter, 2 ms, 100 ms)`).

N. **No audio jitter buffer.** `vita/src/audio.c` consumes Opus frames as they arrive; a single bad burst causes audible dropouts. Opus decode runs on the Takion thread.

O. **Vsync is OFF during streaming, but `vita2d_wait_rendering_done()` is called before swap** (`video.c:600`). Trade-off is locked in; the wait is a CPU stall worth measuring.

P. **48-socket random-allocation burst on PSN connect** (`holepunch.c:86-90`, Vita-tuned to 48). Close to `sceNet`'s socket ceiling; can collide with other open sockets.

---

## Recommended improvements, ranked by ROI/effort

Each item: **what**, **why**, **where**, **effort** (S = <1 day, M = 2-5 days, L = 1-2 weeks), **risk**.

### Tier 1 — High ROI, low risk (do these first)

**1. Split CPU affinity across the 3 user cores.** [S, low risk]
- *What:* Move audio off `USER_0` to `USER_2`; pin Takion recv thread to `USER_1`; keep HW decode + UI render on `USER_0` (cache-warm).
- *Why:* Bottleneck (A). Today all three hot threads fight for one core; the recv thread loses (it has no priority bump).
- *Where:* `vita/src/audio.c:154-155`, `vita/src/video.c:505-507`, and add `sceKernelChangeThreadCpuAffinityMask` + `sceKernelChangeThreadPriority` to the Takion recv thread (introduce a Vita-side hook in `lib/src/thread.c` or pin from `vita/src/host.c` after session start).
- *Verify:* `dstat`-equivalent isn't available, but a debug overlay counter (frame_overwrite_count, decode_avg_us, queue_high_water from `takion_thread_func`) should improve.

**2. Symmetric `SO_SNDBUF` tuning + read-back log.** [S, low risk]
- *What:* Set `SCE_NET_SO_SNDBUF` to match `TAKION_A_RWND` (512 KB on Vita); log the kernel's actual returned size.
- *Why:* Bottleneck (E). Default send buffer can stall feedback/IDR/ACK bursts; same code path already handles RCVBUF.
- *Where:* `lib/src/takion.c` near the existing `SO_RCVBUF` setsockopt at `:289-301` and `:388-401`.
- *Verify:* `getsockopt` log shows kernel honored the request.

**3. Lower default bitrate on the PSN path.** [S, low risk]
- *What:* When `connect_info.holepunch_session != NULL` (or `psn_remote == true`), clamp `video_profile.bitrate` to ~3500 kbps for 540p.
- *Why:* Bottleneck (H). PS5 doesn't honor mid-session bitrate changes (validated), so the start value is what we live with. 6 Mbps + Takion + RUDP retransmits + Vita 802.11g + typical home upstream = chop.
- *Where:* `vita/src/host.c:206-219` after `chiaki_connect_video_profile_preset`; gate behind a config option `psn_bitrate_override_kbps` so users can opt out.
- *Verify:* `chiaki_stream_stats_bitrate()` in the overlay (item 9) shows the lower target.

**4. Fix Senkusha 1 ms RTT fallback.** [S, low risk]
- *What:* Change the failure default to 30000 µs (30 ms) for PSN, 5000 µs (5 ms) for LAN.
- *Why:* Bottleneck (J). Current 1 ms is wrong everywhere and silently leaves downstream code with bad timing assumptions.
- *Where:* `lib/src/session.c:670-672`.
- *Verify:* Logs show the new default when Senkusha fails.

**5. Throttle PSN holepunch keepalive while streaming.** [S, low risk]
- *What:* After `SESSION_STATE_DATA_ESTABLISHED`, raise `WEBSOCKET_PING_INTERVAL_SEC` from 5 → 30. Restore on session teardown.
- *Why:* Bottleneck (I). Cheap CPU saver; also reduces background packet load on the Vita's single-flow Wi-Fi.
- *Where:* `lib/src/remote/holepunch.c:73, 968-971` (introduce a setter; call it from `vita/src/host.c` post-`SESSION_STATE_DATA_ESTABLISHED`).
- *Verify:* tcpdump on a companion device, or just the holepunch logs.

**6. Reduce PSN NAT-burst socket count.** [S, low risk]
- *What:* `RANDOM_ALLOCATION_SOCKS_NUMBER` 48 → 24 on Vita (or make it config-driven).
- *Why:* Bottleneck (P). Vita has limited sceNet socket headroom; on tough NATs we'd rather try twice with smaller bursts than fail once with a 48-socket spike.
- *Where:* `lib/src/remote/holepunch.c:86-90`.
- *Verify:* Existing `[NAT]` log lines show 24 candidates.

### Tier 2 — High ROI, moderate effort (port from chiaki-ng main)

**7. Port AV reorder queue with timeout-based flush** ([`ea32368`](https://github.com/streetpea/chiaki-ng/commit/ea32368)). [M, moderate risk]
- *What:* Per-stream reorder queues sized 8 (`TAKION_AV_REORDER_QUEUE_SIZE_EXP=3`) with `TAKION_AV_REORDER_TIMEOUT_US=16000` to skip head-of-line stalls.
- *Why:* Bottleneck (M). Single biggest WiFi-jitter win in chiaki-ng; ~1 frame at 60 fps tolerance per stream means audio/control don't wait on a missing video packet.
- *Where:* `lib/src/takion.c` around the existing `chiaki_reorder_queue_*` calls (`:1418-1534, :1611-1648, :1650`); follow upstream diff carefully — our Vita has tuned constants we must preserve (`TAKION_A_RWND=0x80000`, `TAKION_JITTER_MAX_THRESHOLD_US=100000`).
- *Verify:* Run `./tools/build.sh --env testing`, deploy, induce packet loss with a Wi-Fi shaper or by walking out of range.

**8. Port audio jitter buffer + PLC** ([`9d9a9cc`](https://github.com/streetpea/chiaki-ng/commit/9d9a9cc), [`3e481a9`](https://github.com/streetpea/chiaki-ng/commit/3e481a9)). [M, moderate risk]
- *What:* `CHIAKI_AUDIO_JITTER_PREFILL=3`, `CHIAKI_AUDIO_JITTER_BUFFER_SIZE=8`, plus releasing the audio receiver mutex before invoking `frame_cb`.
- *Why:* Bottleneck (N). Today an audio packet burst causes audible drops; chiaki-ng community considered this their #1 audio fix in 2026.
- *Where:* `lib/src/audioreceiver.c` and `lib/src/opusdecoder.c`. Vita-side glue in `vita/src/audio.c`.
- *Verify:* Stream over PSN, induce loss; before the change audio drops, after it conceals up to 1 missing frame.

**9. Surface diagnostics overlay (LAN+PSN).** [M, low risk]
- *What:* Extend `vita/src/video_overlay.c` to show: selected NAT candidate type (host/srflx/prflx/derived), measured `rtt_us`, `jitter_us`, `windowed_bitrate_mbps`, `wifi_rssi`, `takion_drop_packets` delta, **PS5's `target_bitrate`** from `CONNECTIONQUALITY`. Toggle via existing overlay binding.
- *Why:* Bottleneck (K). We already gather the metrics in `host_metrics.c:274-323`; we just don't show them. `CONNECTIONQUALITY.target_bitrate` tells us when PS5 has throttled itself — invaluable when triaging "why is it choppy."
- *Where:* `vita/src/video_overlay.c:249-256` (HUD), `vita/src/host_metrics.c`, `lib/src/streamconnection.c:580-595` (extract `target_bitrate` into a getter), `vita/src/psn_remote.c:203-330` (export selected candidate type).
- *Verify:* On screen during stream.

**10. Slab allocator for 1500-byte packet buffers.** [M, low-moderate risk]
- *What:* Pre-allocate a pool of N×1536-byte slots in `SCE_KERNEL_MEMBLOCK_TYPE_USER_RW`; replace `malloc(1500)/free()` in the takion recv loop.
- *Why:* Bottleneck (C). Newlib's heap mutex is single-lock; the recv thread holds it on every packet. Pool-backed allocation removes mutex contention with audio/video decode.
- *Where:* New helper in `lib/src/takion.c` (or `vita/src/` if we want to keep `lib/` portable). Call sites: `:1143, 1149, 1163, 1169, :2107`.
- *Verify:* CPU sample on takion thread shows fewer `__malloc_lock` cycles.

**11. Move HW decode to its own thread (decouple from Takion recv).** [M, moderate risk]
- *What:* Single-slot SPSC handoff: Takion thread assembles a frame, hands the contiguous buffer to a dedicated "Vita Decoder" thread (priority 64, `USER_0`), which calls `sceAvcdecDecode` and sets `frame_ready_for_display`.
- *Why:* Bottleneck (B). While `sceAvcdecDecode` blocks the Takion thread today, packets queue in the kernel socket buffer and feedback to the PS5 is delayed. Decoupling lets the recv thread keep draining.
- *Where:* `vita/src/video.c:490-549` plus a new thread plumbing alongside `vita/src/audio.c`'s pattern. The existing `frame_ready_for_display` (`video.c:74, 538-546, 583-587`) handoff to the UI thread is unchanged.
- *Verify:* Compare `decode_avg_us` and `frame_overwrite_count` before/after; expect lower `recv_drop_stats` under bursty load.
- *Risk:* Ordering concerns; the assembly is already linear at flush time so a 1-deep queue is sufficient.

**12. Port chiaki-ng's "decoder flush + IDR on data loss"** ([`855de76`](https://github.com/streetpea/chiaki-ng/commit/855de76)). [M, moderate risk]
- *What:* On detected packet loss past threshold, flush the decoder and request a fresh IDR (replaces silent freeze with brief glitch + recovery).
- *Why:* User-facing freeze is worse than a quick glitch. Our existing cascade-skip and IDR cooldown logic is more sophisticated than upstream — port the *flush* part, not the IDR strategy.
- *Where:* `lib/src/videoreceiver.c:381-485` and `vita/src/video.c` (need an `avcdec_flush` equivalent; check VitaSDK for `sceAvcdecDecodeFlush` — undocumented in public headers but referenced in vita-headers stubs).
- *Verify:* Walk into bad Wi-Fi range; measure freeze recovery time.

**13. Test `IP_TOS = 0xB8` (DSCP EF / voice) on the streaming UDP socket.** [S code, M to validate]
- *What:* Set `SCE_NET_IP_TOS = 0xB8` on the Takion socket via `sceNetSetsockopt`.
- *Why:* If the Marvell SD8787 firmware honors IP_TOS for 802.11e WMM access category, we get free latency on congested APs (voice class wins air-time arbitration). Verdict from research: API supported; effect uncertain — needs an over-the-air capture to confirm WMM AC tagging.
- *Where:* `lib/src/takion.c` socket setup (`:289-401` area).
- *Verify:* Companion Wi-Fi card in monitor mode, capture a few seconds of streaming traffic, inspect the 802.11 QoS Control field. If AC_VO (priority 6) shows up, ship it. If not, document the negative result and revert.

### Tier 3 — Speculative wins, defer until Tier 1+2 land

**14. NEON-accelerated FEC.** [L, moderate risk]
- *What:* Port the GF(2^8) multiply-add inner loop in `gf-complete` (Jerasure's backend) to ARMv7 NEON intrinsics using `vmull.p8`.
- *Why:* Bottleneck (G). FEC dominates CPU when packet loss is bursty. Realistic 3-5× speedup on Cortex-A9 given the non-dual-issue NEON unit.
- *Where:* New file alongside `lib/src/fec.c`; gate behind `CHIAKI_FEC_NEON` config flag so we can fall back. Reference: AArch64 NEON gf-complete patches (need 32-bit ARMv7 backport).
- *Verify:* Microbenchmark `chiaki_fec_decode` on a 1 MB frame with K=N/2; deploy and measure PSN-loss-recovery latency.
- *Effort warning:* 1-2 weeks of focused NEON work. Skip unless Tier 1+2 don't move the needle.

**15. Two-slot decoded-frame ring (eliminate silent overwrites).** [M, low risk]
- *What:* Two `vita2d_texture` surfaces; decoder writes to slot N+1 while UI samples slot N. `frame_ready_for_display` becomes a 2-state index.
- *Why:* Bottleneck (D). Today, if UI is mid-`vita2d_wait_rendering_done()` when decode finishes, the next decode overwrites the same texture — `frame_overwrite_count` proves it happens.
- *Where:* `vita/src/video.c:63, 74, 286-295, 538-617`.
- *Verify:* `frame_overwrite_count` should drop to ~0 in normal operation.
- *Note:* Pairs naturally with item 11; do them in the same PR.

**16. Configurable STUN port-guessing (chiaki-ng [`6e778f7`](https://github.com/streetpea/chiaki-ng/commit/6e778f7)).** [S, low risk]
- *What:* Expose `force_stun_port_guessing` as a per-host config flag (default off, like upstream).
- *Why:* Some symmetric NATs are only crackable with port guessing; right now we always do random allocation (48 sockets). User-tunable lets advanced users force or disable.
- *Where:* `lib/src/remote/holepunch.c` (port the constant), `vita/src/psn_remote.c`, settings UI.
- *Verify:* Per-host setting persists across sessions.

**17. Replace `force_psn_holepunch` global with stream-scoped argument.** [S, low risk]
- *What:* Pass a `bool force_internet` into `host_stream` (or equivalent entry point) instead of mutating a global flag.
- *Why:* Bottleneck (L). Two leak fixes already (#143, #144) prove the global is fragile.
- *Where:* `vita/src/host.c:144-146` and call sites; UI screens that toggle the flag.
- *Verify:* Code review; ensure no leak path can leave the flag set.

### Explicitly NOT planned (validated as infeasible)

These are off the table — documenting so we don't relitigate:

- **Adaptive client→PS5 bitrate negotiation** — protocol does not support; chiaki-ng tried and concluded it's one-way (PS5 down-only with no recovery).
- **TURN relay fallback for double-symmetric NAT** — Sony does not run a relay for Remote Play; CGNAT cannot be bypassed.
- **Mid-session PMTU re-probe** — chiaki-ng doesn't either; Senkusha runs once, MTU is frozen. Low ROI on stable Wi-Fi.
- **Async H.264 decoder API** — no public `sceAvcdecDecode` async variant exists; "decouple decode thread" (item 11) is the actual lever.
- **Multiple HW decoder instances** — single VIP DSP, calls would serialize anyway.
- **Tearing-free + stutter-free vsync** — hardware constraint; libvita2d is already triple-buffered, the trade-off between vblank-wait stutter and mailbox tearing is fundamental.
- **CPU > 444 MHz / 4th core (`USER_3`/`SYSTEM`)** — both require a taiHEN kernel plugin; out of scope for homebrew userland.
- **Wi-Fi 5 GHz / 802.11n 2×2** — Vita radio is 1×1 b/g/n at 2.4 GHz only; no software fix.
- **`SO_PRIORITY`** — not exposed by `sceNet`. (`IP_TOS` is the closest substitute, gated on item 13's findings.)
- **Pushing past 540p** — Vita panel is 960×544; 720p decode wastes CPU on a downscale with no perceived gain.

---

## Critical files (for any item above)

| Area | Path |
|---|---|
| Takion UDP / jitter buffer / reorder | `lib/src/takion.c` |
| Stream connection glue | `lib/src/streamconnection.c` |
| Video receiver (FEC, IDR, refs) | `lib/src/videoreceiver.c` |
| FEC decode | `lib/src/fec.c`, `lib/src/frameprocessor.c` |
| Senkusha (MTU/RTT probe) | `lib/src/senkusha.c`, `lib/src/session.c` |
| Audio receiver / Opus | `lib/src/audioreceiver.c`, `lib/src/opusdecoder.c` |
| PSN holepunch / RUDP | `lib/src/remote/holepunch.c`, `lib/src/remote/rudp.c` |
| Congestion control | `lib/src/congestioncontrol.c` |
| Vita HW decode + render | `vita/src/video.c`, `vita/src/host_callbacks.c` |
| Vita audio out | `vita/src/audio.c` |
| Vita main loop / UI | `vita/src/ui.c`, `vita/src/main.c` |
| Vita session entry | `vita/src/host.c` |
| PSN client glue | `vita/src/psn_remote.c`, `vita/src/psn_auth.c` |
| Diagnostics overlay | `vita/src/video_overlay.c`, `vita/src/host_metrics.c` |

## Verification (per-item plus end-to-end)

For every change:

1. **Build:** `./tools/build.sh --env testing` (per repo rule for hardware streaming validation; `--env testing` enables logs).
2. **Format:** `./tools/build.sh format` before any commit (per `feedback_build_format.md`).
3. **Deploy:** `./tools/build.sh deploy <vita_ip>`.
4. **LAN smoke test:** Stream a known game for 10 minutes; check `frame_overwrite_count`, `decode_avg_us`, `decode_max_us`, `recv_drop_stats`, `takion_drop_packets`, RTT, jitter (overlay added by item 9 makes this trivial).
5. **PSN smoke test:** Same, over PSN remote. Note `target_bitrate` from `CONNECTIONQUALITY` to detect server-side throttling.
6. **Stress test:** Walk to edge of Wi-Fi range; confirm graceful degradation (fewer freezes, faster recovery).
7. **Regression test:** Verify LAN didn't get worse when changes were intended for PSN, and vice-versa.

## Suggested execution order

1. Items **1, 2, 3, 4, 5, 6** (Tier 1, all small) — single batch PR each, ~1 week total.
2. Item **9** (diagnostic overlay) — needed to measure all subsequent work.
3. Items **7, 8, 10, 11+15, 12** (Tier 2 chiaki-ng ports + decode decoupling) — separate PRs, ~3-4 weeks.
4. Item **13** (IP_TOS test) — schedule once you have a companion Wi-Fi capture rig.
5. Items **16, 17** (config polish) — anytime.
6. Item **14** (NEON FEC) — only if Tier 1+2 leave perceived chop on PSN.

## Out-of-band research artifacts cited

- chiaki-ng commits: [`ea32368`](https://github.com/streetpea/chiaki-ng/commit/ea32368), [`9d9a9cc`](https://github.com/streetpea/chiaki-ng/commit/9d9a9cc), [`3e481a9`](https://github.com/streetpea/chiaki-ng/commit/3e481a9), [`855de76`](https://github.com/streetpea/chiaki-ng/commit/855de76), [`2a0dd5f`](https://github.com/streetpea/chiaki-ng/commit/2a0dd5f), [`79a8e61`](https://github.com/streetpea/chiaki-ng/commit/79a8e61), [`6e778f7`](https://github.com/streetpea/chiaki-ng/commit/6e778f7), [`c567d27`](https://github.com/streetpea/chiaki-ng/commit/c567d27).
- chiaki-ng issues: [#516](https://github.com/streetpea/chiaki-ng/issues/516), [#519](https://github.com/streetpea/chiaki-ng/issues/519), [#88](https://github.com/streetpea/chiaki-ng/issues/88).
- VitaSDK headers: `vita-headers/include/psp2common/net.h`, `psp2/videodec.h`, `psp2/power.h`, `psp2/kernel/cpu.h`.
- Hardware constraints: PlayStation Vita Wikipedia (Cortex-A9 + NEONv1, 1×1 802.11b/g/n 2.4 GHz only).
