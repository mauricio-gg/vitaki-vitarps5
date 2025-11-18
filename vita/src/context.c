#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
// #include <debugnet.h>

#include "stdlib.h"
#include <string.h>

#include "message_log.h"
#include "context.h"
#include "config.h"
#include "ui.h"

VitaChiakiContext context;

#define VITARPS5_LOG_PATH "ux0:data/vita-chiaki/vitarps5.log"
#define VITA_LOG_QUEUE_CAP 64

typedef struct {
  char *data;
  size_t len;
} VitaLogMessage;

static SceUID log_file_fd = -1;
static bool log_file_failed = false;
static SceUID log_thread_id = -1;
static SceKernelLwMutexWork log_mutex;
static SceKernelLwCondWork log_cond;
static bool log_worker_initialized = false;
static bool log_thread_should_exit = false;
static VitaLogMessage log_queue[VITA_LOG_QUEUE_CAP];
static size_t log_queue_head = 0;
static size_t log_queue_tail = 0;

static void vita_log_try_open(void) {
  if (log_file_fd >= 0 || log_file_failed)
    return;

  sceIoMkdir("ux0:data", 0777);
  sceIoMkdir("ux0:data/vita-chiaki", 0777);
  log_file_fd = sceIoOpen(VITARPS5_LOG_PATH,
                          SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND,
                          0666);
  if (log_file_fd >= 0) {
    char header[96];
    uint64_t timestamp = sceKernelGetProcessTimeWide();
    int len = sceClibSnprintf(header, sizeof(header),
                              "\n----- VitaRPS5 log start %ju -----\n",
                              timestamp);
    if (len > 0)
      sceIoWrite(log_file_fd, header, len);
  } else {
    log_file_failed = true;
    sceClibPrintf("[LOG] Failed to open %s (0x%x)\n",
                  VITARPS5_LOG_PATH, log_file_fd);
  }
}

static bool vita_log_queue_is_empty(void) {
  return log_queue_head == log_queue_tail;
}

static void vita_log_queue_push_locked(char *data, size_t len) {
  size_t next_tail = (log_queue_tail + 1) % VITA_LOG_QUEUE_CAP;
  if (next_tail == log_queue_head) {
    VitaLogMessage drop = log_queue[log_queue_head];
    if (drop.data)
      free(drop.data);
    log_queue_head = (log_queue_head + 1) % VITA_LOG_QUEUE_CAP;
  }
  log_queue[log_queue_tail].data = data;
  log_queue[log_queue_tail].len = len;
  log_queue_tail = next_tail;
}

static bool vita_log_queue_pop_locked(VitaLogMessage *out_msg) {
  if (vita_log_queue_is_empty())
    return false;
  *out_msg = log_queue[log_queue_head];
  log_queue_head = (log_queue_head + 1) % VITA_LOG_QUEUE_CAP;
  return true;
}

static int vita_log_thread_func(SceSize args, void *argp) {
  (void)args;
  (void)argp;

  while (true) {
    sceKernelLockLwMutex(&log_mutex, 1, NULL);
    while (!log_thread_should_exit && vita_log_queue_is_empty())
      sceKernelWaitLwCond(&log_cond, NULL);

    bool should_exit = log_thread_should_exit && vita_log_queue_is_empty();
    VitaLogMessage msg = {};
    if (!should_exit)
      vita_log_queue_pop_locked(&msg);
    sceKernelUnlockLwMutex(&log_mutex, 1);

    if (should_exit)
      break;

    if (msg.data && msg.len > 0) {
      vita_log_try_open();
      if (log_file_fd >= 0)
        sceIoWrite(log_file_fd, msg.data, msg.len);
      free(msg.data);
    }
  }

  return 0;
}

static void vita_log_worker_init(void) {
  if (log_worker_initialized)
    return;

  int res = sceKernelCreateLwMutex(&log_mutex, "VitaLogMutex", 0, 0, NULL);
  if (res < 0) {
    sceClibPrintf("[LOG] Failed to create log mutex (0x%x)\n", res);
    return;
  }

  res = sceKernelCreateLwCond(&log_cond, "VitaLogCond", 0, &log_mutex, NULL);
  if (res < 0) {
    sceClibPrintf("[LOG] Failed to create log cond (0x%x)\n", res);
    return;
  }

  log_thread_should_exit = false;
  log_thread_id = sceKernelCreateThread("VitaLogThread", vita_log_thread_func,
                                        0x40, 0x1000, 0, 0, NULL);
  if (log_thread_id < 0) {
    sceClibPrintf("[LOG] Failed to create log thread (0x%x)\n", log_thread_id);
    return;
  }
  sceKernelStartThread(log_thread_id, 0, NULL);
  log_worker_initialized = true;
}

void vita_log_init_file(void) {
  vita_log_worker_init();
  vita_log_try_open();
}

void vita_log_append(const char *msg) {
  if (!msg || !msg[0])
    return;

  size_t len = strlen(msg);
  if (len == 0)
    return;
  vita_log_worker_init();
  if (!log_worker_initialized)
    return;

  char *copy = malloc(len);
  if (!copy)
    return;
  sceClibMemcpy(copy, msg, len);

  sceKernelLockLwMutex(&log_mutex, 1, NULL);
  vita_log_queue_push_locked(copy, len);
  sceKernelSignalLwCond(&log_cond);
  sceKernelUnlockLwMutex(&log_mutex, 1);
}

void log_cb_debugnet(ChiakiLogLevel lvl, const char *msg, void *user) {
  // int debugnet_lvl;
  // switch (lvl) {
  //   case CHIAKI_LOG_ERROR:
  //   case CHIAKI_LOG_WARNING:
  //     debugnet_lvl = ERROR;
  //     break;
  //   case CHIAKI_LOG_INFO:
  //     debugnet_lvl = INFO;
  //     break;
  //   // case CHIAKI_LOG_VERBOSE:
  //   case CHIAKI_LOG_DEBUG:
  //   // case CHIAKI_LOG_ALL:
  //     debugnet_lvl = DEBUG;
  //     break;
  //   default:
  //     return;
  // }
  if (lvl == CHIAKI_LOG_ALL || lvl == CHIAKI_LOG_VERBOSE) return;
  // uint64_t timestamp = sceKernelGetProcessTimeWide();
  char line[800];
  sceClibSnprintf(line, sizeof(line), "[CHIAKI] %s\n", msg);
  sceClibPrintf("%s", line);
  vita_log_append(line);
  if (!context.stream.is_streaming) {
      if (context.mlog) {
        write_message_log(context.mlog, line);
      }
    }
}

bool vita_chiaki_init_context() {
  config_parse(&context.config);

  // TODO: Load log level from config
  // TODO: Custom logging callback that logs to a file
  chiaki_log_init(&(context.log), CHIAKI_LOG_ALL & ~(CHIAKI_LOG_VERBOSE | CHIAKI_LOG_DEBUG), &log_cb_debugnet, NULL);
  context.mlog = message_log_create();
  vita_log_init_file();

  write_message_log(context.mlog, "----- Debug log start -----"); // debug

  // add manual hosts to context
  update_context_hosts();

  // init ui to select a certain button
  context.ui_state.active_item = UI_MAIN_WIDGET_SETTINGS_BTN;
  context.ui_state.next_active_item = context.ui_state.active_item;

  return true;
}
