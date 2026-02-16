# Codebase Cleanup Plan (Vitaki Delta-Guided)

## Summary
This cleanup initiative targets Vita runtime files that have grown significantly versus `remotes/vitaki/ywnico`, while preserving VitaRPS5 enhancements (modern UI, recovery hardening, diagnostics, and controller behavior).

Primary inflation hotspots:
- `vita/src/host.c` (+1957 lines)
- `vita/src/config.c` (+565 lines)
- `vita/src/video.c` (+343 lines)
- `vita/include/context.h` (+146 lines)

Guiding constraints:
- Keep functionality and existing enhancements.
- Prefer micro-batches by file/subsystem.
- Keep public behavior stable; refactor internals.
- Validate each batch with Docker build/test flow.

## Scope
### In scope (wave 1)
- Runtime refactors in `host.c`, `config.c`, `video.c`, and `context.h`.
- Dead-code removal and duplicate-logic elimination.
- Decomposition of monolithic logic into focused modules.
- Memory ownership and state-transition hardening.

### Out of scope (wave 1)
- Rewriting Chiaki core transport internals unless strictly required.
- UI redesign; UI module decomposition is already complete.

## Baseline and Measurement
- Baseline branch: `remotes/vitaki/ywnico`.
- Comparison script: `tools/analysis/compare_vitaki_delta.sh`.
- Success metrics:
  - `host.c`: reduce by >=25% without behavior regressions.
  - `config.c`: reduce by >=20%.
  - `video.c`: reduce by >=20%.
  - No streaming lifecycle regressions in testing build logs.

## Implementation Batches
### Batch 1: Tooling + config cleanup
- Add file inflation reporting script.
- Refactor repetitive settings migration parsing in `config.c` into table-driven logic.
- Fix memory ownership bug in manual-host parse failure path.

### Batch 2: Host lifecycle decomposition
- Split `host.c` into focused units:
  - session lifecycle
  - restart/recovery policy
  - diagnostics/metrics
- Replace recursive retry/restart call paths with queued restart intent where possible.

### Batch 3: Context struct decomposition
- Group `VitaChiakiStream` fields by lifecycle/recovery/diagnostics/render ownership.
- Document field ownership expectations (input thread, session thread, UI thread).

### Batch 4: Video module extraction
- Move stream HUD/overlay rendering from `video.c` to dedicated overlay module.
- Keep decoder and renderer responsibilities separated.
- Remove stale debugging code paths not used in production.

### Batch 5: Validation and docs
- Validate each batch with:
  - `./tools/build.sh debug`
  - `./tools/build.sh test`
  - `./tools/build.sh --env testing` (required for local/hardware stream validation)
- Update `TODO.md` and `PROGRESS.md` with completion milestones.

## Risk Controls
- Keep function signatures stable for externally used runtime entrypoints.
- Isolate refactors from behavior changes unless fixing verified bugs.
- Validate with stream start/stop, reconnect, and packet-loss fallback scenarios.
- Land in micro-batches for easier review and rollback.

## Current Status
- Batch 1 completed:
  - table-driven bool migration parsing landed in `vita/src/config.c`
  - manual-host ownership cleanup fix landed in `vita/src/config.c`
  - validation run with `./tools/build.sh debug` and `./tools/build.sh --env testing`
- Batch 4 partially completed:
  - stream HUD/indicator rendering extracted from `vita/src/video.c` to `vita/src/video_overlay.c`
- Batch 2 partially completed:
  - host storage/manual-host utilities extracted to `vita/src/host_storage.c`
  - input-thread/controller-touch mapping extracted to `vita/src/host_input.c`

- Planned immediate next coding steps:
  1. Continue Batch 2 by extracting restart/recovery policy helpers from `vita/src/host.c`.
  2. Begin Batch 3 by grouping and documenting `VitaChiakiStream` field ownership in `vita/include/context.h`.
  3. Re-run validation after each micro-batch (`debug`, `--env testing`; `test` once `vitarps5_tests` target exists again).
