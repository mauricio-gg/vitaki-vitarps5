# TODO

## Latency Reduction Initiative

### Network & Recovery
- [ ] **GH #206** - Proximity A/B test (RSSI-independent jitter): RSSI 84→100 refuted signal-strength hypothesis; Vita Wi-Fi burstiness is intrinsic. PS5 throttling milder this run (~5.4Mbps target). 249 overflow drops in ~30s. Next: Focus on loss-report capping (chiaki-ng-style ~10% in lib/src/congestioncontrol.c) and client-side burst absorption. Hardware validation pending.
- [ ] **GH #208 Validation (PR #209 merged 205eed56)** - Pending hardware validation checklist:
  - [ ] ENOBUFS burst → single WARN + continued streaming (transient retry + consecutive-error escalation working)
  - [ ] Transport death → "Transport disconnected" banner within ~2s, no force-close required
  - [ ] Console sleep still reports REMOTE_SHUTDOWN (unchanged)
  - [ ] User stop unchanged (session teardown clean)
  - [ ] EBADF flood eliminated (socket invalidated before close)
- [ ] Loss-report capping implementation (cap at ~10% like chiaki-ng to prevent PS5 over-throttling)
- [ ] Client-side burst absorption strategy (network buffer tuning, jitter smoothing)

## UI & Features

### Modern UI Migration
- [ ] Wave navigation system implementation
- [ ] Console card layout with status indicators
- [ ] Tabbed settings interface

## Documentation
- [ ] Update INCOMPLETE_FEATURES.md with GH #206 findings
- [ ] Document burst-absorption strategy in WIFI_OPTIMIZATION.md
