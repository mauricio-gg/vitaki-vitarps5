# TODO

## Streaming Robustness & Recovery Investigation
- [ ] Investigate lib-side suspend/resume detection for PS-button-suspend freeze recovery
  * Goal: Detect socket death at transport layer instead of app-level frame stall detection
  * Lever 1: ENOBUFS/EBADF escalation in `lib/src/takion.c` send path (line ~TBD for reference)
  * Lever 2: DISCONNECT-during-streaming path in `lib/src/streamconnection.c` (currently gated to STATE_TAKION_CONNECT, line ~435)
  * Deliverable: Clean session teardown before PS5 releases old session (avoid RP_IN_USE reconnect rejection)
  * Replaces reverted app-level watchdog approach (PR #196); see DEPRECATED.md for context

- [ ] Investigate 20% sustained packet-loss → 1500 kbps PS5 bitrate floor
  * Problem: User reports lag "as soon as we hit Network unstable"; logs 20361349999 and 20639381559 show PS5 encoder throttled to 1500 kbps floor by sustained ~20% raw loss (66+ capped-loss events)
  * Candidate levers:
    - 40ms Vita jitter-buffer ceiling (commit a826396) — evaluate if buffer is too tight for bursty loss
    - Loss-cap reporting (PR #191) — verify if loss-rate calculation is accurate or masking true loss
    - Recv path improvements (Takion RX threading, buffer alignment)
  * Success criterion: Maintain >3 Mbps bitrate under 20% loss, or gracefully degrade with user-visible messaging instead of silent throttle

## Motion Macroblocking Fix – Complete (v0.1.787)
- [x] Task 1: Align ChiakiVideoReceiver.reference_frames[] from 16 → 8 (PR #186)
- [x] Task 2: IDR request on first missing ref, not third (PR #187)
- [x] Validated on-device: "Requesting IDR (missing_ref)" fires at cascade=1 correctly
- [x] Verified: No IDR flooding, cascade backstop (depth 3) working as fallback

## FPS Health Investigation – Hardware Testing Phase (Pending)
- [ ] Deploy testing build to PS Vita via `./tools/build.sh --env testing` (with --enhanced-recovery flag)
- [ ] Conduct baseline test: Stream at normal distance, verify stable 30fps
- [ ] Conduct degraded test: Move 2x distance from router, measure cascade recovery time
- [ ] Measure: Reduced IDR recovery time (P0: 200ms→100ms cooldown) improvement metrics
- [ ] Verify: Cascade skip working (P1a: 3+ missing refs skip decode) – compare frame times
- [ ] Test: Different WiFi conditions (strong, weak, congested)
- [ ] Check: No excessive soft restarts or bitrate oscillation
- [ ] Verify: P1b CASCADE_ALARM remains disabled (soft restart feature disabled due to Takion v12 handshake failure)

## Controller Layout Redesign – Hardware Validation (Deferred)
- [ ] Test menu overlay navigation on hardware
- [ ] Verify focus loops (Menu → Preset → Toggle → Legend) on Vita device
- [ ] Validate front/back flip animation performance at 60 FPS
- [ ] Test preset persistence across app restarts
- [ ] Validate button mapping accuracy across all 6 presets on real hardware

## Remaining Micro-Interactions & Polish
- [ ] Implement panel transition animations (slide + fade, 120ms)
- [ ] Add instruction bar with DualShock glyphs
- [ ] Complete touch input parity for all micro-animations

## Settings & Configuration
- [ ] Power menu implementation (see INCOMPLETE_FEATURES.md)
- [ ] Manual host deletion flow

## Performance & Optimization
- [ ] Review additional draw call optimization opportunities
- [ ] Profile particle system on real hardware
- [ ] Benchmark wave animation at lowest vita2d quality settings
- [ ] Audit focus manager performance in modal stacking scenarios

## Testing & Validation
- [ ] Capture video demonstrating all Batch 1-4 animations at target FPS
- [ ] Hardware validation on PS Vita device
- [ ] Verify touch + controller input parity across all features
- [ ] Test modal focus stacking (error popup, debug menu, registration) under edge cases
