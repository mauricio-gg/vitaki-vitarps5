#pragma once

#include <chiaki/log.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VITA_LOG_DEFAULT_PATH "ux0:data/vita-chiaki/vitarps5.log"
#define VITA_LOG_DEFAULT_QUEUE_DEPTH 64
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
