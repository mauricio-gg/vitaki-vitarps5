# Logging System Defaults Fix

## Problem
The logging system in `vita/src/logging.c` had hardcoded fallback defaults configured for DEBUG mode:
- `VITARPS5_LOGGING_DEFAULT_ENABLED = 1` (logging ON by default)
- `VITARPS5_DEFAULT_LOG_PROFILE = VITA_LOG_PROFILE_STANDARD` (includes DEBUG/INFO/WARNING/ERROR)

When the build system failed to pass CMake logging configuration flags, these DEBUG defaults would silently apply to production builds, causing unwanted verbose logging output.

## Solution
Modified `vita/src/logging.c` (lines 12-24) to use production-safe fallback defaults:
1. Set `VITARPS5_LOGGING_DEFAULT_ENABLED` to `0` (logging OFF by default)
2. Set `VITARPS5_DEFAULT_LOG_PROFILE` to `VITA_LOG_PROFILE_ERRORS` (only critical errors)
3. Kept `VITARPS5_LOGGING_DEFAULT_FORCE_ERRORS` at `1` (still log errors even when logging is disabled)

## Implementation Details

### Before (Debug-Friendly Defaults)
```c
#ifndef VITARPS5_LOGGING_DEFAULT_ENABLED
#define VITARPS5_LOGGING_DEFAULT_ENABLED 1  // Default ON for debug
#endif

#ifndef VITARPS5_DEFAULT_LOG_PROFILE
#define VITARPS5_DEFAULT_LOG_PROFILE VITA_LOG_PROFILE_STANDARD  // DEBUG/INFO/WARNING/ERROR
#endif
```

### After (Production-Safe Defaults)
```c
// Production-safe fallback defaults: minimal logging if build system fails to configure.
// Build scripts (CMakeLists.txt, build.sh) override these via -D flags for debug/testing builds.
#ifndef VITARPS5_LOGGING_DEFAULT_ENABLED
#define VITARPS5_LOGGING_DEFAULT_ENABLED 0  // Default OFF for production safety
#endif

#ifndef VITARPS5_DEFAULT_LOG_PROFILE
#define VITARPS5_DEFAULT_LOG_PROFILE VITA_LOG_PROFILE_ERRORS  // Only critical errors by default
#endif
```

## Build System Integration
The changes preserve the ability for build scripts to override these values via CMake flags:

- **Production builds** (`./tools/build.sh`): Uses `.env.prod` with minimal logging
- **Testing builds** (`./tools/build.sh --env testing`): Uses `.env.testing` with verbose logging
- **Debug builds** (`./tools/build.sh debug`): Enables full debug output

Build system configuration chain:
1. `tools/build.sh` reads environment file (`.env.prod`, `.env.testing`, etc.)
2. Passes values to CMake via `-DVITARPS5_LOGGING_DEFAULT_ENABLED=<value>`
3. `vita/CMakeLists.txt` applies these as compile definitions
4. `vita/src/logging.c` uses the overrides if defined, otherwise falls back to production-safe defaults

## Safety Guarantee
If the build system configuration chain breaks:
- Old behavior: Would silently fall back to verbose DEBUG logging (insecure)
- New behavior: Falls back to minimal ERROR-only logging (secure by default)

This implements a "fail-safe" rather than "fail-open" security model for logging configuration.

## Testing
Both build modes verified:
- Production build (`./tools/build.sh`): ✓ Compiles successfully with new defaults
- Testing build (`./tools/build.sh --env testing`): ✓ Properly overrides defaults with verbose logging

## Files Modified
- `/Users/mauriciogaldos/Developer/AndeanBear/vitarps5/vita/src/logging.c` (lines 12-24)

## Related Documentation
- Build system: `docs/ai/BUILD_SYSTEM_SETUP.md`
- Environment configuration: `.env.prod`, `.env.testing`
- CMake integration: `vita/CMakeLists.txt` (lines 49-59)
