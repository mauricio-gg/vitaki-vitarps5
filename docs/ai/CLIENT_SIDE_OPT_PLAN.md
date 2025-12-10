# Client-Side Optimization Plan

**Owner:** Implementation agent
**Created:** 2025-02-16
**Scope:** Vita client (`vita/src/*`) – packet-loss handling, pacing, telemetry, LaunchSpec hints, controller resilience.

---

## 1. Loss Event Tuning
- **Status:** Implementation in progress on `feature/loss-event-tuning`.
- **Hypothesis:** The original hard-coded constants in `vita/src/host.c:35-38` triggered soft restarts too aggressively in Ultra Low mode. Adaptive windows tied to latency presets and measured bitrate (`loss_profile_for_mode()` + `adjust_loss_profile_with_metrics()` in `vita/src/host.c:31-210`) should reduce false positives.
- **Action Items:**
  1. Parameterize thresholds per `latency_mode`.
  2. Use live metrics to scale retry thresholds and delays.
  3. Track total frames lost per window (not just event counts) so repeated single-frame drops can still trip the gate.
  4. Log “loss gate” triggers with ratios (events + frames vs. window) for later analysis.
- **Validation Plan:** Force packet loss via Wi-Fi throttling, run `./tools/build.sh debug`, capture `vitarps5.log` sections showing fewer overlay triggers while sustained loss still trips fallbacks.
- **Notes:** TBD (fill in observations + date after each tuning pass).

## 2. Adaptive Pacing & Drop Handling
- **Status:** Force-30 toggle exists; automatic pacing pending.
- **Hypothesis:** Using `context.stream.measured_incoming_fps` + bitrate deltas to auto-engage frame dropping will smooth spikes when PS5 overruns Vita throughput.
- **Action Items:**
  1. Extend `should_drop_frame_for_pacing()` (`vita/src/video.c:143-168`) to look at live metrics and auto-enable drop ratios.
  2. Consider decoder slack when `takion_drop_events` spike.
  3. Gate via new config (`auto_pacing`) for A/B testing.
- **Validation Plan:** Enable FPS overlay, confirm adaptive drops only occur when source fps/bitrate exceed target. Test PS5 720p + PS4 540p streams.
- **Notes:** Record final ratios, toggles, and perf impact.

## 3. Telemetry Surfacing & Controls
- **Status:** Metrics exist behind `show_latency`; UI exposure pending.
- **Hypothesis:** Presenting bitrate/RTT/loss data in the streaming HUD and offering quick preset switches will help users self-mitigate congestion.
- **Action Items:**
  1. Move metrics from the hidden profile card (`vita/src/ui.c:2117-2159`) into the in-stream HUD.
  2. Add prompts (e.g., “Switch to Low preset?”) wired to `request_stream_restart()`.
  3. Link to per-run logs or show a short history overlay.
- **Validation Plan:** Smoke-test on hardware, verify controller + touch interactions, ensure HUD cost is minimal.
- **Notes:** Document user feedback + UI screenshots once implemented.

## 4. LaunchSpec Hint Experiments
- **Status:** Not started.
- **Hypothesis:** Tweaking `network.bwLoss`, `bwKbpsSent`, or `appSpecification.minBandwidth` (`lib/src/launchspec.c:18-74`) might influence PS5 behavior even though `RP-StartBitrate` is ignored.
- **Action Items:**
  1. Expose config flags to adjust these fields per latency preset.
  2. Run structured tests (one variable per build) and log measured bitrate (`chiaki_stream_stats_bitrate`).
  3. Summarize whether PS5 honored any change.
- **Validation Plan:** Compare Mbps/RTT across builds, note if PS5 renegotiates differently.
- **Notes:** Fill with experiment matrix + outcomes; mark any knobs proven ineffective.

## 5. Controller Resilience During Restarts
- **Status:** Soft gate exists, but telemetry (see `TODO.md:33-40`) still reports multi-second stalls.
- **Hypothesis:** Re-arming `inputs_ready` earlier and sending idle packets through `request_stream_restart()` will keep inputs responsive during loss recovery.
- **Action Items:**
  1. Flip `inputs_ready` to true on `CHIAKI_EVENT_CONNECTED` even before video resumes.
  2. Cache controller state through restarts; optionally send periodic keep-alive packets.
  3. Expand logging around `controller_gate_open` latency (`vita/src/host.c:679-823`).
- **Validation Plan:** Trigger packet-loss restarts, measure gate reopen times (<500 ms target), confirm no controller dropouts.
- **Notes:** Capture before/after logs here and move the TODO entry to Done once satisfied.

---

## Logging Template
Use the table below per experiment or tuning pass:

| Date | Area | Build | Scenario | Observation | Next Step |
|------|------|-------|----------|-------------|-----------|
| 2025-02-16 | Loss Tuning | Debug | PS5 720p (loss injected) | `65704648970_vitarps5-testing.log` shows repeated 1-frame drops (`Frame loss` at lines 6179/6221) with no soft restart or hint spam; measured bitrate hovered 0.84–0.95 Mbps vs 1.2 Mbps target | Re-run with `show_latency=true` to capture `Loss gate` logs, fine-tune Balanced/High ratios |
| YYYY-MM-DD | Loss Tuning | Debug | PS5 @720p, Wi-Fi throttled | e.g., “Loss gate triggered after 3 bursts; reduced overlays by 50%” | Adjust MIN_FRAMES for Balanced |

Fill in rows as changes land so future agents can see what was tested and avoid duplicating work.
