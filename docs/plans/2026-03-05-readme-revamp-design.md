# README Revamp Design

**Date:** 2026-03-05
**Goal:** Rewrite README.md to position VitaRPS5 as a standalone project, removing all fork language and streamlining for end-users.

---

## Design Decisions

| Decision | Choice |
|----------|--------|
| Audience | End-user focused (PS Vita players) |
| Attribution | Brief acknowledgements section at the bottom |
| Build docs | Move to `docs/BUILDING.md`, link from README |
| Config docs | Move to `docs/CONFIGURATION.md`, link from README |
| Pitch | "PS5 Remote Play on PS Vita" |
| Support/Donate | Keep prominent, right after screenshots |
| Structure | "App Store" style — clean, scannable, user-first |

---

## README Structure

### 1. Hero
- Logo + "VitaRPS5" title (no "(Vitaki Fork)")
- Tagline: "PS5 Remote Play on PS Vita"
- Badges updated to `mauricio-gg/vitaki-vitarps5` repo URLs

### 2. Screenshots
- Keep existing 4-screenshot centered layout as-is

### 3. Support This Project
- Prominent placement right after screenshots
- Shorter copy: "VitaRPS5 has been a labor of love for the PS Vita community. If you enjoy it, consider supporting continued development."
- Buy Me a Coffee link

### 4. Features (unified)
Single clean list, no "fork" vs "inherited" split:
- Modern UI (card-based console selection, wave nav, tabbed settings, PS-themed)
- Low-Latency Streaming (tuned thread priorities, 500Hz input, optimized buffers)
- Controller Customization (3 custom presets, interactive diagram, front/back views)
- Smart Connection Flow (auto-discovery, animated wake-up, auto streaming start)
- Packet-Loss Recovery (reconnecting overlay, auto bitrate adjust, seamless retry)
- Flexible Video Settings (latency presets, fill/letterbox toggle, 30/60 FPS)
- Full Control Support (all buttons including L2/R2/L3/R3, touchpad, motion)

### 5. Installation
- Download VPK from Releases link
- Transfer to Vita
- Install with VitaShell

### 6. Getting Started
- Pairing instructions (connect WiFi, same PSN, open app, enter code)
- Tip: "If your PSN ID isn't detected, go to the Profile page, select the Profile card, and press X to refresh your Account ID."
- Note: Remote play over internet not yet supported, local Wi-Fi only, internet support planned for future release

### 7. Controls (quick reference)
- L + R + Start (hold ~1s) to stop streaming
- Select + Start for PS button
- Toggle exit hint and latency stats in Settings

### 8. Troubleshooting (condensed)
- Crash C2-12828-1: incompatible plugins
- Config issues: delete chiaki.toml to reset
- Logs: testing builds write to vitarps5-testing.log
- Link to GitHub issues for our repo

### 9. Documentation links
- Configuration Guide -> docs/CONFIGURATION.md
- Building from Source -> docs/BUILDING.md
- Crash Dump Analysis -> docs/CRASH_ANALYSIS.md
- Wi-Fi Optimization -> docs/WIFI_OPTIMIZATION.md

### 10. Acknowledgements
- Brief credit to Chiaki, AAGaming, ywnico, Epicpkmn11
- Thanks to reverse engineering tools (Rizin, Cutter, Frida, x64dbg)

### 11. License
- AGPL-3.0 one-liner with link to LICENSE file
- Created by solidEm with Claude Code credit
- No full AGPL boilerplate text

---

## Content Removed from README

| Content | Disposition |
|---------|-------------|
| "(Vitaki Fork)" in title | Deleted |
| Fork description paragraph | Deleted |
| "What's New in This Fork" section | Merged into unified Features |
| "Features from ywnico's vitaki-fork" section | Merged into unified Features |
| Legacy CMake build instructions | Moved to docs/BUILDING.md |
| Chiaki4deck section | Deleted (irrelevant to VitaRPS5 users) |
| Full AGPL boilerplate text | Replaced with one-liner + link |
| A/B testing log markers | Moved to docs/BUILDING.md |
| Crash dump analysis | Moved to docs/CRASH_ANALYSIS.md |
| Detailed config settings | Moved to docs/CONFIGURATION.md |
| "Gray/unresponsive screen" troubleshooting | Deleted (old fork bug, not applicable) |
| Remote connection section | Replaced with "not yet supported" note |

## New Files Created

| File | Content |
|------|---------|
| `docs/BUILDING.md` | Docker build system, debug builds, legacy CMake, deployment, release workflow, A/B testing |
| `docs/CONFIGURATION.md` | All chiaki.toml options with descriptions |
| `docs/CRASH_ANALYSIS.md` | psp2dmp submodule setup, parse_core usage |

---

## Badge URL Updates

All badges change from `mauricio-gg/vitaki-fork` to `mauricio-gg/vitaki-vitarps5`.
