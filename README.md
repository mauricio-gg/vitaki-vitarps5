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

<a href="https://www.buymeacoffee.com/solidem"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" height="50"></a>

## Features

- **Modern UI** — Card-based console selection, wave navigation sidebar, tabbed settings, PlayStation-themed design
- **Low-Latency Streaming** — Tuned thread priorities, 500Hz input polling, optimized network buffers
- **Controller Customization** — 3 custom preset slots, fullscreen interactive controller diagram with front and back views
- **Smart Connection Flow** — Auto-discovery, animated wake-up with progress countdown, automatic streaming start
- **PSN Internet Remote Play** — Sign in with PSN to stream from outside your home network via UPnP NAT traversal (works on most home networks; falls back gracefully on restrictive ones)
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

### Internet Remote Play (PSN)

VitaRPS5 3.0.0 adds remote play over the internet. The Vita signs in to your PSN account, then uses UPnP to punch through your router so the stream can reach your PS5 from outside your home network.

**One-time PSN sign-in:**

1. In **Settings**, enable **PSN Internet Mode**
2. Open the **Profile** tab and press **X** on the Connection card
3. The Vita shows a QR code and an authorize URL beginning with `https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/authorize`
4. Scan the QR (or type the URL) on your phone or PC, then sign in to PSN
5. After signing in, your browser will land on a page beginning with `https://remoteplay.dl.playstation.net/remoteplay/redirect` — **copy that full URL from the address bar** (it contains a `code=...` parameter)
6. Back on the Vita, press **X** again on the Connection card and paste the redirect URL into the text input — just the `code=...` value also works
7. You should see **"PSN login complete"** — your PSN-registered consoles will now appear with a small radio-wave icon next to the status dot

**Choosing LAN vs Internet:**

Consoles reachable both on your local network and via PSN show a small **blue radio-wave icon** on their card. On those cards:

- **Tap X (or touch the card)** — connect over your local Wi-Fi
- **Hold X for ~0.6 s** — open a "Connect via" picker with **Local Network** and **Internet** options

Cards without the radio-wave icon connect immediately on press (LAN-only or Internet-only, depending on what was discovered).

**Caveat:** Internet remote play depends on UPnP being enabled on your router and your ISP not using carrier-grade NAT. It works on most home networks but not all. If the stream fails to start, your network may be blocking the hole-punch — try LAN instead, or open a [GitHub issue](https://github.com/mauricio-gg/vitaki-vitarps5/issues) with your router model so we can track common failure cases.

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
- [PSN OAuth Credentials](docs/ai/PSN_OAUTH_CREDENTIALS.md) — Why the OAuth client ID/secret are committed (public PS-App values, not user secrets)

## Acknowledgements

VitaRPS5 is built on the [Chiaki](https://git.sr.ht/~thestr4ng3r/chiaki) open-source Remote Play stack. PS Vita support was pioneered by [AAGaming](https://github.com/AAGaming00) and significantly enhanced by [ywnico](https://github.com/ywnico). Special thanks to [Epicpkmn11](https://github.com/Epicpkmn11) for motion control contributions.

Additional thanks to the open-source tools that made reverse engineering possible: [Rizin](https://rizin.re), [Cutter](https://cutter.re), [Frida](https://www.frida.re), and [x64dbg](https://x64dbg.com). Thanks also to [delroth](https://github.com/delroth) for registration/wakeup protocol analysis, [grill2010](https://github.com/grill2010) for PSN OAuth analysis, and [FioraAeterna](https://github.com/FioraAeterna) for FEC/error correction insights.

## License

[AGPL-3.0](LICENSE)

Originally created by Florian Märkl. Ported to PS Vita by AAGaming. Enhanced by ywnico. Developed by [solidEm](https://github.com/mauricio-gg) with the help of [Claude Code](https://claude.com/claude-code).
