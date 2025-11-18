## PROGRESS · Roadmap & Epics

A living reference for larger initiatives. Update this document when we start or finish an epic, or when priorities shift.

---

### 1. Latency Reduction Initiative *(Active)*
- **Objective:** Bring Vita remote-play latency below 50 ms local / 100 ms remote by improving PS5 negotiation, Vita scheduling, and Wi-Fi guidance.
- **Current Focus:** Instrumentation + bitrate handshake research (`docs/LATENCY_ANALYSIS.md`).
- **Upcoming Milestones:**
  1. Capture requested vs. actual bitrate and timing metrics on hardware.
  2. Patch `RP-StartBitrate`/LaunchSpec to request sub-3 Mbps streams reliably.
  3. Surface user-facing controls/documentation for low-bandwidth mode.

### 2. Modern UI Phase 3 *(In Progress per `docs/ai/ROADMAP.md`)*
- **Objective:** Wire the VitaRPS5 UI modules to real backend data (settings, controller maps, registration flow).
- **Key Milestones:**
  1. Integrate modular settings backend (replace legacy `VitaChiakiConfig` usage where needed).
  2. Implement wave navigation + dashboard interactions end-to-end.
  3. Complete tabbed settings, pairing flows, and profile panels with real data.

### 3. Build & Release Reliability *(Ongoing)*
- **Objective:** Keep Docker-based builds, automated releases, and deployment scripts stable.
- **Focus Areas:** Monitor GitHub Actions release workflow, ensure `./tools/build.sh` handles new assets/configs, keep documentation current.

---

### Status Log
| Date | Update | Owner |
|------|--------|-------|
| 2025-11-10 | Created roadmap structure (Latency, UI Phase 3, Build reliability) | Codex agent |
| 2025-11-10 | Added runtime latency/bitrate instrumentation groundwork (host/UI metrics logging) | Codex agent |
| 2025-11-10 | Implemented configurable latency presets (1.2–3.8 Mbps) plus settings UI | Codex agent |

Add entries whenever milestones move forward or new initiatives start.
