#pragma once
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <chiaki/discoveryservice.h>
#include <chiaki/log.h>
#include <chiaki/opusdecoder.h>
#include <chiaki/thread.h>

#include "config.h"
#include "discovery.h"
#include "host.h"
#include "logging.h"
#include "message_log.h"
#include "controller.h"
#include "ui.h"
// #include "debugnet.h"

#define LOGD(fmt, ...) do {\
    uint64_t timestamp = sceKernelGetProcessTimeWide(); \
    char msg[800]; \
    sceClibSnprintf(msg, sizeof(msg), "[DEBUG] %ju "fmt"\n", timestamp __VA_OPT__(,) __VA_ARGS__); \
    sceClibPrintf("%s", msg); \
    vita_log_submit_line(CHIAKI_LOG_DEBUG, msg); \
    if (!context.stream.is_streaming) { \
        if (context.mlog) { \
          write_message_log(context.mlog, msg); \
        } \
      } \
  } while (0)
  //debugNetPrintf(DEBUG, "%ju "fmt"\n", timestamp __VA_OPT__(,) __VA_ARGS__);
#define LOGE(fmt, ...) do {\
    uint64_t timestamp = sceKernelGetProcessTimeWide(); \
    char msg[800]; \
    sceClibSnprintf(msg, sizeof(msg), "[ERROR] %ju "fmt"\n", timestamp __VA_OPT__(,) __VA_ARGS__); \
    sceClibPrintf("%s", msg); \
    vita_log_submit_line(CHIAKI_LOG_ERROR, msg); \
    if (!context.stream.is_streaming) { \
        if (context.mlog) { \
          write_message_log(context.mlog, msg); \
        } \
      } \
  } while (0)
  //debugNetPrintf(ERROR, "%ju "fmt"\n", timestamp __VA_OPT__(,) __VA_ARGS__);

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
  uint32_t negotiated_fps;          // max_fps requested from the console
  uint32_t target_fps;              // local clamp target (prep for pacer)
  uint32_t measured_incoming_fps;   // latest measured incoming fps window
  uint32_t session_generation;      // increments for each successfully initialized stream session
  uint32_t reconnect_generation;    // non-zero when this session is a reconnect/re-entry
  uint32_t fps_under_target_windows; // one-second windows where incoming fps is materially below target
  uint32_t post_reconnect_low_fps_windows; // low-fps windows observed during post-reconnect grace
  uint64_t post_reconnect_window_until_us; // deadline for post-reconnect low-fps tracking
  struct {
    bool recover_active;    // reconnect degraded-mode mitigation is currently active
    uint32_t recover_stage; // staged recovery state machine (0=idle)
    uint64_t recover_last_action_us; // timestamp of latest reconnect mitigation action
    uint32_t recover_idr_attempts; // number of IDR requests used by reconnect mitigation
    uint32_t recover_restart_attempts; // guarded restart attempts used by reconnect mitigation
    uint32_t recover_stable_windows; // consecutive healthy windows observed while mitigation active
  } reconnect;
  uint64_t fps_window_start_us;     // rolling one-second window start
  uint32_t fps_window_frame_count;  // frames counted within the window
  uint64_t pacing_accumulator;      // Bresenham-style pacing accumulator
  ChiakiOpusDecoder opus_decoder;
  ChiakiThread input_thread;
  volatile bool input_thread_should_exit;    // Signal for clean thread exit (volatile prevents CPU caching on ARM)
  float measured_bitrate_mbps;      // Last measured downstream bitrate
  uint32_t measured_rtt_ms;         // Last measured round-trip time (ms)
  uint64_t last_rtt_refresh_us;     // Timestamp of latest latency refresh
  uint64_t metrics_last_update_us;  // Timestamp for latest metrics sample
  uint64_t next_stream_allowed_us;  // Cooldown gate after quit
  uint32_t retry_holdoff_ms;        // Active adaptive holdoff duration
  uint64_t retry_holdoff_until_us;  // Holdoff deadline after RP_IN_USE races
  bool retry_holdoff_active;        // Whether adaptive holdoff is currently armed
  uint32_t frame_loss_events;       // Count of frame loss events reported by Chiaki
  uint32_t total_frames_lost;       // Frames lost across the current session
  uint64_t loss_window_start_us;    // Sliding window start for adaptive mitigations
  uint32_t loss_window_event_count; // Events within the current sliding window
  uint32_t loss_window_frame_accum; // Frames dropped inside the active loss window
  uint32_t loss_burst_frame_accum;  // Frames dropped within the short-term burst bucket
  uint32_t loss_counter_saturated_mask; // Bitmask of loss accumulators that already logged uint32 saturation
  uint64_t loss_burst_start_us;     // Timestamp when the current burst started
  uint32_t loss_recovery_gate_hits; // Number of sustained-loss gates tripped in current recovery window
  uint64_t loss_recovery_window_start_us; // Window start for staged loss recovery
  uint64_t last_loss_recovery_action_us; // Timestamp of last restart/downgrade action from packet loss
  uint64_t stream_start_us;         // Timestamp when streaming connection became active
  uint64_t startup_warmup_until_us; // Startup warmup deadline where we absorb burst pressure
  uint32_t startup_warmup_overflow_events; // Takion overflow events seen during startup warmup
  bool startup_warmup_drain_performed; // One-shot reorder queue drain + IDR request during warmup
  uint64_t startup_bootstrap_until_us; // Deterministic startup bootstrap deadline (decode-only period)
  bool startup_bootstrap_active; // Hold presentation until startup bootstrap converges
  bool startup_bootstrap_idr_requested; // Whether startup bootstrap already requested an IDR
  uint32_t startup_bootstrap_clean_frames; // Decoded frames observed during startup bootstrap
  uint32_t startup_bootstrap_required_clean_frames; // Clean-frame threshold before presenting
  uint64_t startup_bootstrap_last_flush_us; // Last startup bootstrap reorder queue flush timestamp
  uint64_t loss_restart_soft_grace_until_us; // Short startup grace used for early burst suppression only
  uint64_t loss_restart_grace_until_us; // During startup grace, suppress restart escalation
  uint64_t loss_alert_until_us;     // Overlay visibility deadline for loss warning
  uint64_t loss_alert_duration_us;  // Duration used to compute overlay fade
  uint32_t logged_loss_events;      // Last loss event count logged to console
  uint32_t auto_loss_downgrades;    // Number of auto latency downgrades this session
  uint32_t takion_drop_events;      // Queue overflow/corruption events seen from Takion
  uint32_t takion_drop_packets;     // Total packets dropped from Takion queue
  uint32_t logged_drop_events;      // Last drop count that was logged
  uint64_t takion_drop_last_us;     // Timestamp of last drop event (us)
  uint64_t last_takion_overflow_restart_us; // Rate-limit restarts on queue overflow
  uint32_t takion_overflow_soft_attempts;   // Soft mitigation attempts in current window
  uint64_t takion_overflow_window_start_us; // Window tracking for overflow attempts
  uint64_t takion_overflow_backoff_until_us;// Cooldown before next overflow mitigation
  bool takion_cooldown_overlay_active;      // Block UI taps while Takion cools down
  uint64_t takion_overflow_drop_window_start_us; // Short window for ignoring transient drops
  uint32_t takion_overflow_recent_drops;    // Drop counter within ignore window
  uint64_t takion_startup_grace_last_resync_us; // Rate-limit decoder resync requests during startup grace
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
  uint32_t av_diag_stale_snapshot_streak; // Consecutive update_latency_metrics() ticks that missed diag mutex sampling
  uint64_t last_restart_failure_us; // Cooldown gate for repeated restart failures
  uint32_t restart_handshake_failures; // Count of soft-restart handshake failures in rolling window
  uint64_t last_restart_handshake_fail_us; // Timestamp of latest handshake failure after soft restart
  uint64_t restart_cooloff_until_us; // Cooloff deadline that suppresses new soft restarts
  char last_restart_source[32]; // Last recovery path that requested a soft restart
  uint32_t restart_source_attempts; // Number of restart attempts from the current source in the rolling window
  char disconnect_reason[128];
  uint64_t disconnect_banner_until_us;
  bool loss_retry_pending;          // Whether a lower bitrate retry is scheduled
  bool loss_retry_active;           // Apply fallback bitrate on next host_stream
  uint32_t loss_retry_attempts;     // Number of fallback retries used
  uint32_t loss_retry_bitrate_kbps; // Override bitrate for fallback sessions
  uint64_t loss_retry_ready_us;     // When the fallback retry is allowed to start
  bool reconnect_overlay_active;    // Show reconnecting overlay during fallback
  uint64_t reconnect_overlay_start_us;
  bool fast_restart_active;         // Whether a soft reconnect is underway
  bool media_initialized;           // Whether audio/video pipeline is initialized
  ChiakiControllerState cached_controller_state;
  bool cached_controller_valid;
  uint64_t last_input_packet_us;
  uint64_t last_input_stall_log_us;
  uint64_t inputs_blocked_since_us;
  bool inputs_resume_pending;
  uint32_t unrecovered_frame_streak;
  uint32_t unrecovered_gate_events;
  uint64_t unrecovered_gate_window_start_us;
  uint32_t unrecovered_persistent_events;      // Rolling unrecovered-loss event count
  uint64_t unrecovered_persistent_window_start_us;
  uint32_t unrecovered_idr_requests;           // IDR attempts in rolling window
  uint64_t unrecovered_idr_window_start_us;
  bool restart_failure_active;
} VitaChiakiStream;

typedef struct vita_chiaki_context_t {
  ChiakiLog log;
  ChiakiDiscoveryService discovery;
  bool discovery_enabled;
  bool discovery_resume_after_stream;
  VitaChiakiDiscoveryCallbackState* discovery_cb_state;
  VitaChiakiHost* hosts[MAX_NUM_HOSTS];
  VitaChiakiHost* active_host;
  VitaChiakiStream stream;
  VitaChiakiConfig config;
  VitaChiakiUIState ui_state;
  uint8_t num_hosts;
  VitaChiakiMessageLog* mlog;
} VitaChiakiContext;

/// Global context singleton
extern VitaChiakiContext context;

bool vita_chiaki_init_context();
