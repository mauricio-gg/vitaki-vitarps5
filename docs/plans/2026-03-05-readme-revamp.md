# README Revamp Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Rewrite README.md as a standalone product page and extract technical content into dedicated docs.

**Architecture:** Create a feature branch, extract build/config/crash content into 3 new docs, then rewrite README from scratch using the approved "App Store" structure from the design doc at `docs/plans/2026-03-05-readme-revamp-design.md`.

**Tech Stack:** Markdown, git

---

### Task 1: Create feature branch

**Files:**
- None (git operation only)

**Step 1: Create and switch to feature branch**

Run:
```bash
git checkout -b feat/readme-revamp
```
Expected: `Switched to a new branch 'feat/readme-revamp'`

---

### Task 2: Create `docs/BUILDING.md`

Extract build instructions from `README.md:100-192` into a dedicated file.

**Files:**
- Create: `docs/BUILDING.md`

**Step 1: Write `docs/BUILDING.md`**

```markdown
# Building from Source

VitaRPS5 uses a Docker-based build system for consistent, reproducible builds.

## Prerequisites

- Docker installed and running
- Git

## Build Commands

```bash
# Clone the repository
git clone https://github.com/mauricio-gg/vitaki-vitarps5.git
cd vitaki-vitarps5

# Release build (recommended for production)
./tools/build.sh

# Testing build (enables logging output for debugging)
./tools/build.sh --env testing

# Debug build (symbols + verbose logging)
./tools/build.sh debug

# Development shell (for experimentation)
./tools/build.sh shell

# Run test suite
./tools/build.sh test

# Deploy to Vita via FTP
./tools/build.sh deploy <vita_ip>
```

## Build Output

The VPK file will be created in:
- `./build/vitaki-fork.vpk` (main build output)
- `./VitakiForkv0.1.XXX.vpk` (versioned copy in project root)

## Environment Profiles

The build script auto-loads `.env.prod` (default) or any profile you pass via `--env`:
- `./tools/build.sh --env testing` — verbose developer builds with logging enabled
- `./tools/build.sh --env prod` — production-ready builds (logging disabled)

See `docs/ai/LOGGING.md` for the variables each profile controls.

## Testing Notes

- `./tools/build.sh test` only compiles the test executable and does **not** build a `.vpk`
- Use `./tools/build.sh` or `./tools/build.sh debug` when validating streaming behavior on hardware
- Always use `./tools/build.sh --env testing` for debugging — production builds have logging disabled

**Important:** Always use `./tools/build.sh` — never call Docker manually. The script ensures the correct environment and handles versioning automatically.

## Runtime Logs

Logs are written to `ux0:data/vita-chiaki/vitarps5.log` on the Vita. Pull this file to share debug output.

When comparing builds, check these log markers:
- `Bitrate policy: preset_default (...)`
- `Recovery profile: stable_default`
- `Video gap profile: stable_default (hold_ms=24 force_span=12)`

See `docs/ai/STABILITY_AB_TESTING_GUIDE.md` for a full A/B testing procedure.

## Legacy Build System

For compatibility with the original Chiaki build system, you can build using the root CMakeLists.txt:

**Prerequisites:**
- VitaSDK installed and configured
- All Chiaki dependencies installed

```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake \
         -DCHIAKI_ENABLE_VITA=ON \
         -DCHIAKI_ENABLE_GUI=OFF \
         -DCHIAKI_ENABLE_CLI=OFF
make
```

**Note:** The Docker build system is recommended as it handles all dependencies automatically.

## Creating Releases

Releases use a GitHub Actions workflow and can only be created from the `main` branch.

1. Go to **Actions** > **Create Release**
2. Click **Run workflow**
3. Choose version bump type:
   - **patch** (0.0.X) — Bug fixes, small changes
   - **minor** (0.X.0) — New features, non-breaking changes
   - **major** (X.0.0) — Breaking changes, major updates
4. Click **Run workflow**

The workflow automatically calculates the version, builds the VPK, generates a changelog, creates a git tag, and publishes a GitHub release with the VPK attached.
```

**Step 2: Commit**

```bash
git add docs/BUILDING.md
git commit -m "docs: extract build instructions into BUILDING.md"
```

---

### Task 3: Create `docs/CONFIGURATION.md`

Extract config settings from `README.md:247-258` into a dedicated file.

**Files:**
- Create: `docs/CONFIGURATION.md`

**Step 1: Write `docs/CONFIGURATION.md`**

```markdown
# Configuration Guide

VitaRPS5 stores its configuration at `ux0:data/vita-chiaki/chiaki.toml` on the PS Vita.

Most settings can be changed through the in-app Settings screen. This document covers all available options, including those only configurable via the TOML file.

## UI Settings

| Option | Default | Description |
|--------|---------|-------------|
| `circle_btn_confirm` | `false` | Swap circle and cross in the UI. `true` = circle confirms, cross cancels. Does not affect button mappings during remote play. |
| `auto_discovery` | `true` | Auto-start console discovery on launch. Can still be started manually via the Wi-Fi icon. |

## Streaming Settings

| Option | Default | Description |
|--------|---------|-------------|
| `latency_mode` | `"balanced"` | Target bitrate for PS5 streaming. Options: `ultra_low` (~1.2 Mbps), `low` (~1.8 Mbps), `balanced` (~2.6 Mbps), `high` (~3.2 Mbps), `max` (~3.8 Mbps). Also configurable via the latency dropdown in Settings. |
| `stretch_video` | `false` | `false` = letterboxed, `true` = stretched to fill screen. Also available as "Fill Screen" in Streaming Settings. |
| `force_30fps` | `false` | `true` = drop frames locally to present at 30 FPS. Reduces GPU workload at the cost of smoothness. Also available as "Force 30 FPS Output" in Streaming Settings. |

## Advanced / Diagnostic Settings

| Option | Default | Description |
|--------|---------|-------------|
| `send_actual_start_bitrate` | `true` | Send requested bitrate in `RP-StartBitrate` header. **PS5 Quirk:** Current firmware ignores this and forces ~1.5 Mbps regardless. Keep enabled for telemetry. |
| `clamp_soft_restart_bitrate` | `true` | Force Chiaki soft restarts to request ≤1.5 Mbps. Prevents packet-loss fallbacks from spiking Wi-Fi. Also available in Streaming Settings. |

## Resetting Configuration

If you experience issues, delete the config file to reset all settings to defaults:

Delete `ux0:data/vita-chiaki/chiaki.toml` using VitaShell or FTP.
```

**Step 2: Commit**

```bash
git add docs/CONFIGURATION.md
git commit -m "docs: extract configuration reference into CONFIGURATION.md"
```

---

### Task 4: Create `docs/CRASH_ANALYSIS.md`

Extract crash dump analysis from `README.md:280-297` into a dedicated file.

**Files:**
- Create: `docs/CRASH_ANALYSIS.md`

**Step 1: Write `docs/CRASH_ANALYSIS.md`**

```markdown
# Crash Dump Analysis

If the PS Vita throws a `C2-12828-1` crash, it generates a `.psp2dmp` file in `ux0:data/`. You can decode this locally to get a file/line backtrace.

## Setup

Initialize the parser submodule (one-time):

```bash
git submodule update --init scripts/vita/parse_core
```

## Decoding a Crash Dump

1. Pull the `.psp2dmp` file from `ux0:data/` on the Vita
2. Enter the dev container (pyelftools is preinstalled):

```bash
./tools/build.sh shell
```

3. Inside the shell, run the parser against the dump and your debug ELF:

```bash
python3 scripts/vita/parse_core/main.py \
  /build/git/path/to/psp2core-xxxxxxxxx.psp2dmp \
  /build/git/build/vita/VitaRPS5.elf
```

The script prints thread states, register dumps, and a file/line backtrace.

**Note:** You need a debug build (`./tools/build.sh debug`) to get meaningful symbol information in the backtrace.
```

**Step 2: Commit**

```bash
git add docs/CRASH_ANALYSIS.md
git commit -m "docs: extract crash dump analysis into CRASH_ANALYSIS.md"
```

---

### Task 5: Rewrite `README.md`

Replace the entire README with the new standalone structure.

**Files:**
- Modify: `README.md` (full rewrite)

**Step 1: Write the new README.md**

Replace the entire file with:

````markdown
<div align="center">
  <img src="vita/res/icon0.png" alt="VitaRPS5" width="150"/>

  # VitaRPS5

  **PS5 Remote Play on PS Vita**

  [![License](https://img.shields.io/badge/License-AGPL%20v3-blue.svg)](LICENSE)
  [![Build Status](https://img.shields.io/github/actions/workflow/status/mauricio-gg/vitaki-vitarps5/release.yml?branch=main)](https://github.com/mauricio-gg/vitaki-vitarps5/actions)
  [![Latest Release](https://img.shields.io/github/v/release/mauricio-gg/vitaki-vitarps5)](https://github.com/mauricio-gg/vitaki-vitarps5/releases/latest)
</div>

---

## Screenshots

<div align="center">
  <img src="https://github.com/user-attachments/assets/881bdc80-acde-4690-992c-6785b89300c6" alt="Main Screen" width="45%"/>
  <img src="https://github.com/user-attachments/assets/f5213d0f-589c-44ad-9234-5e25a8f8ac06" alt="Main Screen with Menu" width="45%"/>
  <img src="https://github.com/user-attachments/assets/0c5af5c4-e4cf-40e4-b981-03f1ef04c65f" alt="Controller Mapping" width="45%"/>
  <img src="https://github.com/user-attachments/assets/8b6f592e-6f6c-47a4-bc64-fb15ccd973b8" alt="Streaming Settings" width="45%"/>
</div>

## Support This Project

VitaRPS5 has been a labor of love for the PS Vita community. If you enjoy it, consider supporting continued development.

☕ [Buy Me a Coffee](https://buymeacoffee.com/solidem)

## Features

- **Modern UI** — Card-based console selection, wave navigation sidebar, tabbed settings, PlayStation-themed design
- **Low-Latency Streaming** — Tuned thread priorities, 500Hz input polling, optimized network buffers
- **Controller Customization** — 3 custom preset slots, fullscreen interactive controller diagram with front and back views
- **Smart Connection Flow** — Auto-discovery, animated wake-up with progress countdown, automatic streaming start
- **Packet-Loss Recovery** — Reconnecting overlay, automatic bitrate adjustment, seamless retry without crashing
- **Flexible Video Settings** — Latency presets (ultra-low to max), fill/letterbox toggle, 30/60 FPS control
- **Full Control Support** — All buttons mapped including L2/R2/L3/R3, touchpad, and motion controls

## Installation

1. Download the latest `.vpk` from [Releases](https://github.com/mauricio-gg/vitaki-vitarps5/releases/latest)
2. Transfer the VPK to your PS Vita
3. Install using VitaShell or your preferred installer

## Getting Started

### Pairing Your Console

1. Connect your PS Vita and PS5 to the same Wi-Fi network
2. Log in to the same PSN account on both devices
3. Open VitaRPS5 — your console should appear automatically
4. Select the console and enter the pairing code from **PS5 → Settings → System → Remote Play → Pair Device**
5. That's it — future connections won't ask for the code again

**Tip:** If your PSN ID isn't detected, go to the Profile page, select the Profile card, and press X to refresh your Account ID.

**Note:** Remote play over the internet is not yet supported. VitaRPS5 currently works over local Wi-Fi only. Internet remote play is planned for a future release.

## Controls

| Input | Action |
|-------|--------|
| **L + R + Start** (hold ~1s) | Stop streaming, return to menu |
| **Select + Start** | PS (Home) button |

You can show or hide the exit shortcut hint and latency stats in **Settings**.

## Troubleshooting

- **Crash C2-12828-1** — May be caused by incompatible plugins (e.g., reRescaler). Try removing them.
- **Config issues** — Delete `ux0:data/vita-chiaki/chiaki.toml` to reset all settings to defaults.
- **Logs** — Testing builds write to `ux0:data/vita-chiaki/vitarps5-testing.log`. See [Building from Source](docs/BUILDING.md) for how to create a testing build.

For more help, open an [issue](https://github.com/mauricio-gg/vitaki-vitarps5/issues).

## Documentation

- [Configuration Guide](docs/CONFIGURATION.md) — All `chiaki.toml` settings
- [Building from Source](docs/BUILDING.md) — Docker build system, debug builds, deployment
- [Crash Dump Analysis](docs/CRASH_ANALYSIS.md) — Decoding `.psp2dmp` crash files
- [Wi-Fi Optimization](docs/WIFI_OPTIMIZATION.md) — Network tips for best streaming performance

## Acknowledgements

VitaRPS5 is built on the [Chiaki](https://git.sr.ht/~thestr4ng3r/chiaki) open-source Remote Play stack. PS Vita support was pioneered by [AAGaming](https://github.com/AAGaming00) and significantly enhanced by [ywnico](https://github.com/ywnico). Special thanks to [Epicpkmn11](https://github.com/Epicpkmn11) for motion control contributions.

Additional thanks to the open-source tools that made reverse engineering possible: [Rizin](https://rizin.re), [Cutter](https://cutter.re), [Frida](https://www.frida.re), and [x64dbg](https://x64dbg.com). Thanks also to [delroth](https://github.com/delroth) for registration/wakeup protocol analysis, [grill2010](https://github.com/grill2010) for PSN OAuth analysis, and [FioraAeterna](https://github.com/FioraAeterna) for FEC/error correction insights.

## License

[AGPL-3.0](LICENSE)

Originally created by Florian Märkl. Ported to PS Vita by AAGaming. Enhanced by ywnico. Developed by [solidEm](https://github.com/mauricio-gg) with the help of [Claude Code](https://claude.com/claude-code).
````

**Step 2: Commit**

```bash
git add README.md
git commit -m "docs: rewrite README as standalone VitaRPS5 product page

Removes all fork language and repositions VitaRPS5 as a standalone
project. Unifies features into a single list, streamlines for
end-users, and moves technical content to dedicated docs."
```

---

### Task 6: Update CLAUDE.md references

Update `CLAUDE.md` to reflect the new doc locations so Claude agents find the right files.

**Files:**
- Modify: `CLAUDE.md`

**Step 1: Update doc references in CLAUDE.md**

In the "Key Documents" list (section 1), add references to the new docs:
- `docs/BUILDING.md` — build instructions (extracted from README)
- `docs/CONFIGURATION.md` — all chiaki.toml settings (extracted from README)
- `docs/CRASH_ANALYSIS.md` — crash dump analysis (extracted from README)

In section 4 "Build, Run, Test", update the clone URL from `vitaki-fork` to `vitaki-vitarps5` if present.

In section 6 "Reference Material Checklist", add the three new docs.

**Step 2: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: update CLAUDE.md references for new doc structure"
```

---

### Task 7: Final review and cleanup

**Step 1: Verify all links resolve**

Check that every link in the new README points to a file that exists:
```bash
# Check doc links exist
ls docs/BUILDING.md docs/CONFIGURATION.md docs/CRASH_ANALYSIS.md docs/WIFI_OPTIMIZATION.md
```
Expected: All four files listed without error.

**Step 2: Verify no fork language remains in README**

```bash
grep -i "fork\|vitaki-fork\|ywnico's\|this fork\|what's new in" README.md
```
Expected: No matches (grep returns exit code 1).

**Step 3: Verify badge URLs are correct**

```bash
grep "mauricio-gg/vitaki-vitarps5" README.md
```
Expected: 3 matches (License, Build Status, Latest Release badges).

**Step 4: Review the full README one more time**

Read through the final README to catch any issues.
