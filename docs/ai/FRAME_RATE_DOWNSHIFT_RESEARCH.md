# Frame-Rate Downshift Layer Research
**Date:** 2025-02-14  
**Requested by:** Implementation agent (per TODO queue)  
**Scope:** Vita streaming client (Chiaki-based video path)  
**PS5 constraint:** Console always pushes 60 fps streams, regardless of requested profile; any downshift must happen on-device.

---

## Current Behavior Summary
- The fork already **defaults** to 30 fps because `config_parse()` hard-codes `cfg->fps = CHIAKI_VIDEO_FPS_PRESET_30` and ignores higher values from `chiaki.toml` (`vita/src/config.c:100-159`). README point #7 also states that forcing 30 fps reduced lag and that config changes are ignored (`README.md:61-72`).
- Despite the default, the modern settings UI still presents a 30/60 toggle and writes the chosen preset back into `context.config.fps` at runtime (`vita/src/ui.c:1330-1525`). This means players can still start a stream at 60 fps for the current session even though the value is not persisted across launches.
- When a session is created, we pass `context.config.resolution` and `context.config.fps` directly into `chiaki_connect_video_profile_preset()` and embed the result inside `ChiakiConnectInfo` (`vita/src/host.c:529-587`). The Chiaki helper maps the preset to an explicit `max_fps` value that becomes part of the launch spec (`lib/src/session.c:93-137`), so—when the console honors it—we already negotiate the lower frame rate instead of downgrading locally.
- Runtime telemetry derives bitrate from `session.connect_info.video_profile.max_fps` each time we update the latency overlay (`vita/src/host.c:154-188`). That is the authoritative view of what we *requested*, not necessarily what we *receive*.
- The Vita-side decoder path has remnants of a frame-pacer thread that was meant to drop frames whenever `frame_count` exceeded `context.stream.fps` (`vita/src/video.c:178-215`), but the thread is never spawned (`vita/src/video.c:526-538`), `context.stream.fps` is never populated, and the drop counter (`need_drop`) therefore never activates (`vita/src/video.c:820-839`).
- Upstream Chiaki pushes raw frame payloads through `chiaki_video_receiver_flush_frame()` and invokes our callback for *every* completed frame, regardless of how fast they arrive (`lib/src/videoreceiver.c:83-199`). There is no intermediate layer that can discard decoded frames before they hit Vita code today.

---

## Why Consider a 60 → 30 fps Layer?
- Latency docs already capture that holding 30 fps cuts both bandwidth and processing cost (see Quick Win #2 in `docs/LATENCY_QUICK_WINS.md:12-21`). The Vita simply has more thermal and bandwidth headroom when it does not need to present 60 unique frames per second.
- Certain PS5 games insist on 60 fps streams whenever the negotiated resolution is ≥720p. Even if we request 30 fps, the console may upgrade us once link quality improves (`chiaki_connect_info.video_profile_auto_downgrade = true` in `vita/src/host.c:565-569`). In those cases we still end up decoding and rendering 60 fps, which can reintroduce stutter and packet loss on weak networks. A downshift layer would let us clamp the presentation rate even when the console refuses to serve a lower profile.
- Dropping redundant frames locally can stabilize the GPU workload and minimize display tear because Vita’s screen is locked to 59.94 Hz. Presenting every other frame with predictable pacing is better than letting the decoder push frames as fast as they arrive, especially when network jitter already breaks cadence.
- A software dropper also prepares us for future UI policies (for example, maintaining a 60 fps UI while the stream itself stays at 30 fps) because we can isolate the throttling logic to the video overlay rather than hard-coding it in config parsing.

---

## Implementation Strategies Considered

### Reference Check: chiaki-ng
- **Session negotiation (desktop + Switch/Deck):** both pipelines rely on `chiaki_connect_video_profile_preset()` and set `video_profile_auto_downgrade = true`, but there is no override beyond whatever `video_fps` value the UI saved (`chiaki-ng/gui/src/streamsession.cpp:70-146`, `chiaki-ng/switch/src/host.cpp:146-208`).
- **Configuration layer:** Desktop defaults every FPS preset to 60 fps for PS4/PS5 local + remote profiles and only stores the user’s 30/60 choice in its QSettings map (`chiaki-ng/gui/src/settings.cpp:350-454`). The Switch/Deck INI parser mirrors this behavior with a global `ChiakiVideoFPSPreset` default (`chiaki-ng/switch/include/settings.h:34`, `chiaki-ng/switch/src/settings.cpp:1-229`). Neither path validates that the console honored the request.
- **Decode/render path:** Desktop pulls frames out of FFmpeg on every `FfmpegFrameAvailable` signal and immediately enqueues them for QML presentation (`chiaki-ng/gui/src/qmlbackend.cpp:738-809`). `QmlMainWindow::presentFrame()` only drops a frame when the UI thread falls behind, not to enforce a target fps (`chiaki-ng/gui/src/qmlmainwindow.cpp:302-323`). The Switch/Deck IO callback behaves the same (`chiaki-ng/switch/src/io.cpp:200-260`). There is no “skip every other frame” logic anywhere in the chiaki-ng tree.
- **Doc guidance:** Their setup guide still recommends “Switch framerate to 30 fps” to fix network issues (`chiaki-ng/docs/setup/configuration.md:465-468`), reinforcing that chiaki-ng expects the console to comply. Since PS5 always delivers 60 fps for us, Vita must supply its own downshift layer.

### 1. Negotiation Clamp (Configuration Layer)
**Idea:** Preserve the 60 fps UI option but add an explicit “force 30 fps” knob that rewrites the outgoing launch spec even if the user selected 60.

- Use a new config flag (e.g., `fps_force_30`) and gate the call to `chiaki_connect_video_profile_preset()` so that we pass `CHIAKI_VIDEO_FPS_PRESET_30` whenever the flag is active, regardless of UI state (`vita/src/host.c:560-568`).
- Mirror the flag in the settings dropdown so players understand that picking 60 may be overridden. UI already writes config changes through `config_serialize()` (`vita/src/ui.c:1484-1525`), so we only need to add one extra toggle or tooltip.
- Upside: Keeps PS4 behavior consistent (it honors 30 fps).  
  Downside: **Does not help PS5**, which always streams at 60 fps; this option matters only for future-proofing or PS4 clients.

### 2. Vita-Side Pacing Layer (Post-Decode Dropper)
**Idea:** Reanimate the dormant pacer infrastructure so we only *present* 30 frames even when 60 are decoded.

- Store the negotiated `max_fps` inside `context.stream.fps` as soon as we build the profile, then set a new `context.stream.target_fps` (30 by default). This fixes the “bug?” comment in `vita/src/video.c:186` and gives the pacer actual data.
- Re-enable `vita_pacer_thread_main()` creation (`vita/src/video.c:528-536`) and replace the `/*config.enable_frame_pacer*/false` guard with `context.stream.target_fps < context.stream.fps`.  
- Modify the pacing logic to compute how many frames to skip per second (e.g., keep a modulo counter so we drop every other frame when 60 → 30, instead of batching drops when `need_drop` grows). Because we ingest frames on the video thread, simply skipping the drawing block at `vita/src/video.c:820-839` is enough to halve GPU work while keeping decode buffers hot.
- Upside: Simple Vita-only change; no Chiaki modifications. Maintains decoder health because we still decode every frame (reference frames remain intact).  
- Downside: Network and decoder load stay at 60 fps. We also pay the cost of uploading each frame to GPU memory, so the savings are limited to vita2d draw + swap plus UI time slice.
- **Validation:** Enable the built-in FPS overlay (`draw_fps()` at `vita/src/video.c:865-870`) so we can visualize both incoming and presented frame counts while toggling the dropper.

### 3. Chiaki Receiver Filter (Pre-Decode Dropper)
**Idea:** Teach Chiaki’s `chiaki_video_receiver_flush_frame()` to skip full frame payloads before they reach the Vita decoder whenever we exceed a local fps cap.

- Add a `session->target_stream_fps` (or reuse `session->connect_info.video_profile.max_fps`) and compare the running `frame_index_cur` timestamp to see whether we have received more than 30 frames inside the last 1000 ms window (`lib/src/videoreceiver.c:83-199`).
- If the cap is exceeded, mark the frame as “dropped” but **still** push reference frames into the list via `add_ref_frame()` so P-frames do not reference data we ignored. We can accomplish this by calling `video_sample_cb` with a NULL buffer or by short-circuiting before the callback but after `add_ref_frame()`.
- Upside: Vita never sees or decodes the skipped frames, so CPU/GPU load drops together with presentation rate. Bandwidth savings are still limited (packets are received and then discarded), but we offload the expensive `sceAvcdec` decode.
- Downside: Risky because reference-frame management is delicate; any mismatch will explode with green artifacts. We also need to expose new public API surface in Chiaki just for Vita, so upstream compatibility work is required.
- **Validation:** Instrument the receiver to log how many frames were filtered per second and watch for reference-frame recovery warnings (`lib/src/videoreceiver.c:171-199`). Hitting those warnings after enabling the filter would mean the algorithm broke the decode chain.

### 4. Launch-Time Resolution/FPS Coupling (PS5 Workaround)
**Idea:** Detect when the chosen resolution “forces” 60 fps (e.g., PS5 always) and automatically downgrade the *requested* resolution when the player enables the 30 fps mode.

- Because `chiaki_connect_video_profile_preset()` already maps each preset to bitrate + width/height (`lib/src/session.c:93-137`), we can simply remap the resolution dropdown to 540p whenever the player toggles “Prefer 30 fps`. On PS5 this does **not** change the fps (still 60) but it reduces bandwidth, which indirectly helps the dropper.
- Upside: Guarantees lower bitrate which makes post-decode dropping more feasible.  
- Downside: Cuts resolution; may not satisfy players who want 720p clarity even at lower frame rates. Needs explicit UX messaging in `vita/src/ui.c:1334-1413`.

---

## Recommended Path (Short Term)
1. **Document the toggle behavior clearly in the UI** so testers know that 60 fps is best-effort and that we will clamp to 30 whenever the new dropper layer is enabled. This aligns with README guidance (`README.md:61-72`) and prevents confusion when the device still feels like 30 fps.
2. **Implement the Vita-side pacing layer** first. It is contained, keeps the code in `vita/src/video.c`, and can be validated quickly by turning on `draw_fps()`. This fulfills the “add a layer” request without touching Chiaki. We should also populate `context.stream.fps` during stream setup so the pacer knows the incoming rate.
3. After proving stability, **consider moving the dropper upstream into Chiaki** for extra savings. That change is more invasive, so we should only pursue it if profiling shows the Vita GPU remains the bottleneck even after we stop presenting extra frames.
4. **Log real fps values** (incoming vs. presented) via the existing `curr_fps` counters for regression tracking. This also gives us hard evidence to update `docs/LATENCY_ANALYSIS.md` with improved measurements.

---

## Strategy Decision (2025-02-15)
- **Chosen approach:** Re-enable the Vita-side pacing layer first.
  - PS5 always returns 60 fps regardless of the requested preset, so negotiation-only fixes have no effect. Dropping frames locally is the only lever we control today (`vita/src/host.c:529-587`).
  - The codebase already contains a dormant pacer loop and drop counter (`vita/src/video.c:178-215` and `vita/src/video.c:820-839`); reviving it is low risk compared to inventing a new Chiaki filtering API.
  - Keeping the change Vita-only avoids destabilizing the shared Chiaki libraries and aligns with the “small-batch” guideline from AGENTS.md.
- **Immediate work items:**
  1. Populate `context.stream.fps` and a new `target_fps` when building the connect profile so the pacer can compare source vs. clamp (`vita/include/context.h:37-74`, `vita/src/host.c:529-590`).
  2. Spawn `vita_pacer_thread_main()` again and replace the `/*config.enable_frame_pacer*/false` guard with logic tied to `target_fps` (`vita/src/video.c:178-215`, `vita/src/video.c:526-538`).
  3. Add a deterministic drop pattern (e.g., alternate-frame skip) so the Vita GPU presents exactly 30 unique frames when the clamp is on, regardless of jitter.
  4. Surface a UI toggle under Streaming Settings to let testers enable/disable the clamp at runtime, serializing the flag next to other streaming options (`vita/src/ui.c:1334-1525`, `vita/src/config.c:100-188`).
  5. Instrument logging + the FPS overlay to confirm incoming vs. presented rates before landing the change (`vita/src/video.c:865-870`).
- **Follow-up:** Once pacing is stable, revisit the Chiaki-side dropper idea to further reduce decode cost if profiling shows `sceAvcdec` is still the bottleneck.

---

## Implementation Notes (2025-02-15)
- Added a per-second incoming FPS sampler inside `vita/src/video.c` that logs “incoming vs. requested” cadence (gated by the latency overlay) and stores the count in `context.stream.measured_incoming_fps` for downstream logic.
- Introduced `force_30fps` as a persisted setting + UI toggle (“Force 30 FPS Output”) so testers can enable/disable the clamp at runtime without editing TOML.
- Implemented a deterministic pacing accumulator directly in the video thread: when the clamp is on and the measured/negotiated fps exceeds 30, Vita now drops frames using a Bresenham-style ratio so roughly half of the decoded frames are presented. Existing 30 fps streams (or PS4 sessions) are untouched because the guard checks the measured cadence before skipping.
- README and TODO were updated to track the new option and ensure reviewers validate the feature on hardware.

---

## Open Questions / Next Data Needed
- Does the PS5 ever reject a 30 fps request when we stay at 540p? If not, maybe a negotiation clamp alone is sufficient.
- How expensive is `sceAvcdec` on 60 fps workloads compared to our vita2d blit? We should capture CPU utilization via Vita’s profiler when running 60 fps streams to see whether a post-decode dropper yields noticeable headroom.
- Should we expose the frame-dropper as part of the latency mode presets (`context.config.latency_mode` in `vita/src/host.c:560-568`) so the UI can describe the trade-off in one place?

---

## Validation Plan
1. Enable the FPS overlay temporarily (`vita/src/video.c:865-870`) and log curr/presented fps whenever the dropper is on.
2. Compare measured bitrate + RTT metrics before and after the change via the existing HUD (`vita/src/host.c:154-188`).
3. Smoke-test multiple scenarios: PS5 @720p (likely 60 fps source), PS4 @540p (native 30 fps), and poor Wi-Fi (packet loss) to confirm the dropper does not exacerbate artifact recovery.
4. Update `docs/LATENCY_ANALYSIS.md` with the new measurements once the implementation lands, per AGENTS.md Section 2.

---

**Next Steps:** Decide which strategy to prototype first, add the necessary TODO entry in `TODO.md`, and plan a small-batch implementation that toggles the dropper during runtime (likely via the Streaming Settings panel).
