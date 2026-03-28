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
