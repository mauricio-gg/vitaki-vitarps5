# VitaRPS5 Logging Guide

This document covers the new logging pipeline introduced for VitaRPS5. It explains build-time profiles, runtime configuration, and how to collect logs without destabilizing the PS Vita.

## Goals
- Toggle verbose logging on/off without patching code.
- Guarantee that crashes and `LOGE` events still hit disk, even when developers disable regular logs.
- Capture Chiaki warnings/errors from parallel threads without stalling them.

## Build-Time Profiles (`.env`)
Use `.env` files in the repo root to seed compile-time defaults. The build script autoloads `.env.prod` (minimal logging) unless another profile is picked:

```bash
# Verbose build for developer testing
./tools/build.sh --env testing

# Force a specific file
./tools/build.sh --env-file /path/to/custom.env
```

Important command behavior:
- `./tools/build.sh` and `./tools/build.sh debug` produce installable `.vpk` outputs.
- `./tools/build.sh test` only builds the `vitarps5_tests` binary and does not produce/update a `.vpk`.
- If your on-device behavior changed, verify you installed a fresh VPK from a `build`/`debug` command rather than a `test` command.

Supported keys inside `.env.*`:

| Variable | Purpose | Example |
|----------|---------|---------|
| `VITARPS5_LOG_PROFILE` | Default Chiaki mask (`off`, `errors`, `standard`, `verbose`) | `VITARPS5_LOG_PROFILE=verbose` |
| `VITARPS5_LOG_ENABLED` | Whether non-error lines go to disk by default (`true`/`false`) | `VITARPS5_LOG_ENABLED=false` |
| `VITARPS5_FORCE_ERROR_LOGGING` | Keep `CHIAKI_LOG_ERROR`/`LOGE` on disk even if logging is off | `true` |
| `VITARPS5_LOG_QUEUE_DEPTH` | Worker queue depth (8–256) | `128` |
| `VITARPS5_LOG_PATH` | Default Vita log path | `ux0:data/vita-chiaki/vitarps5.log` |

These values become `VITARPS5_*` compile definitions (see `tools/build.sh` and `vita/CMakeLists.txt`). The runtime defaults in `vita/src/logging.c` read them during `vita_logging_config_set_defaults()`.

## Runtime Configuration (`chiaki.toml`)
A new `[logging]` block in `ux0:data/vita-chiaki/chiaki.toml` lets testers override logging on-device without rebuilding:

```toml
[logging]
enabled = true
force_error_logging = true
profile = "verbose"   # off | errors | standard | verbose
path = "ux0:data/vita-chiaki/vitarps5.log"
queue_depth = 128
```

Parsing logic lives in `vita/src/config.c`, and the active values are stored on `context.config.logging`. `profile` translates directly to the Chiaki mask used in `chiaki_log_init()`, so verbose builds capture Takion/hexdump information automatically.

## Crash Logging Guarantee
Even if `enabled = false`, the worker always writes `CHIAKI_LOG_ERROR` and `LOGE` messages when `force_error_logging = true`. This guarantee is enforced in `vita_log_should_write_level()` (`vita/src/logging.c`), so crash reproductions always ship with a usable `.log`.

## Capturing Logs
- Default output file: `ux0:data/vita-chiaki/vitarps5.log` (configurable via `.env` or `[logging].path`).
- Pull the file from VitaShell (`ux0:/data/vita-chiaki/`) instead of copying console text.
- The logging worker (`VitaLogThread`) buffers writes through a fixed queue (default 64) to avoid blocking Chiaki threads; bump `queue_depth` in verbose builds if you see drops.
- For A/B stability runs, prefer `./tools/build.sh --env testing` so logs are written to `ux0:data/vita-chiaki/vitarps5-testing.log` with verbose transport markers.

## Adding New Logs
- Prefer the existing `LOGD`/`LOGE` macros (`vita/include/context.h`). They route through the shared worker and honor the active profile.
- Only gate extremely hot loops with extra checks if needed; otherwise, rely on the logging mask/queue.
- For Chiaki-side logs, call `chiaki_log_*()` with an appropriate level and let the profile filter the callback.

When making architectural changes, update this document and cite the relevant sources (e.g., `vita/src/logging.c` or `tools/build.sh`) so reviewers can trace the behavior quickly.

## Debug HUD Shortcuts (Testing Builds Only)
- Build with `./tools/build.sh --env testing` (or any profile that sets `VITARPS5_DEBUG_MENU=1`).
- On the main dashboard press **L + R + Start** to open the hidden **Debug Actions** palette.
- Available actions:
  1. **Show Remote Play error popup** – renders the new modal overlay so UX changes can be reviewed without forcing a failure.
  2. **Simulate disconnect banner** – greys out the active console card, shows the full-width “Streaming stopped” banner, and blocks reconnect taps for ~4 s.
  3. **Trigger network unstable badge** – fades in the streaming “Network Unstable” pill so styling tweaks can be inspected outside a session.
- While the overlay is visible, inputs to the rest of the UI are blocked; tap anywhere or press Circle to dismiss.
