#pragma once

#include <tomlc99/toml.h>

#include "config.h"

typedef enum migration_source_t {
  MIGRATION_SOURCE_NONE = 0,
  MIGRATION_SOURCE_LEGACY_SECTION,
  MIGRATION_SOURCE_ROOT
} MigrationSource;

ChiakiVideoResolutionPreset parse_resolution_preset(const char *preset);
ChiakiVideoResolutionPreset normalize_resolution_for_vita(ChiakiVideoResolutionPreset preset,
                                                          bool *was_downgraded);
VitaChiakiLatencyMode parse_latency_mode(const char *mode);
const char *serialize_latency_mode(VitaChiakiLatencyMode mode);
bool get_circle_btn_confirm_default(void);
const char *serialize_resolution_preset(ChiakiVideoResolutionPreset preset);

bool config_fix_legacy_queue_depth(void);
bool parse_bool_setting_with_migration(toml_table_t *settings,
                                       toml_table_t *parsed,
                                       const char *key,
                                       bool default_value,
                                       bool *out_value,
                                       MigrationSource *out_source);
void apply_migration_source(MigrationSource source,
                            bool *migrated_legacy_settings,
                            bool *migrated_root_settings);
void parse_resolution_with_migration(VitaChiakiConfig *cfg,
                                     toml_table_t *settings,
                                     toml_table_t *parsed,
                                     bool *migrated_legacy_settings,
                                     bool *migrated_root_settings,
                                     bool *migrated_resolution_policy);
void parse_fps_with_migration(VitaChiakiConfig *cfg,
                              toml_table_t *settings,
                              toml_table_t *parsed,
                              bool *migrated_legacy_settings,
                              bool *migrated_root_settings);
void parse_latency_mode_with_migration(VitaChiakiConfig *cfg,
                                       toml_table_t *settings,
                                       toml_table_t *parsed,
                                       bool *migrated_legacy_settings,
                                       bool *migrated_root_settings);
