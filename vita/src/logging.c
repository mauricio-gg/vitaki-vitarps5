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
#warning \
    "VITARPS5_LOGGING_DEFAULT_FORCE_ERRORS not defined by build system - using fallback default (1)"
#define VITARPS5_LOGGING_DEFAULT_FORCE_ERRORS \
  1  // Always log critical errors even when logging disabled
#define VITARPS5_USING_FALLBACK_FORCE_ERRORS
#endif

#ifndef VITARPS5_DEFAULT_LOG_PROFILE
#warning \
    "VITARPS5_DEFAULT_LOG_PROFILE not defined by build system - using fallback default (ERRORS)"
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
#if defined(VITARPS5_USING_FALLBACK_ENABLED) || defined(VITARPS5_USING_FALLBACK_FORCE_ERRORS) || \
    defined(VITARPS5_USING_FALLBACK_PROFILE) || defined(VITARPS5_USING_FALLBACK_QUEUE_DEPTH) ||  \
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

// Dropped-line tracking for the LOGD non-blocking fast path.
// log_lines_dropped is written by multiple threads without a lock; a torn
// increment is an acceptable approximation for this diagnostic counter on
// 32-bit ARM (Cortex-A9 guarantees naturally-aligned 32-bit accesses are
// atomic at the bus level).
static volatile uint32_t log_lines_dropped = 0;

// Ring-buffer overflow tracking -- deliberately a SEPARATE counter from
// log_lines_dropped above. log_lines_dropped only counts lines that never
// made it into the queue at all (mutex-contention fast-path bailout);
// log_lines_evicted counts lines that DID get queued but were then silently
// discarded from the head of a full ring in vita_log_queue_push_locked() to
// make room for a newer line. These are different failure modes with
// different causes (lock contention vs. sustained overflow), and
// conflating them into one counter is exactly what let this eviction path
// go unnoticed -- so they stay separately countable even though both are
// folded into a single leading `LOG_LINES_DROPPED` token in the injected
// summary line (existing greps for that token must keep matching -- but
// note the old literal `count=` field is gone from the format string below;
// anything grepping for `count=` specifically, rather than the
// `LOG_LINES_DROPPED` token, will no longer match).
//
// Unlike log_lines_dropped, this does NOT need `volatile`/torn-read
// tolerance. vita_log_queue_push_locked() is `static` with exactly two call
// sites in the whole codebase (verified by grep), both inside
// vita_log_submit_line(), and both occur after that function has taken
// log_mutex (either via the blocking lock for non-debug lines or a
// successful non-blocking trylock for debug lines -- see the lock block
// below). The only other place this counter is touched is the
// drop-summary reset below, which runs in that same locked region. So every
// read and write of log_lines_evicted happens under log_mutex by
// construction: there is no concurrent-access hazard for `volatile` to
// paper over, and adding it here would misstate the actual (single-writer,
// mutex-serialized) access pattern.
static uint32_t log_lines_evicted = 0;
// GH #262: session-cumulative twin of log_lines_evicted, never reset by the per-window
// summary below (same idea as latency_dropped_total_count in stream_state.h). Root cause
// of the #262 investigation's 5s metrics hole: the eviction summaries themselves were
// self-evicting during a sustained flood, so a per-window-only counter could read back as
// 0 for the exact window that mattered. This total survives that -- it only ever grows,
// so the LAST summary line of a flood still shows the full magnitude even if every
// intermediate one got evicted. Same single-writer(log_mutex-serialized) discipline as
// log_lines_evicted above -- see that field's comment.
static uint64_t log_lines_evicted_total = 0;
// First/last sceKernelGetProcessTimeWide() timestamp of an eviction since
// the last summary report, so the summary can show how wide a time window
// the lost lines spanned, not just how many were lost. Same locking
// discipline as log_lines_evicted (stamped only inside
// vita_log_queue_push_locked()'s eviction branch, reset alongside the
// counter under log_mutex in the summary-injection block below).
static uint64_t log_first_eviction_us = 0;
static uint64_t log_last_eviction_us = 0;
// Timestamp of the last drop-summary injection; updated only while holding
// log_mutex so there is no reset race between threads.
static uint64_t log_last_drop_report_us = 0;

static bool vita_log_queue_is_empty(void) {
  return log_queue_head == log_queue_tail;
}

// REENTRANCY: this function is also how vita_log_submit_line() enqueues its
// own synthetic drop-summary line (pushed later in this file, inside
// vita_log_submit_line()'s once-per-second drop-report block), so it can be
// on its own call stack indirectly. Three things to establish about that:
//
// 1. No unbounded recursion. This function only touches the ring array
//    (log_queue/_head/_tail/_cap) and calls free(); it never calls
//    vita_log_submit_line() or itself. The summary line is built and passed
//    in by the CALLER (vita_log_submit_line) before this function is
//    invoked, so there is no call-graph cycle here at all, bounded or not.
// 2. The summary line cannot evict itself, for any queue depth this
//    codebase actually configures (cap > 1, enforced by the depth floor in
//    config.c). The invariant: an eviction only happens when next_tail ==
//    log_queue_head BEFORE the write, i.e. log_queue_tail and
//    log_queue_head are necessarily different slots at that moment. The
//    write always targets log_queue_tail; the eviction always targets
//    log_queue_head. So a single push only ever evicts the CURRENT head,
//    never the slot it is itself about to fill.
// 3. If pushing the summary line itself triggers an eviction (ring still
//    full from unrelated pressure), that eviction is NOT silently lost: it
//    runs through this exact branch, increments log_lines_evicted, and
//    stamps log_first/last_eviction_us like any other eviction. It just
//    means the summary line pushed this cycle displaces one more real log
//    line, and that displacement gets folded into and surfaced by the NEXT
//    once-per-second summary tick -- self-describing, not silent.
static void vita_log_queue_push_locked(char *data, size_t len) {
  size_t next_tail = (log_queue_tail + 1) % log_queue_cap;
  if (next_tail == log_queue_head) {
    VitaLogMessage drop = log_queue[log_queue_head];
    if (drop.data)
      free(drop.data);
    log_queue_head = (log_queue_head + 1) % log_queue_cap;

    // Overflow eviction bookkeeping. This branch only executes when the
    // ring is already full, so the syscall timestamp below costs nothing
    // during normal, non-overflowing operation (the unconditional write at
    // the bottom of this function is unchanged and stays cheap). But under
    // SUSTAINED overflow -- the exact failure mode this feature exists to
    // measure -- the ring stays full and this branch runs on every push
    // while log_mutex is held. That per-push syscall cost under sustained
    // overflow is an accepted tradeoff: it buys accurate eviction-window
    // instrumentation for precisely the scenario that motivated this fix.
    uint64_t evict_now_us = sceKernelGetProcessTimeWide();
    if (log_lines_evicted == 0)
      log_first_eviction_us = evict_now_us;
    log_last_eviction_us = evict_now_us;
    log_lines_evicted++;
    log_lines_evicted_total++;
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
    size_t copy_len =
        dir_len < sizeof(resolved_log_path) - 1 ? dir_len : sizeof(resolved_log_path) - 1;
    sceClibMemcpy(resolved_log_path, base_path, copy_len);
    pos = copy_len;
  }

  if (pos < sizeof(resolved_log_path)) {
    sceClibSnprintf(resolved_log_path + pos, sizeof(resolved_log_path) - pos, "%s%s",
                    timestamp_prefix, filename);
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
  log_file_fd = sceIoOpen(log_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
  if (log_file_fd >= 0) {
    char header[96];
    uint64_t timestamp = sceKernelGetProcessTimeWide();
    int len = sceClibSnprintf(header, sizeof(header), "\n----- VitaRPS5 log start %ju -----\n",
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
    log_queue_cap =
        active_cfg.queue_depth > 0 ? active_cfg.queue_depth : VITA_LOG_DEFAULT_QUEUE_DEPTH;
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

  // NOTE: VitaLogThread's priority (0x40) is a known concern raised
  // alongside other Vita thread-priority work -- out of scope for this
  // change, left untouched intentionally.
  log_thread_should_exit = false;
  log_thread_id =
      sceKernelCreateThread("VitaLogThread", vita_log_thread_func, 0x40, 0x1000, 0, 0, NULL);
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
    case VITA_LOG_PROFILE_OFF:
      return "off";
    case VITA_LOG_PROFILE_ERRORS:
      return "errors";
    case VITA_LOG_PROFILE_VERBOSE:
      return "verbose";
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
  // Floor: runtime TOML (parse_logging_settings() in config.c, gated by
  // VITARPS5_ALLOW_RUNTIME_LOGGING_CONFIG) is allowed to RAISE queue_depth
  // above the build's compiled default, but must never be allowed to
  // silently lower it below build intent. The three-stage flow that lands
  // here: (1) vita_logging_config_set_defaults() seeds
  // cfg->logging.queue_depth = VITARPS5_LOGGING_DEFAULT_QUEUE_DEPTH (the
  // build-configured value -- 64 for prod, 512 for testing, or this file's
  // VITA_LOG_DEFAULT_QUEUE_DEPTH fallback if the build didn't pass the
  // compile flag at all); (2) config.c's parse_logging_settings() may then
  // overwrite it from the on-device TOML, unconditionally, with no
  // awareness of what stage 1 set; (3) this function is the last chokepoint
  // before the merged value fixes log_queue_cap for the session, so it is
  // the only place that can still catch a TOML value that came in below
  // build intent and correct it.
  //
  // This is not a hypothetical: a hardware incident ran testing (compiled
  // default 512) against a stale on-device TOML that had queue_depth = 128
  // left over from an earlier experiment. TOML silently overrode the
  // compiled 512 down to 128, and it was that undersized 128-deep queue --
  // not a queue running at its intended depth -- that lost 2.5 seconds of
  // diagnostic data during the incident that motivated this whole
  // eviction-tracking feature. The compiled default was never actually in
  // effect; nothing before this fix ever noticed.
  if (active_cfg.queue_depth < VITARPS5_LOGGING_DEFAULT_QUEUE_DEPTH)
    active_cfg.queue_depth = VITARPS5_LOGGING_DEFAULT_QUEUE_DEPTH;
  if (active_cfg.queue_depth > VITA_LOG_QUEUE_DEPTH_MAX)
    active_cfg.queue_depth = VITA_LOG_QUEUE_DEPTH_MAX;
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
                  config_source, active_cfg.enabled, active_cfg.force_error_logging,
                  vita_logging_profile_to_string(active_cfg.profile), active_cfg.queue_depth,
                  active_cfg.path, VITARPS5_BUILD_GIT_COMMIT, VITARPS5_BUILD_GIT_BRANCH,
                  VITARPS5_BUILD_GIT_DIRTY, VITARPS5_BUILD_TIMESTAMP);

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
  bool is_error_or_warning = (level == CHIAKI_LOG_ERROR || level == CHIAKI_LOG_WARNING);
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

  bool is_debug = (level == CHIAKI_LOG_DEBUG);

  if (is_debug) {
    // Non-blocking path for debug: drop rather than stall the recv thread.
    if (sceKernelTryLockLwMutex(&log_mutex, 1) != 0) {
      free(copy);
      log_lines_dropped++;
      return;
    }
  } else {
    sceKernelLockLwMutex(&log_mutex, 1, NULL);
  }

  // Inject a drop-summary line at most once per second when either
  // contention-drops or ring-overflow evictions have occurred. Both
  // counters share this one gate/injection mechanism (rather than a second
  // parallel reporting path) and both reset together below; they are kept
  // as separate fields in the formatted line -- see the log_lines_evicted
  // comment above for why merging them into a single count would hide the
  // distinction that motivated tracking overflow evictions in the first
  // place.
  if (log_lines_dropped > 0 || log_lines_evicted > 0) {
    uint64_t now_us = sceKernelGetProcessTimeWide();
    if (now_us - log_last_drop_report_us >= 1000000ULL) {
      // Snapshot the counters into locals and reset the shared state BEFORE
      // formatting/pushing the summary line below. If that push itself
      // triggers a ring eviction (ring still full from sustained overflow --
      // exactly the case this feature exists to catch), the eviction must
      // land on an already-zeroed log_lines_evicted so it gets picked up by
      // the *next* tick instead of being clobbered by a reset that used to
      // run after the push. See the reentrancy comment on
      // vita_log_queue_push_locked() above for the full argument.
      uint32_t evicted_snapshot = log_lines_evicted;
      uint32_t dropped_snapshot = log_lines_dropped;
      // span_ms covers only the eviction window (0 when no eviction
      // happened this cycle, e.g. a contention-only cycle). Contention
      // drops have no comparable window -- each is a single instantaneous
      // fast-path bailout, not a range of displaced queue entries.
      uint64_t span_ms =
          evicted_snapshot > 0 ? (log_last_eviction_us - log_first_eviction_us) / 1000ULL : 0ULL;

      log_lines_dropped = 0;
      log_lines_evicted = 0;
      log_first_eviction_us = 0;
      log_last_eviction_us = 0;
      log_last_drop_report_us = now_us;

      // Buffer sizing (worst case, all four fields at max width):
      //   "LOG_LINES_DROPPED overflow="   27 chars
      //   + up to 10 digits (uint32_t max 4294967295)
      //   " contention="                  12 chars
      //   + up to 10 digits (uint32_t max)
      //   " span_ms="                      9 chars
      //   + up to 20 digits (uint64_t max 18446744073709551615)
      //   " total_overflow="              16 chars
      //   + up to 20 digits (uint64_t max)
      //   "\n"                             1 char
      //   NUL                              1 char
      //   = 27+10+12+10+9+20+16+20+1+1 = 126 bytes worst case; sized generously above that.
      // GH #262: total_overflow is log_lines_evicted_total -- session-cumulative, never
      // reset (see its declaration comment) -- so even if THIS summary line itself later
      // gets evicted by a still-ongoing flood, the next one that survives still carries
      // the full-session magnitude, not just whatever this one window saw.
      char summary[192];
      int slen = sceClibSnprintf(
          summary, sizeof(summary),
          "LOG_LINES_DROPPED overflow=%u contention=%u span_ms=%llu total_overflow=%llu\n",
          evicted_snapshot, dropped_snapshot, (unsigned long long)span_ms,
          (unsigned long long)log_lines_evicted_total);
      if (slen > 0 && (size_t)slen < sizeof(summary)) {
        char *scopy = malloc((size_t)slen);
        if (scopy) {
          memcpy(scopy, summary, (size_t)slen);
          vita_log_queue_push_locked(scopy, (size_t)slen);
        }
      }
    }
  }

  vita_log_queue_push_locked(copy, len);
  sceKernelSignalLwCond(&log_cond);
  sceKernelUnlockLwMutex(&log_mutex, 1);
}

const VitaLoggingConfig *vita_log_get_active_config(void) {
  if (!cfg_initialized)
    return NULL;
  return &active_cfg;
}

// Called from UI thread only. active_cfg.enabled is read by log callback
// threads but bool writes are atomic on ARM Cortex-A9 (natural alignment),
// so the race is benign: readers see either old or new value safely.
void vita_log_update_enabled(bool enabled) {
  if (!cfg_initialized)
    return;
  active_cfg.enabled = enabled;
  if (enabled && active_cfg.profile < VITA_LOG_PROFILE_VERBOSE)
    active_cfg.profile = VITA_LOG_PROFILE_VERBOSE;
}
