#pragma once

#include <chiaki/log.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VITA_LOG_DEFAULT_PATH "ux0:data/vita-chiaki/vitarps5.log"
// Last-resort fallback, NOT the depth either real build profile actually
// runs at. This only matters for a degenerate/ad-hoc build that skips the
// .env-based compile flag entirely (see the
// VITARPS5_LOGGING_DEFAULT_QUEUE_DEPTH #warning block in logging.c) --
// .env.prod pins 64 and .env.testing pins 512 via
// -DVITARPS5_LOGGING_DEFAULT_QUEUE_DEPTH in tools/build.sh/CMakeLists.txt,
// so both real profiles bypass this constant entirely.
//
// Chosen as 512 to match testing's real-world queue-depth value rather than
// inventing a third number. This is NOT a claim that 512 is validated or
// sufficient -- it has never actually been exercised by the incident that
// motivated this eviction-tracking feature. That incident ran testing
// (compiled default 512) but a stale on-device TOML config silently
// overrode it down to 128 at runtime (VITARPS5_ALLOW_RUNTIME_LOGGING_CONFIG
// permits TOML to override the compiled default with no floor -- see
// parse_logging_settings() in config.c); the compiled 512 was never in effect, and it was that
// undersized 128-deep queue that lost the diagnostic data. So 512 is simply
// the best available real-world reference point for this fallback, not a
// proven-sufficient number either way. The actual fix for that failure mode
// is not this constant -- it's closing the silent-downgrade path itself,
// which vita_log_module_init() (logging.c) now does by flooring the merged
// config against the build's compiled default before it can take effect.
// Raising this misconfigured-build fallback to 512 is still a strict
// improvement over the old 64 on its own terms. Its documented memory cost
// (see the VITA_LOG_QUEUE_DEPTH_MAX comment below for the full per-entry
// accounting) is a cost this file already accepts for testing builds.
//
// This macro is referenced in several other spots too -- e.g.
// config_parse_file_with_queue_fix() in config.c (a log message),
// vita_log_worker_init()'s own defensive fallback in this file, and the
// TOML-corruption repair value in config_migration.c -- this comment does
// not claim to enumerate them exhaustively; grep the symbol for the current
// full list rather than trusting a hand-maintained one here. The one call
// site worth flagging specifically: config_migration.c's repair path is NOT
// gated behind VITARPS5_ALLOW_RUNTIME_LOGGING_CONFIG, so it runs in prod
// too -- a prod build repairing a corrupt config will write
// queue_depth = 512 to disk, not 64. There is still no LIVE prod memory
// cost from that, because prod's parse_logging_settings() path that would
// read the value back is #if'd out entirely.
#define VITA_LOG_DEFAULT_QUEUE_DEPTH 512
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
