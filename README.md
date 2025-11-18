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
  <img src="screenshots/main.png" alt="Main Screen" width="45%"/>
  <img src="screenshots/Settings.png" alt="Settings Screen" width="45%"/>
  <img src="screenshots/Controller.png" alt="Controller Mapping" width="45%"/>
</div>

## Support This Project

This fork has been a labor of love for the PS Vita community, developed over 3+ months across several design iterations (we even had to start over at one point!). If you enjoy the UI enhancements and improvements, consider buying me a coffee to support continued development. More enhancements are coming!

☕ [Buy Me a Coffee](https://buymeacoffee.com/solidem)

## What's New in This Fork

This fork adds the following enhancements to ywnico's vitaki-fork:

1. **VitaRPS5-Style UI Redesign**
   - Modern card-based console selection interface
   - Professional PlayStation-themed color scheme
   - Redesigned PIN entry screen with individual digit display
   - Improved visual feedback and animations

2. **Enhanced Console Wake Flow**
   - "Waking up console..." screen with animated progress
   - 30-second timeout with visual countdown
   - Automatic streaming start when console wakes up
   - No need to press X twice

3. **Fixed Controller Input Issues**
   - Resolved race condition that prevented controller input during streaming
   - Properly separated UI and input thread buffer access
   - Controllers now work reliably during remote play

4. **Improved Console Management**
   - Re-pairing now properly deletes registration data from storage
   - Better console name and IP display formatting
   - Circle button for cancel (PlayStation convention)

5. **Packet-Loss Fallback & Overlay**
   - When Ultra Low still drops frames, the client now pauses briefly, displays a reconnecting overlay, and restarts streaming at an even lower bitrate instead of crashing back to the menu.
   - Automatic retries keep discovery paused and resume seamlessly once the link stabilizes.

## Features from ywnico's vitaki-fork

All the features from ywnico's fork are included:
1. Implemented audio
2. Implemented controls
    - Control mappings for L2, R2, L3, R3, and touchpad (trapezoid button), following the official ps4 remote play maps in `vs0:app/NPXS10013/keymap/`. Note that `Select` + `Start` sends the PS (home) button.
    - Motion controls (thanks to [@Epicpkmn11](https://github.com/Epicpkmn11), who also contributed some other controller improvements)
3. Implemented external network remote play (with manually-specified remote IP addresses)
4. Fixed console wakeup
5. Made debug logs visible, added tooltips on some buttons
6. Fixed instant disconnection bug
7. Disabled `vblank_wait` and set fps to 30 to reduce lag.
    - NOTE: the fps in the config file (`chiaki.toml`) will be ignored
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
- `stretch_video = false` keeps incoming frames centered with letterboxing. Set to `true` (or toggle “Fill Screen” under Streaming Settings) if you prefer the 360p/540p output stretched across the display.
- `force_30fps = false` disables the new 30 fps presentation clamp. Set to `true` (or toggle “Force 30 FPS Output” under Streaming Settings) to make the Vita drop frames locally whenever the PS5 insists on a 60 fps stream. This keeps GPU workload and perceived latency closer to native 30 fps behavior at the cost of visual smoothness.
- `send_start_bitrate = false` controls whether VitaRPS5 includes the encrypted `RP-StartBitrate` header inside the ctrl request. Flip it (or the matching “Send RP-StartBitrate Header” toggle under Streaming Settings) if you want to A/B test whether PS5 firmware honors the requested bitrate value on your network.
- `low_bandwidth_mode = false` toggles the 360p/≈1.5 Mbps preset. The “Low-Bandwidth Mode” toggle in Streaming Settings forces 360p + 30 FPS + Ultra Low latency mode and caps the requested bitrate below 2 Mbps so congested Wi-Fi links stay playable.

## Known issues & troubleshooting
- Latency. On remote connections (not local WLAN), it's especially bad. ([Relevant GitHub issue](https://github.com/ywnico/vitaki-fork/issues/12))
- Vitaki may crash with error C2-12828-1 if incompatible plugins such as reRescaler are installed. Thanks to [@GuillermoAVeces](https://github.com/GuillermoAVeces) for identifying this. ([Relevant issue](https://github.com/ywnico/vitaki-fork/issues/1)).
- Typically only one stream works per launch. If the screen becomes gray and unresponsive, restart Vitaki. ([Relevant issue](https://github.com/ywnico/vitaki-fork/issues/16))
- In the past crashes occurred when multiple consoles are on the network, but this has likely been fixed. ([Relevant issue](https://github.com/ywnico/vitaki-fork/issues/6)).

If problems arise:
- Try restarting Vitaki first.
- Then, try deleting/renaming the config file (`ux0:data/vita-chiaki/chiaki.toml`). 
- If that doesn't help, create a new [issue](https://github.com/ywnico/vitaki-fork/issues) (or comment on an existing issue).

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
