#pragma once

#include <chiaki/log.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VITA_LOG_DEFAULT_PATH "ux0:data/vita-chiaki/vitarps5.log"
#define VITA_LOG_DEFAULT_QUEUE_DEPTH 64
// Hard ceiling on log_queue_cap regardless of source (compile-time default or
// runtime TOML override). Testing builds run at 512 (see .env.testing); this
// leaves headroom above that for further investigative bumps without a code
// change, while still bounding a mistaken/malicious runtime override. GH #221.
//
// Memory cost of this ceiling is two parts, not one: the VitaLogMessage{data,
// len} slot array itself is cheap (8 bytes/entry on this 32-bit target, so
// 512 vs. the old 256-entry testing default is +2KB fixed), but each queued
// entry also owns a separately malloc'd copy of its log line (~150 bytes
// typical for this codebase's tagged log lines). That scales with depth, not
// with the ceiling alone: a fully-populated queue is ~77KB at the 512
// testing depth and ~150KB at this 1024 ceiling. This isn't just a rare
// worst-case burst -- under a verbose testing profile the drop-oldest ring
// commonly runs close to full between drains, so budget for the queue
// sitting near its populated-entries cost, not near zero, during normal
// testing-build operation.
#define VITA_LOG_QUEUE_DEPTH_MAX 1024
#define VITA_LOG_MAX_PATH 160

typedef enum {
  VITA_LOG_PROFILE_OFF = 0,
  VITA_LOG_PROFILE_ERRORS,
  VITA_LOG_PROFILE_STANDARD,
  VITA_LOG_PROFILE_VERBOSE
} VitaLogProfile;

typedef struct vita_logging_config_t {
  bool enabled;
  bool force_error_logging;
  VitaLogProfile profile;
  char path[VITA_LOG_MAX_PATH];
  size_t queue_depth;
} VitaLoggingConfig;

void vita_logging_config_set_defaults(VitaLoggingConfig *cfg);
VitaLogProfile vita_logging_profile_from_string(const char *value);
const char *vita_logging_profile_to_string(VitaLogProfile profile);
uint32_t vita_logging_profile_mask(VitaLogProfile profile);

void vita_log_module_init(const VitaLoggingConfig *cfg);
void vita_log_module_shutdown(void);
void vita_log_submit_line(ChiakiLogLevel level, const char *line);
bool vita_log_should_write_level(ChiakiLogLevel level);
const VitaLoggingConfig *vita_log_get_active_config(void);
void vita_log_update_enabled(bool enabled);
