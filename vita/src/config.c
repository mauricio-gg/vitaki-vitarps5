#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <string.h>
#include <tomlc99/toml.h>

#include "config.h"
#include "config_internal.h"
#include "config_hosts.h"
#include "context.h"
#include "host.h"
#include "util.h"

static bool config_fix_legacy_queue_depth(void) {
  FILE* fp = fopen(CFG_FILENAME, "r");
  if (!fp)
    return false;

  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return false;
  }
  long len = ftell(fp);
  if (len <= 0) {
    fclose(fp);
    return false;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return false;
  }

  size_t size = (size_t)len;
  char* data = malloc(size);
  if (!data) {
    fclose(fp);
    return false;
  }
  size_t read = fread(data, 1, size, fp);
  fclose(fp);
  if (read != size) {
    free(data);
    return false;
  }

  const char* prefix = "queue_depth = ";
  char* prefix_pos = strstr(data, prefix);
  if (!prefix_pos) {
    free(data);
    return false;
  }
  char* value_start = prefix_pos + strlen(prefix);
  size_t remaining = (size_t)(data + size - value_start);
  char* line_end = memchr(value_start, '\n', remaining);
  if (!line_end)
    line_end = data + size;

  char replacement[16];
  int replacement_len = snprintf(replacement, sizeof(replacement), "%u", VITA_LOG_DEFAULT_QUEUE_DEPTH);
  if (replacement_len <= 0) {
    free(data);
    return false;
  }

  size_t head_len = (size_t)(value_start - data);
  size_t tail_len = (size_t)(data + size - line_end);
  size_t new_size = head_len + (size_t)replacement_len + tail_len;
  char* patched = malloc(new_size);
  if (!patched) {
    free(data);
    return false;
  }

  memcpy(patched, data, head_len);
  memcpy(patched + head_len, replacement, (size_t)replacement_len);
  memcpy(patched + head_len + (size_t)replacement_len, line_end, tail_len);

  fp = fopen(CFG_FILENAME, "w");
  if (!fp) {
    free(data);
    free(patched);
    return false;
  }
  size_t written = fwrite(patched, 1, new_size, fp);
  fclose(fp);

  free(data);
  free(patched);
  return written == new_size;
}

static bool parse_legacy_bool_setting(toml_table_t *parsed, const char *key, bool *out) {
  for (int slot = 0; slot < 3; slot++) {
    char section_name[32];
    snprintf(section_name, sizeof(section_name), "controller_custom_map_%d", slot + 1);
    toml_table_t *legacy = toml_table_in(parsed, section_name);
    if (!legacy)
      continue;
    toml_datum_t datum = toml_bool_in(legacy, key);
    if (datum.ok) {
      *out = datum.u.b;
      return true;
    }
  }
  return false;
}

static bool parse_root_bool_setting(toml_table_t *parsed, const char *key, bool *out) {
  toml_datum_t datum = toml_bool_in(parsed, key);
  if (!datum.ok)
    return false;
  *out = datum.u.b;
  return true;
}

static bool parse_legacy_int_setting(toml_table_t *parsed, const char *key, int *out) {
  for (int slot = 0; slot < 3; slot++) {
    char section_name[32];
    snprintf(section_name, sizeof(section_name), "controller_custom_map_%d", slot + 1);
    toml_table_t *legacy = toml_table_in(parsed, section_name);
    if (!legacy)
      continue;
    toml_datum_t datum = toml_int_in(legacy, key);
    if (datum.ok) {
      *out = (int)datum.u.i;
      return true;
    }
  }
  return false;
}

static bool parse_root_int_setting(toml_table_t *parsed, const char *key, int *out) {
  toml_datum_t datum = toml_int_in(parsed, key);
  if (!datum.ok)
    return false;
  *out = (int)datum.u.i;
  return true;
}

// Returns allocated string via *out that caller must free.
static bool parse_legacy_string_setting(toml_table_t *parsed, const char *key, char **out) {
  for (int slot = 0; slot < 3; slot++) {
    char section_name[32];
    snprintf(section_name, sizeof(section_name), "controller_custom_map_%d", slot + 1);
    toml_table_t *legacy = toml_table_in(parsed, section_name);
    if (!legacy)
      continue;
    toml_datum_t datum = toml_string_in(legacy, key);
    if (datum.ok) {
      *out = datum.u.s;
      return true;
    }
  }
  return false;
}

// Returns allocated string via *out that caller must free.
static bool parse_root_string_setting(toml_table_t *parsed, const char *key, char **out) {
  toml_datum_t datum = toml_string_in(parsed, key);
  if (!datum.ok)
    return false;
  *out = datum.u.s;
  return true;
}

typedef enum migration_source_t {
  MIGRATION_SOURCE_NONE = 0,
  MIGRATION_SOURCE_LEGACY_SECTION,
  MIGRATION_SOURCE_ROOT
} MigrationSource;

static bool parse_bool_setting_with_migration(toml_table_t *settings,
                                              toml_table_t *parsed,
                                              const char *key,
                                              bool default_value,
                                              bool *out_value,
                                              MigrationSource *out_source) {
  toml_datum_t datum;
  bool legacy_value = false;
  if (out_source)
    *out_source = MIGRATION_SOURCE_NONE;
  if (settings) {
    datum = toml_bool_in(settings, key);
    if (datum.ok) {
      *out_value = datum.u.b;
      return true;
    }
  }
  if (parse_legacy_bool_setting(parsed, key, &legacy_value)) {
    *out_value = legacy_value;
    if (out_source)
      *out_source = MIGRATION_SOURCE_LEGACY_SECTION;
    return true;
  }
  if (parse_root_bool_setting(parsed, key, &legacy_value)) {
    *out_value = legacy_value;
    if (out_source)
      *out_source = MIGRATION_SOURCE_ROOT;
    return true;
  }
  *out_value = default_value;
  return true;
}

typedef struct bool_setting_spec_t {
  const char *key;
  bool default_value;
  bool *out_value;
} BoolSettingSpec;

typedef struct bool_serialize_spec_t {
  const char *key;
  bool value;
} BoolSerializeSpec;

static void apply_migration_source(MigrationSource source,
                                   bool *migrated_legacy_settings,
                                   bool *migrated_root_settings) {
  if (source == MIGRATION_SOURCE_LEGACY_SECTION)
    *migrated_legacy_settings = true;
  else if (source == MIGRATION_SOURCE_ROOT)
    *migrated_root_settings = true;
}

static void config_set_defaults(VitaChiakiConfig *cfg, bool circle_btn_confirm_default) {
  cfg->psn_account_id = NULL;
  cfg->auto_discovery = true;
  cfg->resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
  cfg->fps = CHIAKI_VIDEO_FPS_PRESET_30;
  cfg->controller_map_id = VITAKI_CONTROLLER_MAP_CUSTOM_1;
  for (int i = 0; i < 3; i++) {
    controller_map_storage_set_defaults(&cfg->custom_maps[i]);
    cfg->custom_maps_valid[i] = false;
  }
  cfg->show_latency = false;
  cfg->show_network_indicator = true;
  cfg->show_stream_exit_hint = true;
  cfg->latency_mode = VITA_LATENCY_MODE_BALANCED;
  cfg->stretch_video = false;
  cfg->force_30fps = false;
  cfg->send_actual_start_bitrate = true;
  cfg->clamp_soft_restart_bitrate = true;
  cfg->show_nav_labels = false;
  cfg->show_only_paired = false;
  cfg->circle_btn_confirm = circle_btn_confirm_default;
  vita_logging_config_set_defaults(&cfg->logging);
}

static void load_custom_map_slot(toml_table_t *custom_map,
                                 ControllerMapStorage *map,
                                 bool *valid_out) {
  toml_datum_t datum = toml_bool_in(custom_map, "valid");
  bool valid = datum.ok ? datum.u.b : false;
  *valid_out = valid;
  if (!valid)
    return;

  for (int i = 0; i < VITAKI_CTRL_IN_COUNT; i++) {
    char key[24];
    snprintf(key, sizeof(key), "slot_%d", i);
    datum = toml_int_in(custom_map, key);
    map->in_out_btn[i] = datum.ok ? (int)datum.u.i : 0;
  }
  datum = toml_int_in(custom_map, "in_l2");
  map->in_l2 = datum.ok ? (int)datum.u.i : VITAKI_CTRL_IN_NONE;
  datum = toml_int_in(custom_map, "in_r2");
  map->in_r2 = datum.ok ? (int)datum.u.i : VITAKI_CTRL_IN_NONE;
}

static void parse_custom_map_slots(VitaChiakiConfig *cfg, toml_table_t *parsed) {
  for (int slot = 0; slot < 3; slot++) {
    char section_name[32];
    snprintf(section_name, sizeof(section_name), "controller_custom_map_%d", slot + 1);
    toml_table_t *custom_map = toml_table_in(parsed, section_name);
    if (!custom_map)
      continue;
    load_custom_map_slot(custom_map, &cfg->custom_maps[slot], &cfg->custom_maps_valid[slot]);
  }
}

static void migrate_legacy_custom_map_if_needed(VitaChiakiConfig *cfg, toml_table_t *parsed) {
  if (cfg->custom_maps_valid[0])
    return;

  toml_table_t *old_custom_map = toml_table_in(parsed, "controller_custom_map");
  if (!old_custom_map)
    return;

  bool old_valid = false;
  load_custom_map_slot(old_custom_map, &cfg->custom_maps[0], &old_valid);
  cfg->custom_maps_valid[0] = old_valid;
}

static void serialize_bool_settings(FILE *fp, const BoolSerializeSpec *specs, size_t count) {
  for (size_t i = 0; i < count; i++) {
    fprintf(fp, "%s = %s\n", specs[i].key, specs[i].value ? "true" : "false");
  }
}

static void parse_logging_settings(VitaChiakiConfig *cfg, toml_table_t *parsed) {
#if defined(VITARPS5_ALLOW_RUNTIME_LOGGING_CONFIG) && VITARPS5_ALLOW_RUNTIME_LOGGING_CONFIG
  toml_table_t *logging = toml_table_in(parsed, "logging");
  if (!logging)
    return;

  toml_datum_t datum = toml_bool_in(logging, "enabled");
  if (datum.ok)
    cfg->logging.enabled = datum.u.b;

  datum = toml_bool_in(logging, "force_error_logging");
  if (datum.ok)
    cfg->logging.force_error_logging = datum.u.b;

  datum = toml_string_in(logging, "profile");
  if (datum.ok) {
    cfg->logging.profile = vita_logging_profile_from_string(datum.u.s);
    free(datum.u.s);
  }

  datum = toml_int_in(logging, "queue_depth");
  if (datum.ok) {
    if (datum.u.i < 8)
      cfg->logging.queue_depth = 8;
    else if (datum.u.i > 256)
      cfg->logging.queue_depth = 256;
    else
      cfg->logging.queue_depth = datum.u.i;
  }

  datum = toml_string_in(logging, "path");
  if (datum.ok) {
    strncpy(cfg->logging.path, datum.u.s, sizeof(cfg->logging.path) - 1);
    cfg->logging.path[sizeof(cfg->logging.path) - 1] = '\0';
    free(datum.u.s);
  }
#else
  // Production build: Ignore [logging] section in TOML entirely.
  (void)cfg;
  (void)toml_table_in(parsed, "logging");
#endif
}

static void parse_resolution_with_migration(VitaChiakiConfig *cfg,
                                            toml_table_t *settings,
                                            toml_table_t *parsed,
                                            bool *migrated_legacy_settings,
                                            bool *migrated_root_settings,
                                            bool *migrated_resolution_policy) {
  toml_datum_t datum;
  char *str_value = NULL;
  MigrationSource source = MIGRATION_SOURCE_NONE;

  if (settings) {
    datum = toml_string_in(settings, "resolution");
    if (datum.ok)
      str_value = datum.u.s;
  }
  if (!str_value && parse_legacy_string_setting(parsed, "resolution", &str_value))
    source = MIGRATION_SOURCE_LEGACY_SECTION;
  if (!str_value && parse_root_string_setting(parsed, "resolution", &str_value))
    source = MIGRATION_SOURCE_ROOT;

  if (str_value) {
    cfg->resolution = parse_resolution_preset(str_value);
    free(str_value);
    apply_migration_source(source, migrated_legacy_settings, migrated_root_settings);
  }

  bool downgraded_resolution = false;
  cfg->resolution = normalize_resolution_for_vita(cfg->resolution, &downgraded_resolution);
  if (downgraded_resolution) {
    LOGD("Resolution is not supported on Vita; downgrading to 540p");
    *migrated_resolution_policy = true;
  }
}

static void parse_fps_with_migration(VitaChiakiConfig *cfg,
                                     toml_table_t *settings,
                                     toml_table_t *parsed,
                                     bool *migrated_legacy_settings,
                                     bool *migrated_root_settings) {
  toml_datum_t datum;
  int fps_value = 30;
  bool fps_found = false;
  MigrationSource source = MIGRATION_SOURCE_NONE;

  if (settings) {
    datum = toml_int_in(settings, "fps");
    if (datum.ok) {
      fps_value = (int)datum.u.i;
      fps_found = true;
    }
  }
  if (!fps_found && parse_legacy_int_setting(parsed, "fps", &fps_value)) {
    fps_found = true;
    source = MIGRATION_SOURCE_LEGACY_SECTION;
  }
  if (!fps_found && parse_root_int_setting(parsed, "fps", &fps_value)) {
    fps_found = true;
    source = MIGRATION_SOURCE_ROOT;
  }
  if (!fps_found)
    return;

  if (fps_value == 60) {
    cfg->fps = CHIAKI_VIDEO_FPS_PRESET_60;
  } else {
    if (fps_value != 30) {
      LOGD("Unsupported fps value %d in config (supported: 30 or 60); defaulting to 30", fps_value);
    }
    cfg->fps = CHIAKI_VIDEO_FPS_PRESET_30;
  }
  apply_migration_source(source, migrated_legacy_settings, migrated_root_settings);
}

static void parse_latency_mode_with_migration(VitaChiakiConfig *cfg,
                                              toml_table_t *settings,
                                              toml_table_t *parsed,
                                              bool *migrated_legacy_settings,
                                              bool *migrated_root_settings) {
  toml_datum_t datum;
  char *str_value = NULL;
  MigrationSource source = MIGRATION_SOURCE_NONE;

  if (settings) {
    datum = toml_string_in(settings, "latency_mode");
    if (datum.ok)
      str_value = datum.u.s;
  }
  if (!str_value && parse_legacy_string_setting(parsed, "latency_mode", &str_value))
    source = MIGRATION_SOURCE_LEGACY_SECTION;
  if (!str_value && parse_root_string_setting(parsed, "latency_mode", &str_value))
    source = MIGRATION_SOURCE_ROOT;

  if (str_value) {
    cfg->latency_mode = parse_latency_mode(str_value);
    free(str_value);
    apply_migration_source(source, migrated_legacy_settings, migrated_root_settings);
  } else {
    cfg->latency_mode = VITA_LATENCY_MODE_BALANCED;
  }
}

static bool config_parse_file_with_queue_fix(toml_table_t **parsed_out) {
  char errbuf[200];
  bool attempted_queue_fix = false;

  while (true) {
    FILE *fp = fopen(CFG_FILENAME, "r");
    if (!fp) {
      CHIAKI_LOGE(&(context.log), "Failed to open %s for reading", CFG_FILENAME);
      return false;
    }

    toml_table_t *parsed = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);
    if (parsed) {
      *parsed_out = parsed;
      return true;
    }

    if (attempted_queue_fix || !config_fix_legacy_queue_depth()) {
      CHIAKI_LOGE(&(context.log), "Failed to parse config due to illegal TOML: %s", errbuf);
      return false;
    }

    attempted_queue_fix = true;
    CHIAKI_LOGW(&(context.log),
                "Recovered invalid logging.queue_depth entry, resetting to default (%u)",
                VITA_LOG_DEFAULT_QUEUE_DEPTH);
  }
}

static bool config_validate_general_section(toml_table_t *parsed) {
  toml_table_t *general = toml_table_in(parsed, "general");
  if (!general) {
    CHIAKI_LOGE(&(context.log), "Failed to parse config due to missing [general] section");
    return false;
  }

  toml_datum_t datum = toml_int_in(general, "version");
  if (!datum.ok || datum.u.i != CFG_VERSION) {
    CHIAKI_LOGE(&(context.log), "Failed to parse config due to bad general.version, expected %d.", CFG_VERSION);
    return false;
  }

  return true;
}

static void parse_basic_settings(VitaChiakiConfig *cfg, toml_table_t *settings) {
  toml_datum_t datum;
  if (!settings)
    return;

  datum = toml_bool_in(settings, "auto_discovery");
  cfg->auto_discovery = datum.ok ? datum.u.b : true;

  datum = toml_string_in(settings, "psn_account_id");
  if (datum.ok)
    cfg->psn_account_id = datum.u.s;

  datum = toml_int_in(settings, "controller_map_id");
  if (datum.ok)
    cfg->controller_map_id = datum.u.i;
}

static void normalize_controller_map_id(VitaChiakiConfig *cfg) {
  if (cfg->controller_map_id == VITAKI_CONTROLLER_MAP_CUSTOM_1 ||
      cfg->controller_map_id == VITAKI_CONTROLLER_MAP_CUSTOM_2 ||
      cfg->controller_map_id == VITAKI_CONTROLLER_MAP_CUSTOM_3) {
    return;
  }
  cfg->controller_map_id = VITAKI_CONTROLLER_MAP_CUSTOM_1;
}

static void parse_bool_settings_with_migration(VitaChiakiConfig *cfg,
                                               toml_table_t *settings,
                                               toml_table_t *parsed,
                                               bool circle_btn_confirm_default,
                                               bool *migrated_legacy_settings,
                                               bool *migrated_root_settings) {
  BoolSettingSpec bool_settings[] = {
    {"circle_btn_confirm", circle_btn_confirm_default, &cfg->circle_btn_confirm},
    {"show_latency", false, &cfg->show_latency},
    {"show_network_indicator", true, &cfg->show_network_indicator},
    {"show_stream_exit_hint", true, &cfg->show_stream_exit_hint},
    {"stretch_video", false, &cfg->stretch_video},
    {"force_30fps", false, &cfg->force_30fps},
    {"send_actual_start_bitrate", true, &cfg->send_actual_start_bitrate},
    {"clamp_soft_restart_bitrate", true, &cfg->clamp_soft_restart_bitrate},
    {"show_nav_labels", false, &cfg->show_nav_labels},
    {"show_only_paired", false, &cfg->show_only_paired},
  };

  size_t bool_settings_count = sizeof(bool_settings) / sizeof(bool_settings[0]);
  for (size_t i = 0; i < bool_settings_count; i++) {
    MigrationSource source = MIGRATION_SOURCE_NONE;
    parse_bool_setting_with_migration(settings,
                                      parsed,
                                      bool_settings[i].key,
                                      bool_settings[i].default_value,
                                      bool_settings[i].out_value,
                                      &source);
    apply_migration_source(source, migrated_legacy_settings, migrated_root_settings);
  }
}

static void persist_migrated_config_if_needed(VitaChiakiConfig *cfg,
                                              bool migrated_legacy_settings,
                                              bool migrated_root_settings,
                                              bool migrated_resolution_policy) {
  if (!(migrated_legacy_settings || migrated_root_settings || migrated_resolution_policy))
    return;

  if (migrated_root_settings) {
    LOGD("Recovered settings via root-level fallback and rewriting %s",
         CFG_FILENAME);
  } else if (migrated_resolution_policy) {
    LOGD("Applied Vita resolution policy and rewriting %s", CFG_FILENAME);
  } else {
    LOGD("Recovered misplaced settings from legacy config layout; rewriting %s",
         CFG_FILENAME);
  }

  if (!config_serialize(cfg)) {
    LOGE("Failed to persist migrated config to %s; using in-memory settings for this session",
         CFG_FILENAME);
  }
}

void config_parse(VitaChiakiConfig* cfg) {
  bool circle_btn_confirm_default = get_circle_btn_confirm_default();
  config_set_defaults(cfg, circle_btn_confirm_default);

  if (access(CFG_FILENAME, F_OK) != 0)
    goto config_done;

  toml_table_t *parsed = NULL;
  if (!config_parse_file_with_queue_fix(&parsed))
    return;

  if (!config_validate_general_section(parsed)) {
    toml_free(parsed);
    return;
  }

  bool migrated_legacy_settings = false;
  bool migrated_root_settings = false;
  bool migrated_resolution_policy = false;
  toml_table_t *settings = toml_table_in(parsed, "settings");

  parse_basic_settings(cfg, settings);
  parse_resolution_with_migration(cfg,
                                  settings,
                                  parsed,
                                  &migrated_legacy_settings,
                                  &migrated_root_settings,
                                  &migrated_resolution_policy);
  parse_fps_with_migration(cfg,
                           settings,
                           parsed,
                           &migrated_legacy_settings,
                           &migrated_root_settings);

  parse_custom_map_slots(cfg, parsed);
  migrate_legacy_custom_map_if_needed(cfg, parsed);
  normalize_controller_map_id(cfg);

  parse_bool_settings_with_migration(cfg,
                                     settings,
                                     parsed,
                                     circle_btn_confirm_default,
                                     &migrated_legacy_settings,
                                     &migrated_root_settings);
  parse_latency_mode_with_migration(cfg,
                                    settings,
                                    parsed,
                                    &migrated_legacy_settings,
                                    &migrated_root_settings);

  // Security: runtime logging overrides are compile-time gated.
  parse_logging_settings(cfg, parsed);

  config_parse_registered_hosts(cfg, parsed);
  config_parse_manual_hosts(cfg, parsed);
  toml_free(parsed);
  persist_migrated_config_if_needed(cfg,
                                    migrated_legacy_settings,
                                    migrated_root_settings,
                                    migrated_resolution_policy);

config_done:
  LOGD("Config loaded: latency_mode=%s force_30fps=%s send_actual_start_bitrate=%s clamp_soft_restart_bitrate=%s",
       serialize_latency_mode(cfg->latency_mode),
       cfg->force_30fps ? "true" : "false",
       cfg->send_actual_start_bitrate ? "true" : "false",
       cfg->clamp_soft_restart_bitrate ? "true" : "false");
}

void config_free(VitaChiakiConfig* cfg) {
  if (cfg == NULL) {
    return;
  }
  free(cfg->psn_account_id);
  for (int i = 0; i < MAX_MANUAL_HOSTS; i++) {
    if (cfg->manual_hosts[i] != NULL) {
      host_free(cfg->manual_hosts[i]);
    }
  }
  for (int i = 0; i < MAX_REGISTERED_HOSTS; i++) {
    if (cfg->registered_hosts[i] != NULL) {
      host_free(cfg->registered_hosts[i]);
    }
  }
  free(cfg);
}

bool config_serialize(VitaChiakiConfig* cfg) {
  bool downgraded_resolution = false;
  cfg->resolution = normalize_resolution_for_vita(cfg->resolution, &downgraded_resolution);
  if (downgraded_resolution) {
    LOGD("Refusing to persist unsupported resolution on Vita; saving 540p instead");
  }

  FILE *fp = fopen(CFG_FILENAME, "w");
  if (!fp) {
    LOGE("Failed to open %s for writing", CFG_FILENAME);
    return false;
  }
  fprintf(fp, "[general]\nversion = 1\n");

  // Settings
  fprintf(fp, "[settings]\n");
  fprintf(fp, "auto_discovery = %s\n",
          cfg->auto_discovery ? "true" : "false");
  fprintf(fp, "resolution = \"%s\"\n",
          serialize_resolution_preset(cfg->resolution));
  fprintf(fp, "fps = %d\n", cfg->fps);
  if (cfg->psn_account_id) {
    fprintf(fp, "psn_account_id = \"%s\"\n", cfg->psn_account_id);
  }
  fprintf(fp, "controller_map_id = %d\n", cfg->controller_map_id);
  BoolSerializeSpec bool_settings[] = {
    {"circle_btn_confirm", cfg->circle_btn_confirm},
    {"show_latency", cfg->show_latency},
    {"show_network_indicator", cfg->show_network_indicator},
    {"show_stream_exit_hint", cfg->show_stream_exit_hint},
    {"stretch_video", cfg->stretch_video},
    {"force_30fps", cfg->force_30fps},
    {"send_actual_start_bitrate", cfg->send_actual_start_bitrate},
    {"clamp_soft_restart_bitrate", cfg->clamp_soft_restart_bitrate},
    {"show_nav_labels", cfg->show_nav_labels},
    {"show_only_paired", cfg->show_only_paired},
  };
  serialize_bool_settings(fp, bool_settings, sizeof(bool_settings) / sizeof(bool_settings[0]));
  fprintf(fp, "latency_mode = \"%s\"\n", serialize_latency_mode(cfg->latency_mode));

  // Save 3 custom map slots
  for (int slot = 0; slot < 3; slot++) {
    fprintf(fp, "\n[controller_custom_map_%d]\n", slot + 1);
    fprintf(fp, "valid = %s\n", cfg->custom_maps_valid[slot] ? "true" : "false");
    fprintf(fp, "in_l2 = %d\n", cfg->custom_maps[slot].in_l2);
    fprintf(fp, "in_r2 = %d\n", cfg->custom_maps[slot].in_r2);
    for (int i = 0; i < VITAKI_CTRL_IN_COUNT; i++) {
      fprintf(fp, "slot_%d = %d\n", i, cfg->custom_maps[slot].in_out_btn[i]);
    }
  }
  // Only serialize [logging] section in testing/debug builds where runtime config is allowed
#if defined(VITARPS5_ALLOW_RUNTIME_LOGGING_CONFIG) && VITARPS5_ALLOW_RUNTIME_LOGGING_CONFIG
  fprintf(fp, "\n[logging]\n");
  fprintf(fp, "enabled = %s\n", cfg->logging.enabled ? "true" : "false");
  fprintf(fp, "force_error_logging = %s\n",
          cfg->logging.force_error_logging ? "true" : "false");
  fprintf(fp, "profile = \"%s\"\n",
          vita_logging_profile_to_string(cfg->logging.profile));
  fprintf(fp, "path = \"%s\"\n", cfg->logging.path);
  // SCE libc used on Vita ignores %zu, so cast explicitly for portability.
  fprintf(fp, "queue_depth = %lu\n", (unsigned long)cfg->logging.queue_depth);
#endif

  config_serialize_manual_hosts(fp, cfg);
  config_serialize_registered_hosts(fp, cfg);
  bool write_ok = (ferror(fp) == 0) && (fclose(fp) == 0);
  if (!write_ok) {
    LOGE("Failed to flush %s", CFG_FILENAME);
    return false;
  }
  return true;
}
