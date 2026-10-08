# Architecture

What the code on `main` does today. Paths are relative to the repo root. Detail on the streaming pipeline and recovery is in [streaming.md](streaming.md); PSN internet play is in [psn.md](psn.md).

## Repo layout

| Path | What it is |
|---|---|
| `vita/src/`, `vita/include/` | The Vita client app (C). UI modules are in `vita/src/ui/` and `vita/include/ui/`. |
| `lib/` | The Chiaki core (session, control channel, Takion transport, video and audio receivers, discovery, registration, PSN hole punching in `lib/src/remote/`). It is a fork of upstream Chiaki with local changes, so treat it as upstream unless a change is needed. |
| `third-party/` | Bundled dependencies (curl, jerasure, gf-complete, libvita2d, nanopb, and others). |
| `tools/` | `tools/build.sh` is the only supported way to build: it runs the VitaSDK toolchain in Docker. |
| `assets/` | Images, SVGs and `psn-ca-bundle.pem`. |

Layering rule: `vita/` calls into `lib/`, never the other way round. Where `vita/` needs a `lib/` value it cannot include, it keeps a hand-copied constant (for example the priorities logged in `vita/src/host.c:512-513`).

## Vita client modules (`vita/src/`)

**Entry and main loop**
- `main.c`: `main()` (L143) runs `vita_init()` (power clocks, network and SSL/AVCDEC modules), creates the context, optionally starts discovery, then calls `draw_ui()`.
- `ui.c`: `draw_ui()`, the UI-thread main loop: input, screen dispatch, drawing, and deferred session finalization (`ui.c:499-500`).
- `context.c`, `vita/include/context.h`: the one global `context` (config, hosts, stream state) shared by all threads.

**Session lifecycle (`host*.c`)**
- `host.c`: `host_stream()` (L213) builds and starts a session; `host_default_video_profile()`.
- `host_callbacks.c`: the callbacks lib calls: `host_event_cb()` (connected, quit, rumble) and `host_video_cb()`.
- `host_quit.c`: `host_handle_quit_event()`, which classifies a quit and decides on teardown or recovery.
- `host_lifecycle.c`: `host_shutdown_media_pipeline()`, `host_finalize_session_resources()`, `host_finalize_deferred_session()`.
- `host_recovery.c`: soft-restart requests and post-reconnect degraded-mode handling.
- `host_input.c`: the input thread (controller, touch, motion sampling) and its priority constant.
- `host_feedback.c`: on-screen hints and the loss/overflow/resync reactions.
- `host_metrics.c`: per-stream counters and latency/FPS windows.
- `host_loss_profile.c`: latency-mode bitrate targets and loss-detection thresholds.
- `host_disconnect.c`: quit-reason labels, which reasons allow retry, the disconnect banner.
- `host_registration.c`: `host_register()` (PIN registration) and `host_wakeup()`.
- `host_storage.c`: the host list: merging discovered and saved hosts, manual hosts, host copy helpers.

**Media**
- `video.c`: H.264 decode through `sceAvcdec`, with a queue feeding the VitaDecode thread, plus frame rendering.
- `audio.c`: `sceAudioOut` output, fed by `vita_audio_cb()`.
- `video_overlay.c`: in-stream overlays (indicators, exit hint, stats panel).

**Config and discovery**
- `config.c`, `config_values.c`, `config_hosts.c`, `config_migration.c`: the TOML settings file: parse, serialize, defaults, legacy migration, saved hosts.
- `discovery.c`, `discovery_host.c`: `start_discovery()`/`stop_discovery()` around lib's discovery service, and merging results into the host list.
- `controller.c`, `controller_metadata.c`: controller mapping presets and storage.

**PSN**
- `psn_auth.c`: device login and OAuth tokens. `token_crypto.c`: AES-256-GCM encryption of persisted tokens. `psn_remote.c`: remote host list and per-connect preparation. `vita_dns.c`: DNS resolution for curl and sockets.

**Other**
- `logging.c`: asynchronous file logger (a queue drained by its own thread). `message_log.c`: in-app message log. `debug_tools.c`: debug-only tools, compiled when `VITARPS5_DEBUG_TOOLS` is set. `util.c`: small helpers.

**UI modules (`vita/src/ui/`)**: `ui_screens.c` (full screens), `ui_components.c` (widgets and dialogs), `ui_console_cards.c` (console card grid), `ui_navigation.c` (wave sidebar), `ui_state.c` (connection overlay and the connection worker thread), `ui_input.c`, `ui_focus.c`, `ui_graphics.c` (drawing primitives), `ui_text.c` (font cache), `ui_animation.c` (background particles), `ui_controller_diagram.c`, `ui_qr.c`.

## Threads during a stream

Vita rule: a lower number is a higher priority. "Default" is the literal `0x10000100` that `chiaki_thread_create()` passes to `sceKernelCreateThread()` on Vita (`lib/src/thread.c:71-72`), so every lib thread starts there unless it lowers its own number.

| Thread | Created by | Priority / core |
|---|---|---|
| UI / main | the process; runs `main()` then `draw_ui()` | not set by the app; owns all vita2d drawing |
| VitaConnWorker | `ui_connection_start_thread()`, `vita/src/ui/ui_state.c:180-187`; runs `host_stream()` (L173) | `0x40` (64), 64 KiB stack |
| VitaLogThread | `vita_log_worker_init()`, `vita/src/logging.c:354` | `0x40` (64) |
| Session | `chiaki_session_start()`, `lib/src/session.c:322` | default |
| Ctrl | `chiaki_ctrl_start()` (called at `session.c:603`), `lib/src/ctrl.c:162` | default |
| Takion recv | `chiaki_takion_connect()`, `lib/src/takion.c:581`; one for the Senkusha probe (`senkusha.c:164`), one for the stream (`streamconnection.c:287`) | 64 (`TAKION_RECV_THREAD_PRIORITY`, `takion.c:64`, applied at `takion.c:1277`), core USER_0 |
| Takion send buffer | `chiaki_takion_send_buffer_init()`, `lib/src/takionsendbuffer.c:53`, called from the Takion thread (`takion.c:1345`) | default |
| GKCrypt key buffer | `chiaki_gkcrypt_init()`, `lib/src/gkcrypt.c:75`, only when a key buffer is configured | default |
| Congestion control | `chiaki_congestion_control_start()`, `lib/src/congestioncontrol.c:129`, called from `streamconnection.c:308` | default |
| Feedback sender | `chiaki_feedback_sender_init()`, `lib/src/feedbacksender.c:78` | 65 (`FEEDBACK_SENDER_THREAD_PRIORITY`, `feedbacksender.c:31`, applied at L413), no core pin |
| VitaDecode | `vita_h264_start()`, `vita/src/video.c:1447` | 64 (`VITA_DECODE_THREAD_PRIORITY`, `vita/include/video.h:39`, applied at `video.c:1330`), core USER_1 |
| Input | `host_stream()`, `vita/src/host.c:536`; body `host_input_thread_func()` | 96 (`VITA_INPUT_THREAD_PRIORITY`, `vita/include/host_input.h:11`, applied at `host_input.c:261`), affinity mask 0 |
| Discovery service | `chiaki_discovery_service_init()`, `lib/src/discoveryservice.c:75`, via `start_discovery()` | default; stopped while a stream runs (`host.c:456-460`) |

Notes:
- There is no audio thread. `vita_audio_cb()` (`vita/src/audio.c:204`) runs inline on whichever thread delivers the decoded audio. The call chain `takion_handle_packet_av()` (`takion.c:1828`) to `chiaki_audio_receiver_av_packet()` (`streamconnection.c:1280`) to the Opus decoder callback (`opusdecoder.c:95`) has no thread hand-off, so this is the Takion recv thread. On its first call the callback sets that calling thread to priority 64 (`VITA_AUDIO_THREAD_PRIORITY`, `vita/include/audio.h:5`) and core USER_2 (`audio.c:207-208`). The comments in `takion.c:1273-1276` and `video.c:1329` describe USER_2 as "audio" and USER_0 as "recv" as if they were separate threads; from the code they appear to be the same thread, re-pinned on the first audio frame. This is unverified on hardware.
- The log line `PIPE/PRIORITY` (`host.c:514`) prints decode, audio, recv, feedback and input priorities at each stream start.
- Registration (`regist.c:69`) and PSN hole punching (`lib/src/remote/holepunch.c:990`, `rudpsendbuffer.c:57`) start their own lib threads only on those paths; see [psn.md](psn.md).

## Session flow

1. **Launch.** `main()` initialises the system, builds `context` (`vita_chiaki_init_context()` parses the config, `context.c:27-28`), starts discovery if `config.auto_discovery` is set, and enters `draw_ui()`.
2. **Hosts appear.** Discovery results arrive through `discovery_cb()` (`discovery.c:277`) and are merged into `context.hosts` (`save_discovered_host()`, `discovery.c:116`). Saved and manual hosts come from config (`host_storage.c`). A console that is not yet registered is registered with `host_register()` (`host_registration.c:83`), which calls `chiaki_regist_start()`. PSN remote hosts come from `psn_remote_refresh_hosts()` (called at `ui.c:475`).
3. **User picks a console.** The UI thread starts the connect through `start_connection_thread()` (`ui_state.c:339`, a wrapper for `ui_connection_start_thread()`), which creates the VitaConnWorker thread; that thread calls `host_stream(host)` (`ui_state.c:173`). The UI thread keeps drawing the connect overlay.
4. **`host_stream()`** (`host.c:213`): chooses LAN or PSN (`host.c:218`), stops discovery (L456-460), marks `session_init` under `finalization_mutex` (L463-466), builds the connect profile, calls `chiaki_session_init()` (L429), wires the Opus audio sink (L482-486), the event callback (`host_event_cb`, L489) and the video callback (`host_video_cb`), starts the decoder with `vita_h264_start()`, then `chiaki_session_start()` (L529) and finally creates the input thread (L536).
5. **Session thread** (`session_thread_func`, `lib/src/session.c:516`): starts the ctrl connection (`chiaki_ctrl_start`, L603), waits for it, runs Senkusha, which measures MTU and RTT (L702-708), then `chiaki_stream_connection_run()` (L761), which opens the Takion connection and starts congestion control and the feedback sender. This call blocks for the life of the stream.
6. **Events reach the Vita.** lib calls `host_event_cb()` (`host_callbacks.c:17`) on the session thread. `CHIAKI_EVENT_CONNECTED` resets the stream timers and flags. Video frames arrive on the Takion thread through `host_video_cb()`, which hands them to `vita_h264_decode_frame()` (`host_callbacks.c:146`), the producer side of the queue read by the VitaDecode thread; the UI thread draws the latest decoded frame with `vita_video_render_latest_frame()` (`ui.c:777`).
7. **Quit.** `CHIAKI_EVENT_QUIT` calls `host_handle_quit_event()` (`host_quit.c:42`) on the session thread. It snapshots state, decides whether to schedule recovery, then runs `host_shutdown_media_pipeline()` (`host_quit.c:125`), clears `session_init` and sets `session_finalize_pending` (L139-143).
8. **Teardown.** The session thread cannot join itself, so the UI thread does the join: `host_finalize_deferred_session()` (`ui.c:499-500`, `host_lifecycle.c`) joins the session thread and the input thread, then calls `chiaki_session_fini()`. If recovery was scheduled, the UI thread then starts the fallback connect (`ui.c:548-561`). Details are in [streaming.md](streaming.md).

### Media pipeline shutdown order

`host_shutdown_media_pipeline()` (`host_lifecycle.c:9`) is order-sensitive:
1. `is_streaming = false`, then `vita_h264_stop()` (joins VitaDecode) and a 2 ms delay, so the UI thread stops drawing the frame texture before it is freed;
2. `chiaki_opus_decoder_fini()`;
3. `vita_h264_cleanup()`;
4. `vita_audio_cleanup()`.
