## TODO · Active Work Queue

This document tracks the short, actionable tasks currently in flight. Update it whenever the plan shifts so every agent knows what to do next.

### 🔄 Workflow Snapshot
1. **Investigation Agent** – research, spike, or scoping work; records findings below.
2. **Implementation Agent** – picks up the next ready task, writes code, notes validation.
3. **Review Agent** – senior-level review; either approves or kicks it back with required fixes.

Only move a task to “Done” after the reviewer signs off.

---

### 🟡 In Progress
1. **Clarify PS5 bitrate negotiation**  
   - *Owner:* Investigation agent  
   - *Goal:* Confirm whether the PS5 honors `RP-StartBitrate` and LaunchSpec fields by instrumenting the ctrl request (`lib/src/ctrl.c:1136-1245`) and comparing against the LaunchSpec payload (`lib/src/streamconnection.c:843-887`).  
   - *Next Step:* Capture control-plane packets (logs) before/after instrumentation.

---

### 📝 Ready for Implementation
1. **Update `RP-StartBitrate` handling**  
   - Send the actual requested bitrate instead of zeros in `lib/src/ctrl.c`.  
   - Guard with config/flag so we can A/B test.

2. **Expose low-bandwidth profile in config/UI**  
   - Allow selecting 360p / <2 Mbps preset through the modern settings once backend supports it.

3. **Graceful mid-stream packet-loss fallback**  
   - Automatically lower bitrate without tearing down the whole UI when Ultra Low still drops frames.  
   - Keep discovery paused, show a “reconnecting” overlay, and restart video/audio while preserving ctrl state.

4. **Preserve controller responsiveness through fallbacks**  
   - Instrument `input_thread_func()` to log when pad packets stall, then cache/synchronize pad state so restarts don’t add extra lag.  
   - Investigate keeping ctrl alive while video/audio reconnect to avoid input gaps.
   - Latest telemetry (`vitarps5.log:11302-11324`) shows the controller gate stays closed for ~6.3 s during packet-loss retries despite gameplay resuming, so we need to re-arm `inputs_ready` (or keep ctrl alive) much earlier in the reconnect flow.

5. **Upstream protocol support for dynamic bitrate**  
   - Spike Chiaki/PS5 changes required to renegotiate bitrate mid-session (ctrl RPC or LaunchSpec update).  
   - Document needed evidence so we can eventually reconfigure without a teardown.

### 📥 In Review
1. **Instrument PS5 bitrate/latency metrics**  
   - *Owner:* Implementation agent (latency instrumentation)  
   - *Summary:* Added runtime bitrate + RTT sampling via `vita/src/host.c` (using `chiaki_stream_stats_bitrate`) with gated logging and profile card display in `vita/src/ui.c`. Metrics reset on stream stop and update whenever frames arrive.  
   - *Needs:* Reviewer to verify code quality, ensure no race conditions with Chiaki structs, and confirm UI integration looks correct on hardware.
2. **Add latency mode presets (1.2–3.8 Mbps)**  
   - *Owner:* Implementation agent  
   - *Summary:* Introduced `latency_mode` config/UI dropdown plus bitrate overrides in `vita/src/host.c` so users can pick Ultra Low → Max bandwidth targets. Added presets to config serialization and documented options in README.  
   - *Needs:* Reviewer to validate Chiaki profile overrides, ensure config migration works, and smoke-test the new dropdown on hardware.

3. **FPS instrumentation + client-side clamp**  
   - *Owner:* Implementation agent  
   - *Summary:* Added per-second frame cadence logging in `vita/src/video.c` + stored the negotiated fps in `vita/src/host.c`. Implemented a “Force 30 FPS Output” toggle (UI + config) that drops frames deterministically when the PS5 still streams 60 fps, while keeping 30 fps streams untouched. README now documents the new option.  
   - *Needs:* Reviewer to verify the pacing logic on hardware (ensure toggling works while streaming, overlay logs make sense, and the Vita maintains 30 unique frames when the clamp is enabled).

---

### ✅ Done
- **Stream retry cooldown + input readiness gate**  
  - Added a 3 s cooldown after `CHIAKI_EVENT_QUIT` and require it to expire before `host_stream()` can run again (`vita/src/host.c:110-134`, `vita/include/context.h:46-60`, `vita/src/host.c:470-520`).  
  - Introduced `inputs_ready` so the input thread starts sending pad data on `CHIAKI_EVENT_CONNECTED`, not the first video frame (`vita/src/host.c:297-474`).

- **Discovery grace window**  
  - Track `last_discovery_seen_us` for each host and keep entries alive for 3 s before pruning so consoles that momentarily disappear don’t thrash (`vita/include/host.h:17-26`, `vita/src/discovery.c:16-191`).

- **Takion queue monitoring**  
  - Restored the reorder queue to 64 entries and log high-water usage to validate headroom while we consider adaptive sizing later (`lib/src/takion.c:46-120`, `lib/src/takion.c:919-1024`).
