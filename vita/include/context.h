#pragma once
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <chiaki/discoveryservice.h>
#include <chiaki/log.h>
#include <chiaki/opusdecoder.h>

#include "config.h"
#include "discovery.h"
#include "host.h"
#include "message_log.h"
#include "controller.h"
#include "ui.h"
// #include "debugnet.h"

void vita_log_init_file(void);
void vita_log_append(const char *msg);

#define LOGD(fmt, ...) do {\
    uint64_t timestamp = sceKernelGetProcessTimeWide(); \
    char msg[800]; \
    sceClibSnprintf(msg, sizeof(msg), "[DEBUG] %ju "fmt"\n", timestamp __VA_OPT__(,) __VA_ARGS__); \
    sceClibPrintf("%s", msg); \
    vita_log_append(msg); \
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
    vita_log_append(msg); \
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
  bool is_streaming;
  bool inputs_ready;
  bool stop_requested;
  uint32_t negotiated_fps;          // max_fps requested from the console
  uint32_t target_fps;              // local clamp target (prep for pacer)
  uint32_t measured_incoming_fps;   // latest measured incoming fps window
  uint64_t fps_window_start_us;     // rolling one-second window start
  uint32_t fps_window_frame_count;  // frames counted within the window
  uint64_t pacing_accumulator;      // Bresenham-style pacing accumulator
  ChiakiOpusDecoder opus_decoder;
  ChiakiThread input_thread;
  float measured_bitrate_mbps;      // Last measured downstream bitrate
  uint32_t measured_rtt_ms;         // Last measured round-trip time (ms)
  uint64_t metrics_last_update_us;  // Timestamp for latest metrics sample
  uint64_t next_stream_allowed_us;  // Cooldown gate after quit
  uint32_t frame_loss_events;       // Count of frame loss events reported by Chiaki
  uint32_t total_frames_lost;       // Frames lost across the current session
  uint64_t loss_window_start_us;    // Sliding window start for adaptive mitigations
  uint32_t loss_window_event_count; // Events within the current sliding window
  uint64_t loss_alert_until_us;     // Overlay visibility deadline for loss warning
  uint64_t loss_alert_duration_us;  // Duration used to compute overlay fade
  uint32_t logged_loss_events;      // Last loss event count logged to console
  uint32_t auto_loss_downgrades;    // Number of auto latency downgrades this session
  uint32_t takion_drop_events;      // Queue overflow/corruption events seen from Takion
  uint32_t takion_drop_packets;     // Total packets dropped from Takion queue
  uint32_t logged_drop_events;      // Last drop count that was logged
  uint64_t takion_drop_last_us;     // Timestamp of last drop event (us)
  bool loss_retry_pending;          // Whether a lower bitrate retry is scheduled
  bool loss_retry_active;           // Apply fallback bitrate on next host_stream
  uint32_t loss_retry_attempts;     // Number of fallback retries used
  uint32_t loss_retry_bitrate_kbps; // Override bitrate for fallback sessions
  uint64_t loss_retry_ready_us;     // When the fallback retry is allowed to start
  bool reconnect_overlay_active;    // Show reconnecting overlay during fallback
  uint64_t reconnect_overlay_start_us;
  ChiakiControllerState cached_controller_state;
  bool cached_controller_valid;
  uint64_t last_input_packet_us;
  uint64_t last_input_stall_log_us;
  uint64_t inputs_blocked_since_us;
  bool inputs_resume_pending;
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
