#include "logging.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <stdlib.h>
#include <string.h>

// Production-safe fallback defaults: minimal logging if build system fails to configure.
// Build scripts (CMakeLists.txt, build.sh) override these via -D flags for debug/testing builds.
//
// COMPILE-TIME VALIDATION: If you see warnings below, the build system did not pass explicit
// logging configuration. This is acceptable (fallbacks are production-safe) but may indicate
// a build configuration issue. Expected behavior:
// - build.sh sets these flags based on .env.prod or .env.testing
// - CMakeLists.txt passes them to the compiler via target_compile_definitions
//
// If warnings appear during normal builds, verify:
// 1. tools/build.sh is reading the correct .env file
// 2. CMake is receiving the -D flags (check CMAKE_EXTRA_FLAGS in build.sh)
// 3. The build environment matches expectations (prod/testing/debug)

#ifndef VITARPS5_LOGGING_DEFAULT_ENABLED
#warning "VITARPS5_LOGGING_DEFAULT_ENABLED not defined by build system - using fallback default (0)"
#define VITARPS5_LOGGING_DEFAULT_ENABLED 0  // Default OFF for production safety
#define VITARPS5_USING_FALLBACK_ENABLED
#endif

#ifndef VITARPS5_LOGGING_DEFAULT_FORCE_ERRORS
#warning "VITARPS5_LOGGING_DEFAULT_FORCE_ERRORS not defined by build system - using fallback default (1)"
#define VITARPS5_LOGGING_DEFAULT_FORCE_ERRORS 1  // Always log critical errors even when logging disabled
#define VITARPS5_USING_FALLBACK_FORCE_ERRORS
#endif

#ifndef VITARPS5_DEFAULT_LOG_PROFILE
#warning "VITARPS5_DEFAULT_LOG_PROFILE not defined by build system - using fallback default (ERRORS)"
#define VITARPS5_DEFAULT_LOG_PROFILE VITA_LOG_PROFILE_ERRORS  // Only critical errors by default
#define VITARPS5_USING_FALLBACK_PROFILE
#endif

#ifndef VITARPS5_LOGGING_DEFAULT_QUEUE_DEPTH
#warning "VITARPS5_LOGGING_DEFAULT_QUEUE_DEPTH not defined by build system - using fallback default"
#define VITARPS5_LOGGING_DEFAULT_QUEUE_DEPTH VITA_LOG_DEFAULT_QUEUE_DEPTH
#define VITARPS5_USING_FALLBACK_QUEUE_DEPTH
#endif

#ifndef VITARPS5_LOGGING_DEFAULT_PATH
#warning "VITARPS5_LOGGING_DEFAULT_PATH not defined by build system - using fallback default"
#define VITARPS5_LOGGING_DEFAULT_PATH VITA_LOG_DEFAULT_PATH
#define VITARPS5_USING_FALLBACK_PATH
#endif

#ifndef VITARPS5_BUILD_GIT_COMMIT
#define VITARPS5_BUILD_GIT_COMMIT "unknown"
#endif

#ifndef VITARPS5_BUILD_GIT_BRANCH
#define VITARPS5_BUILD_GIT_BRANCH "unknown"
#endif

#ifndef VITARPS5_BUILD_GIT_DIRTY
#define VITARPS5_BUILD_GIT_DIRTY -1
#endif

#ifndef VITARPS5_BUILD_TIMESTAMP
#define VITARPS5_BUILD_TIMESTAMP "unknown"
#endif

// Compile-time summary: detect if ANY fallbacks are active
#if defined(VITARPS5_USING_FALLBACK_ENABLED) || \
    defined(VITARPS5_USING_FALLBACK_FORCE_ERRORS) || \
    defined(VITARPS5_USING_FALLBACK_PROFILE) || \
    defined(VITARPS5_USING_FALLBACK_QUEUE_DEPTH) || \
    defined(VITARPS5_USING_FALLBACK_PATH)
#define VITARPS5_USING_FALLBACK_CONFIG
#endif

typedef struct {
  char *data;
  size_t len;
} VitaLogMessage;

static VitaLoggingConfig active_cfg;
static bool cfg_initialized = false;
static char resolved_log_path[VITA_LOG_MAX_PATH];
static bool log_path_resolved = false;

static SceUID log_file_fd = -1;
static bool log_file_failed = false;
static SceUID log_thread_id = -1;
static SceKernelLwMutexWork log_mutex;
static SceKernelLwCondWork log_cond;
static bool log_worker_initialized = false;
static bool log_thread_should_exit = false;
static VitaLogMessage *log_queue = NULL;
static size_t log_queue_head = 0;
static size_t log_queue_tail = 0;
static size_t log_queue_cap = 0;

static bool vita_log_queue_is_empty(void) {
  return log_queue_head == log_queue_tail;
}

static void vita_log_queue_push_locked(char *data, size_t len) {
  size_t next_tail = (log_queue_tail + 1) % log_queue_cap;
  if (next_tail == log_queue_head) {
    VitaLogMessage drop = log_queue[log_queue_head];
    if (drop.data)
      free(drop.data);
    log_queue_head = (log_queue_head + 1) % log_queue_cap;
  }
  log_queue[log_queue_tail].data = data;
  log_queue[log_queue_tail].len = len;
  log_queue_tail = next_tail;
}

static bool vita_log_queue_pop_locked(VitaLogMessage *out_msg) {
  if (vita_log_queue_is_empty())
    return false;
  *out_msg = log_queue[log_queue_head];
  log_queue_head = (log_queue_head + 1) % log_queue_cap;
  return true;
}

static const char *vita_log_get_path(void) {
  if (log_path_resolved && resolved_log_path[0])
    return resolved_log_path;

  const char *base_path = active_cfg.path;
  const char *last_sep = strrchr(base_path, '/');
  size_t dir_len = 0;
  const char *filename = base_path;
  if (last_sep) {
    dir_len = (size_t)(last_sep - base_path + 1);
    filename = last_sep + 1;
  }
  if (!filename || filename[0] == '\0')
    filename = "log.txt";

  char timestamp_prefix[32];
  uint64_t timestamp = sceKernelGetSystemTimeWide();
  sceClibSnprintf(timestamp_prefix, sizeof(timestamp_prefix), "%llu_",
                  (unsigned long long)timestamp);

  size_t pos = 0;
  if (dir_len > 0) {
    size_t copy_len = dir_len < sizeof(resolved_log_path) - 1
                          ? dir_len
                          : sizeof(resolved_log_path) - 1;
    sceClibMemcpy(resolved_log_path, base_path, copy_len);
    pos = copy_len;
  }

  if (pos < sizeof(resolved_log_path)) {
    sceClibSnprintf(resolved_log_path + pos,
                    sizeof(resolved_log_path) - pos,
                    "%s%s",
                    timestamp_prefix,
                    filename);
  } else {
    resolved_log_path[sizeof(resolved_log_path) - 1] = '\0';
  }

  log_path_resolved = true;
  return resolved_log_path;
}

static void vita_log_try_open(void) {
  if (log_file_fd >= 0 || log_file_failed || !cfg_initialized)
    return;

  const char *log_path = vita_log_get_path();
  sceIoMkdir("ux0:data", 0777);
  sceIoMkdir("ux0:data/vita-chiaki", 0777);
  log_file_fd = sceIoOpen(log_path,
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
  }
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

static void vita_log_queue_destroy(void) {
  if (!log_queue)
    return;
  for (size_t i = 0; i < log_queue_cap; ++i) {
    if (log_queue[i].data)
      free(log_queue[i].data);
  }
  free(log_queue);
  log_queue = NULL;
  log_queue_cap = 0;
  log_queue_head = 0;
  log_queue_tail = 0;
}

static void vita_log_worker_init(void) {
  if (log_worker_initialized || !cfg_initialized)
    return;

  if (log_queue_cap == 0)
    log_queue_cap = active_cfg.queue_depth > 0 ? active_cfg.queue_depth
                                               : VITA_LOG_DEFAULT_QUEUE_DEPTH;
  log_queue = calloc(log_queue_cap, sizeof(VitaLogMessage));
  if (!log_queue) {
    log_queue_cap = 0;
    return;
  }

  int res = sceKernelCreateLwMutex(&log_mutex, "VitaLogMutex", 0, 0, NULL);
  if (res < 0)
    return;

  res = sceKernelCreateLwCond(&log_cond, "VitaLogCond", 0, &log_mutex, NULL);
  if (res < 0)
    return;

  log_thread_should_exit = false;
  log_thread_id = sceKernelCreateThread("VitaLogThread", vita_log_thread_func,
                                        0x40, 0x1000, 0, 0, NULL);
  if (log_thread_id < 0)
    return;
  sceKernelStartThread(log_thread_id, 0, NULL);
  log_worker_initialized = true;
}

void vita_logging_config_set_defaults(VitaLoggingConfig *cfg) {
  if (!cfg)
    return;
  cfg->enabled = VITARPS5_LOGGING_DEFAULT_ENABLED != 0;
  cfg->force_error_logging = VITARPS5_LOGGING_DEFAULT_FORCE_ERRORS != 0;
  cfg->profile = VITARPS5_DEFAULT_LOG_PROFILE;
  cfg->queue_depth = VITARPS5_LOGGING_DEFAULT_QUEUE_DEPTH;
  memset(cfg->path, 0, sizeof(cfg->path));
  strncpy(cfg->path, VITARPS5_LOGGING_DEFAULT_PATH, sizeof(cfg->path) - 1);
}

VitaLogProfile vita_logging_profile_from_string(const char *value) {
  if (!value)
    return VITA_LOG_PROFILE_STANDARD;
  if (strcmp(value, "off") == 0)
    return VITA_LOG_PROFILE_OFF;
  if (strcmp(value, "errors") == 0)
    return VITA_LOG_PROFILE_ERRORS;
  if (strcmp(value, "verbose") == 0)
    return VITA_LOG_PROFILE_VERBOSE;
  return VITA_LOG_PROFILE_STANDARD;
}

const char *vita_logging_profile_to_string(VitaLogProfile profile) {
  switch (profile) {
    case VITA_LOG_PROFILE_OFF: return "off";
    case VITA_LOG_PROFILE_ERRORS: return "errors";
    case VITA_LOG_PROFILE_VERBOSE: return "verbose";
    case VITA_LOG_PROFILE_STANDARD:
    default:
      return "standard";
  }
}

uint32_t vita_logging_profile_mask(VitaLogProfile profile) {
  switch (profile) {
    case VITA_LOG_PROFILE_OFF:
    case VITA_LOG_PROFILE_ERRORS:
      return CHIAKI_LOG_ERROR | CHIAKI_LOG_WARNING;
    case VITA_LOG_PROFILE_VERBOSE:
      return CHIAKI_LOG_ALL;
    case VITA_LOG_PROFILE_STANDARD:
    default:
      return CHIAKI_LOG_ALL & ~(CHIAKI_LOG_VERBOSE | CHIAKI_LOG_DEBUG);
  }
}

void vita_log_module_init(const VitaLoggingConfig *cfg) {
  vita_logging_config_set_defaults(&active_cfg);
  if (cfg)
    memcpy(&active_cfg, cfg, sizeof(VitaLoggingConfig));
  if (active_cfg.queue_depth == 0)
    active_cfg.queue_depth = VITA_LOG_DEFAULT_QUEUE_DEPTH;
  if (active_cfg.queue_depth > 256)
    active_cfg.queue_depth = 256;
  if (!active_cfg.path[0])
    strncpy(active_cfg.path, VITA_LOG_DEFAULT_PATH, sizeof(active_cfg.path) - 1);
  log_queue_cap = active_cfg.queue_depth;
  cfg_initialized = true;
  log_path_resolved = false;
  resolved_log_path[0] = '\0';

  // Runtime validation: log active configuration for debugging build issues
  // This runs once at initialization and helps verify build system behavior.
  // Output only appears if logging is enabled (testing/debug builds).
#ifdef VITARPS5_USING_FALLBACK_CONFIG
  const char *config_source = "FALLBACK (build system did not configure)";
#else
  const char *config_source = "build system";
#endif

  // Format a detailed configuration summary
  char init_msg[768];
  sceClibSnprintf(init_msg, sizeof(init_msg),
                  "[LOGGING] Initialized from %s:\n"
                  "  enabled=%d, force_errors=%d, profile=%s, queue=%zu, path=%s\n"
                  "[PIPE/BUILD] commit=%s branch=%s dirty=%d built=%s\n",
                  config_source,
                  active_cfg.enabled,
                  active_cfg.force_error_logging,
                  vita_logging_profile_to_string(active_cfg.profile),
                  active_cfg.queue_depth,
                  active_cfg.path,
                  VITARPS5_BUILD_GIT_COMMIT,
                  VITARPS5_BUILD_GIT_BRANCH,
                  VITARPS5_BUILD_GIT_DIRTY,
                  VITARPS5_BUILD_TIMESTAMP);

  // Log initialization details in testing/debug builds where logging is enabled.
  // Production builds (enabled=false) will skip this entirely.
  if (active_cfg.enabled) {
    vita_log_submit_line(CHIAKI_LOG_INFO, init_msg);
  }
}

void vita_log_module_shutdown(void) {
  if (log_worker_initialized) {
    sceKernelLockLwMutex(&log_mutex, 1, NULL);
    log_thread_should_exit = true;
    sceKernelSignalLwCond(&log_cond);
    sceKernelUnlockLwMutex(&log_mutex, 1);
    sceKernelWaitThreadEnd(log_thread_id, NULL, NULL);
    log_thread_id = -1;
    sceKernelDeleteLwCond(&log_cond);
    sceKernelDeleteLwMutex(&log_mutex);
    log_worker_initialized = false;
  }

  vita_log_queue_destroy();

  if (log_file_fd >= 0) {
    sceIoClose(log_file_fd);
    log_file_fd = -1;
  }
  log_file_failed = false;
  log_path_resolved = false;
  resolved_log_path[0] = '\0';
}

bool vita_log_should_write_level(ChiakiLogLevel level) {
  if (!cfg_initialized)
    return false;
  bool is_error_or_warning = (level == CHIAKI_LOG_ERROR ||
                              level == CHIAKI_LOG_WARNING);
  if (!active_cfg.enabled)
    return active_cfg.force_error_logging && is_error_or_warning;
  return true;
}

void vita_log_submit_line(ChiakiLogLevel level, const char *line) {
  if (!line || !line[0])
    return;
  if (!vita_log_should_write_level(level))
    return;

  size_t len = strlen(line);
  if (len == 0)
    return;

  vita_log_worker_init();
  if (!log_worker_initialized)
    return;

  char *copy = malloc(len);
  if (!copy)
    return;
  memcpy(copy, line, len);

  sceKernelLockLwMutex(&log_mutex, 1, NULL);
  vita_log_queue_push_locked(copy, len);
  sceKernelSignalLwCond(&log_cond);
  sceKernelUnlockLwMutex(&log_mutex, 1);
}

const VitaLoggingConfig *vita_log_get_active_config(void) {
  if (!cfg_initialized)
    return NULL;
  return &active_cfg;
}
