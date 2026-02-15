<div align="center">
  <img src="vita/res/icon0.png" alt="VitaRPS5 Icon" width="150"/>

  # VitaRPS5 (Vitaki Fork)

  **PlayStation 5 Remote Play for PS Vita**

  [![License](https://img.shields.io/badge/License-AGPL%20v3-blue.svg)](LICENSE)
  [![Build Status](https://img.shields.io/github/actions/workflow/status/mauricio-gg/vitaki-fork/release.yml?branch=main)](https://github.com/mauricio-gg/vitaki-fork/actions)
  [![Latest Release](https://img.shields.io/github/v/release/mauricio-gg/vitaki-fork)](https://github.com/mauricio-gg/vitaki-fork/releases/latest)
</div>

---

This is a fork built on top of [@ywnico](https://github.com/ywnico)'s excellent [vitaki-fork](https://github.com/ywnico/vitaki-fork), which itself is based on [@AAGaming00](https://github.com/AAGaming00)'s [Vitaki](https://git.catvibers.me/aa/chiaki).

**Credit where it's due:** ywnico did the vast majority of the hard work, implementing audio, controls, external network support, wakeup functionality, and numerous critical fixes. AAGaming did the foundational work of porting Chiaki to PS Vita. This fork simply adds UI enhancements and quality-of-life improvements on top of their solid foundation.

## Screenshots

<div align="center">
  <img src="https://github.com/user-attachments/assets/881bdc80-acde-4690-992c-6785b89300c6" alt="Main Screen" width="45%"/>
  <img src="https://github.com/user-attachments/assets/f5213d0f-589c-44ad-9234-5e25a8f8ac06" alt="Main Screen with Menu" width="45%"/>
  <img src="https://github.com/user-attachments/assets/0c5af5c4-e4cf-40e4-b981-03f1ef04c65f" alt="Controller Mapping" width="45%"/>
<img src="https://github.com/user-attachments/assets/8b6f592e-6f6c-47a4-bc64-fb15ccd973b8" alt="Streaming Settings" width="45%"/>

</div>

## Support This Project

This fork has been a labor of love for the PS Vita community, developed over 3+ months across several design iterations (we even had to start over at one point!). If you enjoy the UI enhancements and improvements, consider buying me a coffee to support continued development. More enhancements are coming!

☕ [Buy Me a Coffee](https://buymeacoffee.com/solidem)

## What's New in This Fork

This fork adds the following enhancements to ywnico's vitaki-fork:

1. **Latency & Performance Optimizations**
   - **Thread priority tuning**: Input thread at priority 96 (highest), audio/video at 64, with CPU affinity to prevent contention
   - **Faster input polling**: Reduced from 5ms to 2ms (500Hz sampling) for 2.5× faster controller response
   - **Optimized network queue**: Trimmed Takion reorder buffer from 128 to 64 packets to reduce buffering latency
   - **Estimated improvement**: 20-40ms total latency reduction, input lag reduced from ~20-30ms to ~10ms
   - See `docs/LATENCY_QUICK_WINS.md` for full details

2. **Controller Customization**
   - **3 custom preset slots**: Create and save your own button mappings (Custom 1, Custom 2, Custom 3)
   - **Immersive controller screen**: Fullscreen PS Vita diagram with procedurally-rendered graphics
   - **Front/back view navigation**: D-pad or touch to switch between front controls and rear touchpad
   - **Visual mapping**: See all button assignments on an interactive controller diagram
   - **Settings integration**: Controller tab in Settings page with L/R navigation

3. **VitaRPS5-Style UI Redesign**
   - Modern card-based console selection interface
   - Professional PlayStation-themed color scheme
   - Wave-animated navigation sidebar with collapsible menu (Triangle to open/close)
   - Redesigned PIN entry screen with individual digit display
   - Centralized focus manager for smooth D-pad navigation
   - Tabbed settings page (Streaming, Controller) with visual polish

4. **Enhanced Console Wake Flow**
   - "Waking up console..." screen with animated progress
   - 30-second timeout with visual countdown
   - Automatic streaming start when console wakes up
   - No need to press X twice

5. **Fixed Controller Input Issues**
   - Resolved race condition that prevented controller input during streaming
   - Properly separated UI and input thread buffer access
   - Controllers now work reliably during remote play
   - Input stays responsive during packet-loss fallback recovery

6. **Improved Console Management**
   - Re-pairing now properly deletes registration data from storage
   - Better console name and IP display formatting
   - Circle button for cancel (PlayStation convention)

7. **Packet-Loss Fallback & Overlay**
   - When Ultra Low still drops frames, the client now pauses briefly, displays a reconnecting overlay, and restarts streaming at an even lower bitrate instead of crashing back to the menu
   - Automatic retries keep discovery paused and resume seamlessly once the link stabilizes
   - Controller input continues flowing during recovery to prevent input gaps

## Features from ywnico's vitaki-fork

All the features from ywnico's fork are included:
1. Implemented audio
2. Implemented controls
    - Control mappings for L1, R1, L2, R2, L3, R3, and touchpad (trapezoid button), including remappable shoulder triggers in custom presets. Note that `Select` + `Start` sends the PS (home) button.
    - Motion controls (thanks to [@Epicpkmn11](https://github.com/Epicpkmn11), who also contributed some other controller improvements)
3. Implemented external network remote play (with manually-specified remote IP addresses)
4. Fixed console wakeup
5. Made debug logs visible, added tooltips on some buttons
6. Fixed instant disconnection bug
7. Disabled `vblank_wait` and set fps to 30 by default to reduce lag.
    - NOTE: fps is now persisted from settings/config (`chiaki.toml`) and supports 30/60.
8. Merged in updates from chiaki4deck (improved some connection issues)
9. Included [ghost's LiveArea icon fixes](https://git.catvibers.me/aa/chiaki/pulls/13)
10. Many bug and crash fixes

## Building from Source

This project supports two build methods:

### Modern Build System (Recommended - Default)

Uses Docker for consistent, reproducible builds across all platforms. The build script handles all Docker operations automatically.

**Prerequisites:**
- Docker installed and running
- Git (to clone the repository)

**Build Commands:**
```bash
# Clone the repository
git clone https://github.com/mauricio-gg/vitaki-fork.git
cd vitaki-fork

# Release build (recommended)
./tools/build.sh

# Debug build (with symbols and verbose logging)
./tools/build.sh debug

# Development shell (for experimentation)
./tools/build.sh shell

# Run test suite
./tools/build.sh test

# Deploy to Vita via FTP
./tools/build.sh deploy <vita_ip>
```

The VPK file will be created in:
- `./build/vitaki-fork.vpk` (main build output)
- `./VitakiForkv0.1.XXX.vpk` (versioned copy in project root)

Runtime logs (including Chiaki transport warnings) are also written to `ux0:data/vita-chiaki/vitarps5.log` on the Vita, so you can pull that file to share debug output without capturing console text.

**Environment profiles:** the build script auto-loads `.env.prod` (default) or any profile you pass via `--env`. For verbose developer builds run `./tools/build.sh --env testing`; for production-ready builds stick with `--env prod`. See `docs/ai/LOGGING.md` for the variables each profile controls.

`./tools/build.sh test` only compiles the `vitarps5_tests` executable and does not build or update a `.vpk`. Use `./tools/build.sh` or `./tools/build.sh debug` when validating streaming behavior on hardware.

**Note:** Always use `./tools/build.sh` - never call Docker manually. The script ensures the correct environment and handles versioning automatically.

### Legacy Build System

For compatibility with the original Chiaki build system, you can build using the root CMakeLists.txt:

**Prerequisites:**
- VitaSDK installed and configured
- All Chiaki dependencies installed

**Build Commands:**
```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake \
         -DCHIAKI_ENABLE_VITA=ON \
         -DCHIAKI_ENABLE_GUI=OFF \
         -DCHIAKI_ENABLE_CLI=OFF
make
```

**Note:** The modern build system (Docker) is recommended as it handles all dependencies automatically.

### Creating Releases

This project uses a GitHub Actions workflow to create releases. **Releases can only be created from the `main` branch.**

**To create a new release:**

1. Go to the **Actions** tab on GitHub
2. Select **"Create Release"** workflow
3. Click **"Run workflow"**
4. Choose the version bump type:
   - **patch** (0.0.X) - Bug fixes, small changes
   - **minor** (0.X.0) - New features, non-breaking changes
   - **major** (X.0.0) - Breaking changes, major updates
5. Click **"Run workflow"**

**The workflow will automatically:**
- Calculate the new version number from the latest tag
- Build the VPK using Docker
- Generate a changelog from merged PRs and commits
- Create and push a git tag
- Create a GitHub release with the VPK attached
- Include installation instructions and changelog

**Example:** If the latest tag is `v1.2.3` and you select `minor`, the new version will be `v1.3.0`.

**Note:** The `main` branch should be protected to ensure releases are created only through approved PRs.

## Instructions
### Local connection
1. Connect PS Vita and PS5 (or PS4) to the same local WiFi network.
2. Log in to the same PSN account on both the PS5 and the Vita.
3. Open Vitaki on PS Vita.
4. Check settings (gear icon) to ensure your encoded PSN ID is there (if it's not automatically populated, or you accidentally deleted it, press START to re-detect it).
5. The console should be automatically detected and appear as an icon.
6. Select the console and Vitaki should ask for a registration code. On the PS5, navigate to `Settings > System > Remote Play` and select `Pair Device`. An 8-digit numeric code should appear; enter this into Vitaki and hit triangle to save.
7. Select the console again in Vitaki. It should now connect (and in the future, will not ask for the device pairing code).

### In-stream controls
- Hold **L + R + Start** (Options) for about a second to stop the current Remote Play session and return to the VitaRPS5 menu. This lets you tweak settings (e.g., latency mode) without rebooting the app.
- You can show or hide the top-right shortcut hint in **Settings → Show Exit Shortcut Hint**.
- Enabling **Settings → Show Latency** now also shows a compact in-stream stats panel (live latency + FPS).

### Controller customization
VitaRPS5 provides 3 customizable controller preset slots that let you create your own button mappings:

1. **Access Controller Settings**
   - Open the navigation menu (Triangle button or tap the menu pill)
   - Select the Controller icon (gamepad)
   - OR: Go to Settings → Controller tab (press L/R to switch tabs)

2. **Select a Custom Preset**
   - You'll see 3 slots: Custom 1, Custom 2, Custom 3
   - Use D-pad LEFT/RIGHT or touch to cycle between presets
   - Each preset has independent button mappings

3. **View Button Mappings**
   - The controller diagram shows all current button assignments
   - **Front View**: D-pad, face buttons (△○×□), analog sticks, L/R shoulders, Start/Select/PS
   - **Back View**: Rear touchpad zones (4 quadrants: Upper-Left, Upper-Right, Lower-Left, Lower-Right)
   - Press D-pad UP/DOWN or touch to switch between front and back views

4. **Customize Mappings** (Future Feature)
   - Currently, you can view and select presets
   - Per-button customization is planned for a future update
   - See `docs/INCOMPLETE_FEATURES.md` for roadmap

**Tip**: Each custom slot saves automatically. Switch between them to find what works best for different game types (FPS, racing, fighting games, etc.).

### Remote connection
UDP holepunching is not supported. Instead, a remote connection requires a static IP and port forwarding.

1. Register your console on your local network following the above instructions.
2. Follow the "manual remote connection" section in [these instructions](https://streetpea.github.io/chiaki-ng/setup/remoteconnection/#manual-remote-connection) to set up a static IP and port forwarding on your network.
3. Select the `add remote host` button (the leftmost button in the toolbar) in Vitaki. Enter the remote IP address and the registered console.

If you are on the local network, your console will be discovered locally and a separate tile for remote connection will not be shown. If you want to test on the local network, turn off discovery (wifi icon in the toolbar).

Currently, Vitaki cannot detect the status of remote hosts. Therefore, when selecting one, it will both send the wakeup signal and immediately try to start remote play. If the console was asleep, then this first attempt at remote play will fail, so try again in 10 or 15 seconds.

Note: if the remote host cannot be reached, it will get stuck on "Trying to request session" for 90 seconds and then time out. If the remote host was reachable but asleep, "Trying to request session" should fail after just a few seconds.

## Config settings
Some configuration lacks a UI but can be set in the config file located at `ux0:data/vita-chiaki/chiaki.toml`.
- `circle_btn_confirm = true` swaps circle and cross in the main UI, so that circle is confirm and cross is cancel (`false` makes cross into confirm and circle into cancel). Note that this does not affect the button mappings in remote play, only in the UI before remote play starts.
- `auto_discovery = false` makes Vitaki not start discovery on launch. It can still be started manually by selecting the wifi icon.
- `latency_mode = "balanced"` targets a specific bitrate for PS5 streaming. Options:  
  `ultra_low` (≈1.2 Mbps), `low` (≈1.8 Mbps), `balanced` (≈2.6 Mbps), `high` (≈3.2 Mbps), `max` (≈3.8 Mbps, ~95% of Vita Wi-Fi). Use the new latency dropdown in Settings to switch modes without editing the file.
- Startup/recovery now uses a single stable default path (Vitaki-like preset bitrate plus conservative recovery), so no `stability_profile` toggle is required.
- `stretch_video = false` keeps incoming frames centered with letterboxing. Set to `true` (or toggle “Fill Screen” under Streaming Settings) if you prefer the 360p/540p output stretched across the display.
- `force_30fps = false` disables the new 30 fps presentation clamp. Set to `true` (or toggle “Force 30 FPS Output” under Streaming Settings) to make the Vita drop frames locally whenever the PS5 insists on a 60 fps stream. This keeps GPU workload and perceived latency closer to native 30 fps behavior at the cost of visual smoothness.
- `send_actual_start_bitrate = true` sends the requested bitrate (from the latency preset) in the `RP-StartBitrate` header. Flip to `false` to fall back to zeroed headers while A/B testing packet-loss behavior.
  - **PS5 Quirk:** Current PS5 firmware ignores the `RP-StartBitrate` hint and immediately forces ~1.5 Mbps streams even when lower presets or LaunchSpec values are requested. Keep this flag enabled for telemetry and future firmware checks, but expect the console to override the requested rate.
- `clamp_soft_restart_bitrate = true` forces all Chiaki soft restarts to request ≤1.5 Mbps. Leave this enabled (or toggle “Clamp Soft Restart Bitrate” under Streaming Settings) to keep packet-loss fallbacks from spiking the Vita’s Wi-Fi when PS5 renegotiates.

## Known issues & troubleshooting
- Latency. On remote connections (not local WLAN), it's especially bad. ([Relevant GitHub issue](https://github.com/ywnico/vitaki-fork/issues/12))
- Vitaki may crash with error C2-12828-1 if incompatible plugins such as reRescaler are installed. Thanks to [@GuillermoAVeces](https://github.com/GuillermoAVeces) for identifying this. ([Relevant issue](https://github.com/ywnico/vitaki-fork/issues/1)).
- Typically only one stream works per launch. If the screen becomes gray and unresponsive, restart Vitaki. ([Relevant issue](https://github.com/ywnico/vitaki-fork/issues/16))
- In the past crashes occurred when multiple consoles are on the network, but this has likely been fixed. ([Relevant issue](https://github.com/ywnico/vitaki-fork/issues/6)).

If problems arise:
- Try restarting Vitaki first.
- Then, try deleting/renaming the config file (`ux0:data/vita-chiaki/chiaki.toml`). 
- If that doesn't help, create a new [issue](https://github.com/ywnico/vitaki-fork/issues) (or comment on an existing issue).

If two builds feel different, confirm what is actually running:
- `./tools/build.sh --env testing` builds a testing-profile VPK and writes logs to `ux0:data/vita-chiaki/vitarps5-testing.log`.
- `./tools/build.sh test` does not produce/install a VPK, so gameplay should not change from that command alone.
- Check these log markers in the active log file:
  - `Bitrate policy: preset_default (...)`
  - `Recovery profile: stable_default`
  - `Video gap profile: stable_default (hold_ms=24 force_span=12)`
See `docs/ai/STABILITY_AB_TESTING_GUIDE.md` for a full A/B procedure.

### Crash dump analysis
If Vita throws a `C2-12828-1` crash, you can pull the generated `.psp2dmp` from `ux0:data/` and decode it locally:

1. Initialize the parser submodule once:  
   ```bash
   git submodule update --init scripts/vita/parse_core
   ```
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
The script prints thread states, register dumps, and a file/line backtrace so you can pinpoint the fault without additional setup.

# Chiaki4deck

## [chiaki4deck](https://streetpea.github.io/chiaki4deck/)

An open source project looking to help users of the Steam Deck get the most out of Chiaki. [Click here to see the accompanying site for documentation, updates and more](https://streetpea.github.io/chiaki4deck/). 

**Disclaimer:** This project is not endorsed or certified by Sony Interactive Entertainment LLC.

Chiaki is a Free and Open Source Software Client for PlayStation 4 and PlayStation 5 Remote Play
for Linux, FreeBSD, OpenBSD, NetBSD, Android, macOS, Windows, Nintendo Switch and potentially even more platforms.

## Acknowledgements

This project has only been made possible because of the following Open Source projects:
[Rizin](https://rizin.re),
[Cutter](https://cutter.re),
[Frida](https://www.frida.re) and
[x64dbg](https://x64dbg.com).

Also thanks to [delroth](https://github.com/delroth) for analyzing the registration and wakeup protocol,
[grill2010](https://github.com/grill2010) for analyzing the PSN's OAuth Login,
as well as a huge thank you to [FioraAeterna](https://github.com/FioraAeterna) for giving me some
extremely helpful information about FEC and error correction.

## About

Originally created by Florian Märkl
Ported to PS Vita by AAGaming
Enhanced by ywnico
Augmented by solidEm with the help of [Claude Code](https://claude.com/claude-code)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License version 3
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.

Additional permission under GNU AGPL version 3 section 7

If you modify this program, or any covered work, by linking or
combining it with the OpenSSL project's OpenSSL library (or a
modified version of that library), containing parts covered by the
terms of the OpenSSL or SSLeay licenses, the Free Software Foundation
grants you additional permission to convey the resulting work.
Corresponding Source for a non-source form of such a combination
shall include the source code for the parts of OpenSSL used as well
as that of the covered work.
