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
  int fps;
  ChiakiOpusDecoder opus_decoder;
  ChiakiThread input_thread;
  float measured_bitrate_mbps;      // Last measured downstream bitrate
  uint32_t measured_rtt_ms;         // Last measured round-trip time (ms)
  uint64_t metrics_last_update_us;  // Timestamp for latest metrics sample
  uint64_t next_stream_allowed_us;  // Cooldown gate after quit
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
