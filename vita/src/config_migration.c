#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_internal.h"
#include "context.h"
#include "logging.h"

bool config_fix_legacy_queue_depth(void) {
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
  char* data = malloc(size + 1);
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
  data[size] = '\0';

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

static bool parse_root_string_setting(toml_table_t *parsed, const char *key, char **out) {
  toml_datum_t datum = toml_string_in(parsed, key);
  if (!datum.ok)
    return false;
  *out = datum.u.s;
  return true;
}

bool parse_bool_setting_with_migration(toml_table_t *settings,
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

void apply_migration_source(MigrationSource source,
                            bool *migrated_legacy_settings,
                            bool *migrated_root_settings) {
  if (source == MIGRATION_SOURCE_LEGACY_SECTION)
    *migrated_legacy_settings = true;
  else if (source == MIGRATION_SOURCE_ROOT)
    *migrated_root_settings = true;
}

void parse_resolution_with_migration(VitaChiakiConfig *cfg,
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

void parse_fps_with_migration(VitaChiakiConfig *cfg,
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

void parse_latency_mode_with_migration(VitaChiakiConfig *cfg,
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
