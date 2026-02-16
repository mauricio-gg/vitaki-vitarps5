#pragma once
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <chiaki/discoveryservice.h>
#include <chiaki/log.h>

#include "config.h"
#include "discovery.h"
#include "host.h"
#include "logging.h"
#include "message_log.h"
#include "stream_state.h"
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

typedef struct vita_chiaki_context_t {
  ChiakiLog log;
  ChiakiDiscoveryService discovery;
  bool discovery_enabled;
  bool discovery_resume_after_stream;
  VitaChiakiDiscoveryCallbackState* discovery_cb_state;
  VitaChiakiHost* hosts[MAX_CONTEXT_HOSTS];
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
