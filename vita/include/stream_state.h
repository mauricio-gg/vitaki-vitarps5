#pragma once

#include <chiaki/session.h>
#include <chiaki/opusdecoder.h>
#include <chiaki/thread.h>

#include "controller.h"

/* Maximum length of a resolved PSN holepunch address string.
 * INET6_ADDRSTRLEN is 46 bytes; 64 gives comfortable headroom. */
#define PSN_SELECTED_ADDR_SIZE 64

/* Latency investigation (2026-08): fixed-capacity ring for per-frame end-to-end latency
 * samples within a single ~1s window. The window-close trigger (refresh_rtt in
 * host_metrics.c) is evaluated once per UI pass rather than on a hard 1000000us tick, so
 * real windows run ~1.02-1.06s at 60fps (60-64 samples) and further past that whenever the
 * UI loop hitches -- exactly when a late-window latency spike is most interesting. 128
 * gives headroom for a ~2s window at 60fps before the ring wraps. See vita/src/video.c
 * (writer, at vita2d_swap_buffers() — overwrites the OLDEST sample once full, never drops
 * the newest) and vita/src/host_metrics.c (reader/reducer, at its existing 1s window). No
 * heap allocation, no per-frame growth either way. */
#define LATENCY_WINDOW_SAMPLE_CAP 128

/* Code review fix: a 4-slot decode queue's entire interesting range is 0.00-4.00 frames,
 * and the leading hypothesis under test is a SUB-FRAME persistent depth shift (e.g.
 * 1.2 -> 1.9), which plain integer division cannot resolve -- both print "1" and the
 * signal this whole PR exists to find becomes invisible. decode_q_occ_avg/audio_q_occ_avg
 * below are therefore fixed-point, scaled by this factor (avg_x100=190 means 1.90 frames);
 * the corresponding *_max fields are genuinely discrete depth counts and stay plain
 * integers. Shared by vita/src/video.c (decode queue) and vita/src/audio.c (audio ring)
 * so both occupancy metrics use the same encoding. */
#define OCC_AVG_FIXED_POINT_SCALE 100u

typedef struct vita_chiaki_stream_t {
  ChiakiSession session;
  ChiakiControllerState controller_state;
  VitakiCtrlMapInfo vcmi;
  bool session_init;
  ChiakiMutex finalization_mutex;  // Protects session_init flag during finalize_session_resources()
  bool is_streaming;
  bool video_first_frame_logged;
  bool inputs_ready;
  bool stop_requested;
  bool stop_requested_by_user;
  bool teardown_in_progress;
  volatile bool session_finalize_pending;  // Set by session thread (event_cb), consumed by UI
                                           // thread for deferred join+fini
  uint32_t negotiated_fps;                 // max_fps requested from the console
  uint32_t target_fps;                     // local clamp target (prep for pacer)
  uint32_t measured_incoming_fps;          // latest measured incoming fps window
  uint64_t incoming_frame_last_us;  // GH #261: process-time stamp of the last incoming
                                    // frame sample, for PIPE/FPS staleness detection
  uint32_t session_generation;    // increments for each successfully initialized stream session
  uint32_t reconnect_generation;  // non-zero when this session is a reconnect/re-entry
  bool reset_reconnect_gen;       // next session should start as fresh (gen 0)
  uint32_t auto_reconnect_count;  // automatic recovery reconnects in current session (resets per
                                  // manual connect)
  uint32_t
      fps_under_target_windows;  // one-second windows where incoming fps is materially below target
  uint32_t post_reconnect_low_fps_windows;  // low-fps windows observed during post-reconnect grace
  uint64_t post_reconnect_window_until_us;  // deadline for post-reconnect low-fps tracking
  struct {
    bool recover_active;                // reconnect degraded-mode mitigation is currently active
    uint32_t recover_stage;             // staged recovery state machine (0=idle)
    uint64_t recover_last_action_us;    // timestamp of latest reconnect mitigation action
    uint32_t recover_idr_attempts;      // number of IDR requests used by reconnect mitigation
    uint32_t recover_restart_attempts;  // guarded restart attempts used by reconnect mitigation
    uint32_t
        recover_stable_windows;  // consecutive healthy windows observed while mitigation active
  } reconnect;
  uint64_t fps_window_start_us;     // rolling one-second window start
  uint32_t fps_window_frame_count;  // frames counted within the window
  uint64_t pacing_accumulator;      // Bresenham-style pacing accumulator
  ChiakiOpusDecoder opus_decoder;
  ChiakiThread input_thread;
  volatile bool input_thread_should_exit;  // Signal for clean thread exit (volatile prevents CPU
                                           // caching on ARM)
  float measured_bitrate_mbps;             // Last measured downstream bitrate
  uint32_t measured_rtt_ms;                // Last measured round-trip time (ms)
  uint64_t last_rtt_refresh_us;            // Timestamp of latest latency refresh
  uint64_t metrics_last_update_us;         // Timestamp for latest metrics sample
  uint64_t suspend_tick_last_us;           // GH #258: previous UI-loop tick timestamp; a gap beyond
                                           // SUSPEND_GAP_THRESHOLD_US means the app was suspended
  uint64_t suspend_resync_next_us;  // GH #258: deadline for the next scheduled post-resume resync
  uint32_t suspend_resync_shots_left;    // GH #258: remaining scheduled post-resume resync attempts
  bool suspend_fps_guard;                // GH #258: suppresses the first post-resume low-fps window
  uint64_t next_stream_allowed_us;       // Cooldown gate after quit
  bool post_stop_guard;                  // True only for the cooldown window following a deliberate
                                         // user stop; suppresses the "Streaming stopped" banner
                                         // since the console card already shows its own
                                         // "Console releasing session..." hint for that case.
  uint32_t retry_holdoff_ms;             // Active adaptive holdoff duration
  uint64_t retry_holdoff_until_us;       // Holdoff deadline after RP_IN_USE races
  bool retry_holdoff_active;             // Whether adaptive holdoff is currently armed
  uint32_t frame_loss_events;            // Count of frame loss events reported by Chiaki
  uint32_t total_frames_lost;            // Frames lost across the current session
  uint64_t loss_window_start_us;         // Sliding window start for adaptive mitigations
  uint32_t loss_window_event_count;      // Events within the current sliding window
  uint32_t loss_window_frame_accum;      // Frames dropped inside the active loss window
  uint32_t loss_burst_frame_accum;       // Frames dropped within the short-term burst bucket
  uint32_t loss_counter_saturated_mask;  // Bitmask of loss accumulators that already logged uint32
                                         // saturation
  uint64_t loss_burst_start_us;          // Timestamp when the current burst started
  uint32_t
      loss_recovery_gate_hits;  // Number of sustained-loss gates tripped in current recovery window
  uint64_t loss_recovery_window_start_us;  // Window start for staged loss recovery
  uint64_t
      last_loss_recovery_action_us;  // Timestamp of last restart/downgrade action from packet loss
  uint64_t stream_start_us;          // Timestamp when streaming connection became active
  uint64_t session_start_us;         // timestamp from chiaki_session_start() success; used for
                                     // TIME_TO_FIRST_FRAME
  uint32_t
      first_decode_frame_count;  // counts frames 1..30 for PIPE/DECODE logging; reset each session
  uint64_t loss_restart_soft_grace_until_us;  // Short startup grace used for early burst
                                              // suppression only
  uint64_t loss_restart_grace_until_us;       // During startup grace, suppress restart escalation
  uint64_t loss_alert_until_us;               // Overlay visibility deadline for loss warning
  uint64_t loss_alert_duration_us;            // Duration used to compute overlay fade
  uint64_t net_unstable_last_activated_us;    // debounce: minimum 500ms between PIPE/NET_UNSTABLE
                                              // activations
  uint32_t logged_loss_events;                // Last loss event count logged to console
  uint32_t auto_loss_downgrades;              // Number of auto latency downgrades this session
  uint32_t takion_drop_events;                // Queue overflow/corruption events seen from Takion
  uint32_t takion_drop_packets;               // Total packets dropped from Takion queue
  uint32_t logged_drop_events;                // Last drop count that was logged
  uint64_t takion_drop_last_us;               // Timestamp of last drop event (us)
  struct {
    uint32_t missing_ref_count;       // Missing reference-frame events from video receiver
    uint32_t corrupt_burst_count;     // Corrupt-frame requests sent to server
    uint32_t fec_fail_count;          // FEC recovery failures in frame processor
    uint32_t sendbuf_overflow_count;  // Takion control send-buffer overflows
    uint32_t logged_missing_ref_count;
    uint32_t logged_corrupt_burst_count;
    uint32_t logged_fec_fail_count;
    uint32_t logged_sendbuf_overflow_count;
    uint64_t last_log_us;
    uint32_t last_corrupt_start;
    uint32_t last_corrupt_end;
  } av_diag;
  uint32_t av_diag_stale_snapshot_streak;  // Consecutive update_latency_metrics() ticks that missed
                                           // diag mutex sampling
  uint64_t last_restart_failure_us;        // Cooldown gate for repeated restart failures
  uint32_t
      restart_handshake_failures;  // Count of soft-restart handshake failures in rolling window
  uint64_t
      last_restart_handshake_fail_us;  // Timestamp of latest handshake failure after soft restart
  uint64_t restart_cooloff_until_us;   // Cooloff deadline that suppresses new soft restarts
  char last_restart_source[32];        // Last recovery path that requested a soft restart
  uint32_t restart_source_attempts;    // Number of restart attempts from the current source in the
                                       // rolling window
  char disconnect_reason[128];
  uint64_t disconnect_banner_until_us;
  bool loss_retry_pending;           // Whether a lower bitrate retry is scheduled
  bool loss_retry_active;            // Apply fallback bitrate on next host_stream
  uint32_t loss_retry_attempts;      // Number of fallback retries used
  uint32_t loss_retry_bitrate_kbps;  // Override bitrate for fallback sessions
  uint64_t loss_retry_ready_us;      // When the fallback retry is allowed to start
  bool reconnect_overlay_active;     // Show reconnecting overlay during fallback
  uint64_t reconnect_overlay_start_us;
  bool fast_restart_active;  // Whether a soft reconnect is underway
  bool media_initialized;    // Whether audio/video pipeline is initialized
  ChiakiControllerState cached_controller_state;
  bool cached_controller_valid;
  uint64_t last_input_packet_us;
  uint64_t last_input_stall_log_us;
  uint64_t inputs_blocked_since_us;
  bool inputs_resume_pending;
  uint32_t unrecovered_frame_streak;
  uint32_t unrecovered_gate_events;
  uint64_t unrecovered_gate_window_start_us;
  uint32_t unrecovered_persistent_events;  // Rolling unrecovered-loss event count
  uint64_t unrecovered_persistent_window_start_us;
  uint32_t unrecovered_idr_requests;  // IDR attempts in rolling window
  uint64_t unrecovered_idr_window_start_us;
  bool restart_failure_active;
  bool rp_in_use_retry_pending;        // A single auto-retry after RP_IN_USE is armed and waiting
  uint64_t rp_in_use_retry_at_us;      // When the armed auto-retry is allowed to fire
  bool rp_in_use_retry_used;           // This session has already used its one auto-retry
  bool rp_in_use_retry_psn_holepunch;  // Snapshot of last_connect_used_psn_holepunch taken when the
                                       // RP_IN_USE auto-retry was armed; restored into
                                       // force_psn_holepunch right before the retry's
                                       // start_connection_thread() call so the user's
                                       // PSN-vs-LAN choice survives the retry.
  bool force_psn_holepunch;            // Set by UI when user selects Internet in the connect popup;
                                       // consumed and cleared at the top of host_stream().
  bool last_connect_used_psn_holepunch;  // Durable copy of the psn_remote decision computed in
                                         // host_stream(), taken BEFORE force_psn_holepunch is
                                         // cleared there. force_psn_holepunch itself is already
                                         // false by the time host_quit.c runs after a connect
                                         // attempt, so this is the only reliable source for
                                         // arming rp_in_use_retry_psn_holepunch above.
  char psn_selected_addr[PSN_SELECTED_ADDR_SIZE];  // Resolved PSN-path IP written by holepunch;
                                                   // consumed by host_stream

  // --- Diagnostic instrumentation (D1: Decode Time) ---
  volatile uint32_t
      decode_time_us;  // Latest single-frame decode time (Takion thread writes, UI reads)
  volatile uint32_t decode_avg_us;  // Window-averaged decode time (published each 1s window)
  volatile uint32_t decode_max_us;  // Window-max decode time (published each 1s window)
  uint32_t decode_window_total_us;  // Takion-thread-only accumulator
  uint32_t decode_window_max_us;    // Takion-thread-only max tracker
  uint32_t decode_window_count;     // Takion-thread-only frame count

  // --- Diagnostic instrumentation (D4: Windowed Bitrate) ---
  uint64_t bitrate_prev_bytes;             // Previous snapshot of total bytes for delta
  uint64_t bitrate_prev_frames;            // Previous snapshot of total frames for delta
  uint64_t bitrate_window_delta_bytes[3];  // 3-element ring buffer of byte deltas
  uint64_t bitrate_window_elapsed_us[3];   // elapsed µs for each ring-buffer window
  uint64_t bitrate_prev_update_us;         // process timestamp of the last ring-buffer push
  uint8_t bitrate_window_index;            // Current ring buffer write position
  uint8_t bitrate_window_filled;           // Number of valid entries in ring buffer
  volatile float windowed_bitrate_mbps;    // Rolling 3s bitrate (Takion writes, UI reads)

  // --- Diagnostic instrumentation (D5: Frame Overwrite / Freeze) ---
  volatile uint32_t frame_overwrite_count;  // Frames overwritten before display consumed them
  volatile uint32_t
      freeze_engaged_count;  // Corrupt frames suppressed (last-good presented in their place)

  // --- Diagnostic instrumentation (D6: Wi-Fi RSSI) ---
  volatile int32_t wifi_rssi;  // Latest Wi-Fi signal strength (-1 if unavailable)

  // --- Diagnostic instrumentation (D7: Display FPS) ---
  volatile uint32_t display_fps;         // Frames actually rendered to screen per second
  uint32_t display_frame_count;          // UI-thread-only counter within current window
  uint64_t display_fps_window_start_us;  // UI-thread-only window start timestamp
  uint64_t display_frame_last_us;  // GH #261: process-time stamp of the last displayed
                                   // frame, for PIPE/FPS staleness detection

  // --- Stuck bitrate detection ---
  bool stuck_bitrate_restart_used;        // Only allow one stuck-bitrate restart per session
  uint32_t stuck_bitrate_low_fps_streak;  // Consecutive 1s windows qualifying as stuck

  // --- Cascade alarm detection ---
  uint32_t cascade_prev_missing_ref_count;  // Previous snapshot for delta calc
  uint32_t cascade_alarm_streak;    // Consecutive 1s windows with cascade_delta >= 2 AND low FPS
  bool cascade_alarm_restart_used;  // Once per session
  uint64_t cascade_alarm_last_action_us;  // Cooldown timestamp

  // --- Latency investigation (true end-to-end frame latency, decode/audio occupancy) ---
  // latency_window_samples_ms: written by vita_video_render_latest_frame() (UI thread) at
  // vita2d_swap_buffers() time; read, reduced to p50/p95/max and reset by
  // host_metrics_update_latency() (also UI thread -- called immediately after render each
  // loop iteration, see vita/src/ui.c) inside its existing 1s refresh_rtt window. Same
  // thread on both ends, sequential within one loop pass -- no locking needed.
  uint32_t latency_window_samples_ms[LATENCY_WINDOW_SAMPLE_CAP];
  uint32_t latency_window_sample_count;  // Valid entries in the ring right now (<= CAP)
  uint32_t latency_window_write_idx;     // Next ring slot to write; wraps once count==CAP
  // Code review fix: this used to be session-cumulative (like decode_queue_drops), which
  // hid whether the window that just closed was itself truncated -- exactly the case that
  // matters when hunting a post-event latency spike. Now per-window: incremented in
  // video.c whenever the ring overwrites a still-valid sample, reset (after being
  // snapshotted into latency_dropped_n below) each window close in host_metrics.c.
  uint32_t latency_window_dropped_count;
  uint32_t latency_dropped_total_count;  // Session-cumulative twin, kept for long-run visibility
  uint32_t latency_p50_ms;  // Published once per ~1s window by host_metrics_update_latency()
  uint32_t latency_p95_ms;
  uint32_t latency_max_ms;
  uint32_t latency_sample_n;   // Sample count backing the above (0 = no data this window)
  uint32_t latency_dropped_n;  // latency_window_dropped_count snapshot for the window that
                               // just closed (0 = this window's ring never wrapped)

  // --- PIPE/INPUT: controller-poll-origin to feedback-sender-wire latency ---
  // Backing ring (input_latency_samples_us[]) lives in ChiakiFeedbackSender
  // (lib/include/chiaki/feedbacksender.h), not here: lib/ must not include
  // vita/-app headers, so host_metrics_update_latency() reaches directly into
  // stream_connection->feedback_sender under its state_mutex to reduce and
  // reset that ring, publishing only the reduced summary into these fields --
  // mirrors the latency_p50_ms/p95_ms/max_ms/sample_n/dropped_n cluster above.
  uint32_t input_latency_p50_us;
  uint32_t input_latency_p95_us;
  uint32_t input_latency_max_us;
  uint32_t input_latency_sample_n;
  uint32_t input_latency_dropped_n;
  // Code review fix: was a function-local `static` in host_metrics.c with no way for
  // host_metrics_reset_stream() to clear it across a session reset. See host_metrics.c.
  uint64_t latency_log_last_us;

  // --- PIPE/DISPLAY: decode-done to on-screen split (2026-08) ---
  // PIPE/LATENCY only accounts for ~27ms of the observed ~80ms first-packet-arrival ->
  // swap latency (avg_assemble_ms + decode_avg_ms + avg_submit_ms); ~50ms is unexplained
  // and lives entirely between "decode finished" and "pixels on screen". This splits that
  // gap into four spans, each with its own ring so a stage-specific stall (GPU wait,
  // memcpy, etc.) is visible instead of averaged away:
  //   pickup_us   -- decode-done (decode thread) to render entry (UI thread picks the
  //                  frame up on its next pass; the ONLY cross-thread span here).
  //   snapshot_us -- render entry to just after the corrupt/clean/cap-release branch
  //                  that may call promote_decoded_frame_to_last_good() (GH #245: an
  //                  O(1) frame_texture/last_good_texture pointer swap under `mtx`,
  //                  not the ~2MB memcpy this stage was originally named for -- kept
  //                  the same field/stage name so existing dashboards/consumers of
  //                  this ring don't need to change; expect this to read near-zero
  //                  post-fix, which is the proof the fix landed).
  //   draw_us     -- end of that branch to vita2d_end_drawing() (vita2d command
  //                  submission for this frame).
  //   swap_us     -- vita2d_end_drawing() to vita2d_swap_buffers() (GPU wait + swap).
  // All four are written by vita_video_render_latest_frame() (UI thread) and reduced/reset
  // by host_metrics_update_latency() (also UI thread, same call-once-per-loop-iteration
  // relationship as the latency_window_* cluster above) -- sequential on one thread, no
  // locking needed for the rings or reduced fields below. Same overwrite-oldest-on-wrap
  // ring semantics as latency_window_samples_ms above (see its comment): dropping the
  // newest sample would hide exactly the late-window stall this instrumentation exists to
  // catch.
  //
  // On the paced-drop path (should_drop_frame_for_pacing() == true) the frame is consumed
  // but never drawn or swapped, so only pickup_us has a real value -- recording a 0 for
  // snapshot/draw/swap there would corrupt their percentiles with fake zero-latency
  // samples. Paced-drop occurrences are counted separately in display_paced_count instead;
  // pickup_us IS still recorded for those frames (the pickup span is real and happens
  // regardless of pacing), so its ring naturally receives more samples per window than the
  // other three -- display_sample_n below (published by host_metrics.c) tracks the
  // synchronized snapshot/draw/swap count (all three are always written together on the
  // non-paced path only).
  uint32_t display_pickup_samples_us[LATENCY_WINDOW_SAMPLE_CAP];
  uint32_t display_pickup_sample_count;
  uint32_t display_pickup_write_idx;
  uint32_t display_pickup_dropped_count;
  uint32_t display_snapshot_samples_us[LATENCY_WINDOW_SAMPLE_CAP];
  uint32_t display_snapshot_sample_count;
  uint32_t display_snapshot_write_idx;
  uint32_t display_snapshot_dropped_count;
  uint32_t display_draw_samples_us[LATENCY_WINDOW_SAMPLE_CAP];
  uint32_t display_draw_sample_count;
  uint32_t display_draw_write_idx;
  uint32_t display_draw_dropped_count;
  uint32_t display_swap_samples_us[LATENCY_WINDOW_SAMPLE_CAP];
  uint32_t display_swap_sample_count;
  uint32_t display_swap_write_idx;
  uint32_t display_swap_dropped_count;
  // display_paced_count: number of paced-drop frames this window (pickup-only samples,
  // see above). Reset each window close alongside the rings.
  uint32_t display_paced_count;
  // Reduced p50/p95 outputs, published once per ~1s window by
  // host_metrics_update_latency(). No _max_us fields (unlike latency_max_ms above) --
  // four extra maxima would clutter the PIPE/DISPLAY line without adding much beyond
  // what p95 already shows; p50/p95 is sufficient to see whether a stage's typical case
  // or its tail is where the ~50ms is hiding.
  uint32_t display_pickup_p50_us;
  uint32_t display_pickup_p95_us;
  uint32_t display_snapshot_p50_us;
  uint32_t display_snapshot_p95_us;
  uint32_t display_draw_p50_us;
  uint32_t display_draw_p95_us;
  uint32_t display_swap_p50_us;
  uint32_t display_swap_p95_us;
  // display_sample_n: the synchronized snapshot/draw/swap sample count for the window that
  // just closed (all three rings always wrap together -- see comment above -- so any one
  // ring's count/dropped represents all three; pickup's true count is
  // display_sample_n + display_paced_n).
  uint32_t display_sample_n;
  uint32_t display_dropped_n;
  uint32_t display_paced_n;

  // Decode queue occupancy (vita/src/video.c). Time-weighted (area-under-the-depth-curve
  // divided by window duration), not a naive average of push/pop samples: see the
  // decode_q_occ_area_us comment in video.c for why a push/pop-only sample is structurally
  // biased (every such sample is >=1 by construction, and none fall during the multi-ms
  // sceAvcdecDecode window where a real backlog would actually sit). Decode thread and recv
  // thread write under decode_q_mtx; UI thread reads once per ~1s window.
  volatile uint32_t decode_q_occ_avg;  // Fixed-point x OCC_AVG_FIXED_POINT_SCALE (see above)
  volatile uint32_t decode_q_occ_max;  // Plain integer frame count (genuinely discrete)

  // Audio ring occupancy in frames (vita/src/audio.c): audio thread writes each ~1s
  // window, UI thread reads.
  volatile uint32_t audio_q_occ_avg;  // Fixed-point x OCC_AVG_FIXED_POINT_SCALE (see above)
  volatile uint32_t audio_q_occ_max;  // Plain integer frame count (genuinely discrete)

  // NET_UNSTABLE banner debounce -- mirrors the UNRECOVERED_FRAME_THRESHOLD pattern in
  // host_feedback.c. Accumulated frames_lost since the streak last reset (either by
  // firing the banner or by NET_UNSTABLE_BANNER_DECAY_US of quiet — see host_feedback.c).
  uint32_t net_unstable_banner_streak;
  uint64_t net_unstable_last_loss_us;  // Timestamp of the most recent loss event counted
                                       // into the streak above; drives the decay check.
} VitaChiakiStream;
