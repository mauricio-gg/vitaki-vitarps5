#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <string.h>
#include <tomlc99/toml.h>
#include <chiaki/base64.h>

#include "config.h"
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

void zero_pad(char* buf, size_t size) {
  bool pad = false;
  for (int i=0; i < size; i++) {
    if (buf[i] == '\0') {
      pad = true;
    }
    if (pad) {
      buf[i] = '\0';
    }
  }
  if (!pad) {
    buf[size-1] = '\0';
  }
}

ChiakiVideoResolutionPreset parse_resolution_preset(const char* preset) {
  if (!preset)
    return CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
  if (strcmp(preset, "360p") == 0)
    return CHIAKI_VIDEO_RESOLUTION_PRESET_360p;
  if (strcmp(preset, "720p") == 0)
    return CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
  if (strcmp(preset, "1080p") == 0)
    return CHIAKI_VIDEO_RESOLUTION_PRESET_1080p;
  return CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
}

static ChiakiVideoResolutionPreset normalize_resolution_for_vita(ChiakiVideoResolutionPreset preset,
                                                                 bool *was_downgraded) {
  if (was_downgraded)
    *was_downgraded = false;
  if (preset == CHIAKI_VIDEO_RESOLUTION_PRESET_1080p ||
      preset == CHIAKI_VIDEO_RESOLUTION_PRESET_720p) {
    if (was_downgraded)
      *was_downgraded = true;
    return CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
  }
  return preset;
}

VitaChiakiLatencyMode parse_latency_mode(const char* mode) {
  if (!mode)
    return VITA_LATENCY_MODE_BALANCED;
  if (strcmp(mode, "ultra_low") == 0)
    return VITA_LATENCY_MODE_ULTRA_LOW;
  if (strcmp(mode, "low") == 0)
    return VITA_LATENCY_MODE_LOW;
  if (strcmp(mode, "high") == 0)
    return VITA_LATENCY_MODE_HIGH;
  if (strcmp(mode, "max") == 0)
    return VITA_LATENCY_MODE_MAX;
  return VITA_LATENCY_MODE_BALANCED;
}

const char* serialize_latency_mode(VitaChiakiLatencyMode mode) {
  switch (mode) {
    case VITA_LATENCY_MODE_ULTRA_LOW: return "ultra_low";
    case VITA_LATENCY_MODE_LOW: return "low";
    case VITA_LATENCY_MODE_HIGH: return "high";
    case VITA_LATENCY_MODE_MAX: return "max";
    case VITA_LATENCY_MODE_BALANCED:
    default:
      return "balanced";
  }
}

ChiakiTarget parse_target(char* target_name) {
  if (strcmp("ps4_unknown", target_name) == 0) {
    return CHIAKI_TARGET_PS4_UNKNOWN;
  } else if (strcmp("ps4_8", target_name) == 0) {
    return CHIAKI_TARGET_PS4_8;
  } else if (strcmp("ps4_9", target_name) == 0) {
    return CHIAKI_TARGET_PS4_9;
  } else if (strcmp("ps4_10", target_name) == 0) {
    return CHIAKI_TARGET_PS4_10;
  } else if (strcmp("ps5_unknown", target_name) == 0) {
    return CHIAKI_TARGET_PS5_UNKNOWN;
  } else if (strcmp("ps5_1", target_name) == 0) {
    return CHIAKI_TARGET_PS5_1;
  }
  return CHIAKI_TARGET_PS4_UNKNOWN;
}

bool get_circle_btn_confirm_default() {
  // Check system settings to see if circle should be select instead of cross
  // (should be true on Japanese vitas and false elsewhere).

	int button_assign = -1;
	int ret = 0;
	ret = sceRegMgrGetKeyInt("/CONFIG/SYSTEM", "button_assign", &button_assign);
  if (ret < 0) {
    // Failed to determine. Just return false.
    return false;
  }

  // 0 => circle select; 1 => cross select; other values invalid (so default to false)
  return (button_assign == 0);
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

void config_parse(VitaChiakiConfig* cfg) {
  cfg->psn_account_id = NULL;
  cfg->auto_discovery = true;
  cfg->resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
  cfg->fps = CHIAKI_VIDEO_FPS_PRESET_30;
  cfg->controller_map_id = VITAKI_CONTROLLER_MAP_CUSTOM_1;  // Default to Custom 1
  for (int i = 0; i < 3; i++) {
    controller_map_storage_set_defaults(&cfg->custom_maps[i]);
    cfg->custom_maps_valid[i] = false;
  }
  cfg->show_latency = false;  // Default: latency display disabled
  cfg->show_network_indicator = true;
  cfg->latency_mode = VITA_LATENCY_MODE_BALANCED;
  cfg->stretch_video = false;
  cfg->force_30fps = false;
  cfg->send_actual_start_bitrate = true;
  cfg->clamp_soft_restart_bitrate = true;
  cfg->show_nav_labels = false;  // Default: no text labels below nav icons
  vita_logging_config_set_defaults(&cfg->logging);

  bool circle_btn_confirm_default = get_circle_btn_confirm_default();
  cfg->circle_btn_confirm = circle_btn_confirm_default;

  if (access(CFG_FILENAME, F_OK) == 0) {
    char errbuf[200];
    bool attempted_queue_fix = false;
    toml_table_t* parsed = NULL;
    while (true) {
      FILE* fp = fopen(CFG_FILENAME, "r");
      if (!fp) {
        CHIAKI_LOGE(&(context.log), "Failed to open %s for reading", CFG_FILENAME);
        return;
      }
      parsed = toml_parse_file(fp, errbuf, sizeof(errbuf));
      fclose(fp);
      if (parsed) {
        break;
      }
      if (attempted_queue_fix || !config_fix_legacy_queue_depth()) {
        CHIAKI_LOGE(&(context.log), "Failed to parse config due to illegal TOML: %s", errbuf);
        return;
      }
      attempted_queue_fix = true;
      CHIAKI_LOGW(&(context.log),
                  "Recovered invalid logging.queue_depth entry, resetting to default (%u)",
                  VITA_LOG_DEFAULT_QUEUE_DEPTH);
    }
    toml_table_t* general = toml_table_in(parsed, "general");
    if (!general) {
      CHIAKI_LOGE(&(context.log), "Failed to parse config due to missing [general] section");
      toml_free(parsed);
      return;
    }
    toml_datum_t datum;
    datum = toml_int_in(general, "version");
    if (!datum.ok || datum.u.i != CFG_VERSION) {
      CHIAKI_LOGE(&(context.log), "Failed to parse config due to bad general.version, expected %d.", CFG_VERSION);
      toml_free(parsed);
      return;
    }

    bool migrated_legacy_settings = false;
    bool migrated_root_settings = false;
    bool migrated_resolution_policy = false;
    toml_table_t* settings = toml_table_in(parsed, "settings");
    if (settings) {
      datum = toml_bool_in(settings, "auto_discovery");
      cfg->auto_discovery = datum.ok ? datum.u.b : true;
    }

    char *str_value = NULL;
    MigrationSource source = MIGRATION_SOURCE_NONE;
    if (settings) {
      datum = toml_string_in(settings, "resolution");
      if (datum.ok) {
        str_value = datum.u.s;
      }
    }
    if (!str_value && parse_legacy_string_setting(parsed, "resolution", &str_value))
      source = MIGRATION_SOURCE_LEGACY_SECTION;
    if (!str_value && parse_root_string_setting(parsed, "resolution", &str_value))
      source = MIGRATION_SOURCE_ROOT;
    if (str_value) {
      // str_value comes from toml_string_in()/parse_legacy_string_setting(); caller owns it.
      cfg->resolution = parse_resolution_preset(str_value);
      free(str_value);
      if (source == MIGRATION_SOURCE_LEGACY_SECTION)
        migrated_legacy_settings = true;
      else if (source == MIGRATION_SOURCE_ROOT)
        migrated_root_settings = true;
    }
    bool downgraded_resolution = false;
    cfg->resolution = normalize_resolution_for_vita(cfg->resolution, &downgraded_resolution);
    if (downgraded_resolution) {
      LOGD("Resolution is not supported on Vita; downgrading to 540p");
      migrated_resolution_policy = true;
    }

    int fps_value = 30;
    bool fps_found = false;
    source = MIGRATION_SOURCE_NONE;
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
    if (fps_found) {
      if (fps_value == 60) {
        cfg->fps = CHIAKI_VIDEO_FPS_PRESET_60;
      } else {
        if (fps_value != 30) {
          LOGD("Unsupported fps value %d in config (supported: 30 or 60); defaulting to 30", fps_value);
        }
        cfg->fps = CHIAKI_VIDEO_FPS_PRESET_30;
      }
      if (source == MIGRATION_SOURCE_LEGACY_SECTION)
        migrated_legacy_settings = true;
      else if (source == MIGRATION_SOURCE_ROOT)
        migrated_root_settings = true;
    }

    if (settings) {
      datum = toml_string_in(settings, "psn_account_id");
      if (datum.ok) {
        cfg->psn_account_id = datum.u.s;
      }
    }

    if (settings) {
      datum = toml_int_in(settings, "controller_map_id");
      if (datum.ok) {
        cfg->controller_map_id = datum.u.i;
      }
    }

    // Load 3 custom map slots
    for (int slot = 0; slot < 3; slot++) {
      char section_name[32];
      snprintf(section_name, sizeof(section_name), "controller_custom_map_%d", slot + 1);
      toml_table_t* custom_map = toml_table_in(parsed, section_name);
      if (custom_map) {
        datum = toml_bool_in(custom_map, "valid");
        cfg->custom_maps_valid[slot] = datum.ok ? datum.u.b : false;

        if (cfg->custom_maps_valid[slot]) {
          for (int i = 0; i < VITAKI_CTRL_IN_COUNT; i++) {
            char key[24];
            snprintf(key, sizeof(key), "slot_%d", i);
            datum = toml_int_in(custom_map, key);
            cfg->custom_maps[slot].in_out_btn[i] = datum.ok ? (int)datum.u.i : 0;
          }
          datum = toml_int_in(custom_map, "in_l2");
          cfg->custom_maps[slot].in_l2 = datum.ok ? (int)datum.u.i : VITAKI_CTRL_IN_NONE;
          datum = toml_int_in(custom_map, "in_r2");
          cfg->custom_maps[slot].in_r2 = datum.ok ? (int)datum.u.i : VITAKI_CTRL_IN_NONE;
        }
      }
    }

    // Migration: Check for old [controller_custom_map] section (pre-Custom 1/2/3)
    // If found and slot 0 is empty, migrate it to Custom 1
    if (!cfg->custom_maps_valid[0]) {
      toml_table_t* old_custom_map = toml_table_in(parsed, "controller_custom_map");
      if (old_custom_map) {
        datum = toml_bool_in(old_custom_map, "valid");
        bool old_valid = datum.ok ? datum.u.b : false;
        if (old_valid) {
          // Migrate old custom map to Custom 1 (slot 0)
          for (int i = 0; i < VITAKI_CTRL_IN_COUNT; i++) {
            char key[24];
            snprintf(key, sizeof(key), "slot_%d", i);
            datum = toml_int_in(old_custom_map, key);
            cfg->custom_maps[0].in_out_btn[i] = datum.ok ? (int)datum.u.i : 0;
          }
          datum = toml_int_in(old_custom_map, "in_l2");
          cfg->custom_maps[0].in_l2 = datum.ok ? (int)datum.u.i : VITAKI_CTRL_IN_NONE;
          datum = toml_int_in(old_custom_map, "in_r2");
          cfg->custom_maps[0].in_r2 = datum.ok ? (int)datum.u.i : VITAKI_CTRL_IN_NONE;
          cfg->custom_maps_valid[0] = true;
        }
      }
    }

    // Migration: Convert old controller_map_id values to Custom 1
    // Old values: 0, 1, 3, 4, 25, 99 -> all map to Custom 1 (201)
    if (cfg->controller_map_id != VITAKI_CONTROLLER_MAP_CUSTOM_1 &&
        cfg->controller_map_id != VITAKI_CONTROLLER_MAP_CUSTOM_2 &&
        cfg->controller_map_id != VITAKI_CONTROLLER_MAP_CUSTOM_3) {
      cfg->controller_map_id = VITAKI_CONTROLLER_MAP_CUSTOM_1;
    }

    if (parse_bool_setting_with_migration(settings, parsed,
                                          "circle_btn_confirm", circle_btn_confirm_default,
                                          &cfg->circle_btn_confirm, &source)) {
      if (source == MIGRATION_SOURCE_LEGACY_SECTION)
        migrated_legacy_settings = true;
      else if (source == MIGRATION_SOURCE_ROOT)
        migrated_root_settings = true;
    }

    if (parse_bool_setting_with_migration(settings, parsed,
                                          "show_latency", false,
                                          &cfg->show_latency, &source)) {
      if (source == MIGRATION_SOURCE_LEGACY_SECTION)
        migrated_legacy_settings = true;
      else if (source == MIGRATION_SOURCE_ROOT)
        migrated_root_settings = true;
    }

    if (parse_bool_setting_with_migration(settings, parsed,
                                          "show_network_indicator", true,
                                          &cfg->show_network_indicator, &source)) {
      if (source == MIGRATION_SOURCE_LEGACY_SECTION)
        migrated_legacy_settings = true;
      else if (source == MIGRATION_SOURCE_ROOT)
        migrated_root_settings = true;
    }

    if (parse_bool_setting_with_migration(settings, parsed,
                                          "stretch_video", false,
                                          &cfg->stretch_video, &source)) {
      if (source == MIGRATION_SOURCE_LEGACY_SECTION)
        migrated_legacy_settings = true;
      else if (source == MIGRATION_SOURCE_ROOT)
        migrated_root_settings = true;
    }

    if (parse_bool_setting_with_migration(settings, parsed,
                                          "force_30fps", false,
                                          &cfg->force_30fps, &source)) {
      if (source == MIGRATION_SOURCE_LEGACY_SECTION)
        migrated_legacy_settings = true;
      else if (source == MIGRATION_SOURCE_ROOT)
        migrated_root_settings = true;
    }

    if (parse_bool_setting_with_migration(settings, parsed,
                                          "send_actual_start_bitrate", true,
                                          &cfg->send_actual_start_bitrate, &source)) {
      if (source == MIGRATION_SOURCE_LEGACY_SECTION)
        migrated_legacy_settings = true;
      else if (source == MIGRATION_SOURCE_ROOT)
        migrated_root_settings = true;
    }

    if (parse_bool_setting_with_migration(settings, parsed,
                                          "clamp_soft_restart_bitrate", true,
                                          &cfg->clamp_soft_restart_bitrate, &source)) {
      if (source == MIGRATION_SOURCE_LEGACY_SECTION)
        migrated_legacy_settings = true;
      else if (source == MIGRATION_SOURCE_ROOT)
        migrated_root_settings = true;
    }

    if (parse_bool_setting_with_migration(settings, parsed,
                                          "show_nav_labels", false,
                                          &cfg->show_nav_labels, &source)) {
      if (source == MIGRATION_SOURCE_LEGACY_SECTION)
        migrated_legacy_settings = true;
      else if (source == MIGRATION_SOURCE_ROOT)
        migrated_root_settings = true;
    }

    str_value = NULL;
    source = MIGRATION_SOURCE_NONE;
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
      // str_value comes from toml_string_in()/parse_legacy_string_setting(); caller owns it.
      cfg->latency_mode = parse_latency_mode(str_value);
      free(str_value);
      if (source == MIGRATION_SOURCE_LEGACY_SECTION)
        migrated_legacy_settings = true;
      else if (source == MIGRATION_SOURCE_ROOT)
        migrated_root_settings = true;
    } else {
      cfg->latency_mode = VITA_LATENCY_MODE_BALANCED;
    }

    // Security: Only allow runtime TOML to override logging settings if explicitly enabled
    // In production builds, this is disabled to prevent accidental logging activation
#if defined(VITARPS5_ALLOW_RUNTIME_LOGGING_CONFIG) && VITARPS5_ALLOW_RUNTIME_LOGGING_CONFIG
    toml_table_t* logging = toml_table_in(parsed, "logging");
    if (logging) {
      datum = toml_bool_in(logging, "enabled");
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
    }
#else
    // Production build: Ignore [logging] section in TOML entirely
    // Compiled defaults are immutable at runtime for security
    (void)toml_table_in(parsed, "logging");  // Acknowledge section exists but don't parse
#endif

    toml_array_t* regist_hosts = toml_array_in(parsed, "registered_hosts");
    if (regist_hosts && toml_array_kind(regist_hosts) == 't') {
      int num_rhosts = toml_array_nelem(regist_hosts);
      for (int i=0; i < MIN(MAX_NUM_HOSTS, num_rhosts); i++) {
        VitaChiakiHost* host = malloc(sizeof(VitaChiakiHost));
        ChiakiRegisteredHost* rstate = malloc(sizeof(ChiakiRegisteredHost));
        LOGD("Assigning registered state: 0x%x", rstate);
        host->registered_state = rstate;
        toml_table_t* host_cfg = toml_table_at(regist_hosts, i);
        datum = toml_string_in(host_cfg, "server_mac");
        if (datum.ok) {
          parse_b64(datum.u.s, host->server_mac, 6);
#ifndef NDEBUG
          printf("MAC %X%X%X%X%X%X\n", host->server_mac[0], host->server_mac[1], host->server_mac[2],
                          host->server_mac[3], host->server_mac[4], host->server_mac[5]);
#endif
          memcpy(&rstate->server_mac, &(host->server_mac), 6);
          free(datum.u.s);
        }
        datum = toml_string_in(host_cfg, "server_nickname");
        if (datum.ok) {
          strncpy(rstate->server_nickname, datum.u.s, sizeof(rstate->server_nickname));
          rstate->server_nickname[sizeof(rstate->server_nickname)-1] = '\0';
          free(datum.u.s);
        }
        datum = toml_string_in(host_cfg, "target");
        if (datum.ok) {
          rstate->target = parse_target(datum.u.s);
          host->target = parse_target(datum.u.s);
          free(datum.u.s);
        }
        datum = toml_string_in(host_cfg, "rp_key");
        if (datum.ok) {
#ifndef NDEBUG
          printf("after rp %s\n", datum.u.s);
#endif
          parse_b64(datum.u.s, rstate->rp_key, 0x10);
#ifndef NDEBUG
          hexdump(rstate->rp_key, (size_t)0x10);
#endif
          free(datum.u.s);
        }
        datum = toml_int_in(host_cfg, "rp_key_type");
        if (datum.ok) {
          rstate->rp_key_type = datum.u.i;
        }
        datum = toml_string_in(host_cfg, "rp_regist_key");
        if (datum.ok) {
          strncpy(rstate->rp_regist_key, datum.u.s, sizeof(rstate->rp_regist_key));
          zero_pad(rstate->rp_regist_key, sizeof(rstate->rp_regist_key));
          free(datum.u.s);
        }
        // datum = toml_string_in(host_cfg, "ap_bssid");
        // if (datum.ok) {
        //   strncpy(rstate->ap_bssid, datum.u.s, sizeof(rstate->ap_bssid));
        //   zero_pad(rstate->ap_bssid, sizeof(rstate->ap_bssid));
        //   free(datum.u.s);
        // }
        // datum = toml_string_in(host_cfg, "ap_key");
        // if (datum.ok) {
        //   strncpy(rstate->ap_key, datum.u.s, sizeof(rstate->ap_key));
        //   zero_pad(rstate->ap_key, sizeof(rstate->ap_key));
        //   free(datum.u.s);
        // }
        // datum = toml_string_in(host_cfg, "ap_ssid");
        // if (datum.ok) {
        //   strncpy(rstate->ap_ssid, datum.u.s, sizeof(rstate->ap_ssid));
        //   zero_pad(rstate->ap_ssid, sizeof(rstate->ap_ssid));
        //   free(datum.u.s);
        // }
        // datum = toml_string_in(host_cfg, "ap_name");
        // if (datum.ok) {
        //   strncpy(rstate->ap_name, datum.u.s, sizeof(rstate->ap_name));
        //   zero_pad(rstate->ap_name, sizeof(rstate->ap_name));
        //   free(datum.u.s);
        // }
        cfg->registered_hosts[i] = host;
        cfg->num_registered_hosts++;
      }
    }

    toml_array_t* manual_hosts = toml_array_in(parsed, "manual_hosts");
    if (manual_hosts && toml_array_kind(manual_hosts) == 't') {
      int num_mhosts = toml_array_nelem(manual_hosts);
      LOGD("Found %d manual hosts", num_mhosts);
      for (int i=0; i < MIN(MAX_NUM_HOSTS, num_mhosts) ; i++) {
        VitaChiakiHost* host = NULL;

        bool has_mac = false;
        bool has_hostname = false;
        bool has_registration = false;

        toml_table_t* host_cfg = toml_table_at(manual_hosts, i);
        datum = toml_string_in(host_cfg, "server_mac");
        uint8_t server_mac[6];
        if (datum.ok) {
          // We have a MAC for the manual host, try to find corresponding
          // registered host
          parse_b64(datum.u.s, server_mac, sizeof(server_mac));
          has_mac = true;
          free(datum.u.s);
          for (int hidx=0; hidx < cfg->num_registered_hosts; hidx++) {
            uint8_t* candidate_mac = cfg->registered_hosts[hidx]->server_mac;
            if (candidate_mac) {
              if (mac_addrs_match(&server_mac, candidate_mac)) {
                // copy registered host (TODO for the registered_state, should we use a pointer instead?)
                host = malloc(sizeof(VitaChiakiHost));
                copy_host(host, cfg->registered_hosts[hidx], false);
                host->type = REGISTERED;
                has_registration = true;
                break;
              }
            }
          }
        }
        if (!host) {
          // No corresponding registered host found. Don't save.
          CHIAKI_LOGW(&(context.log), "Manual host missing registered host.");
          continue;
        }

        host->type |= MANUALLY_ADDED;
        host->type &= ~DISCOVERED; // ensure discovered is off

        datum = toml_string_in(host_cfg, "hostname");
        if (datum.ok) {
          host->hostname = datum.u.s;
          has_hostname = true;
        }

        if (has_hostname && has_mac) {
          size_t slot = cfg->num_manual_hosts;
          if (slot >= MAX_NUM_HOSTS) {
            CHIAKI_LOGW(&(context.log), "Manual host capacity reached, skipping entry %d", i);
            host_free(host);
            continue;
          }
          cfg->manual_hosts[slot] = host;
          cfg->num_manual_hosts++;
        } else {
          CHIAKI_LOGW(&(context.log), "Failed to parse manual host due to missing hostname or mac.");
          free(host);
        }
      }
    }
    toml_free(parsed);
    if (migrated_legacy_settings || migrated_root_settings || migrated_resolution_policy) {
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
  }
}

void config_free(VitaChiakiConfig* cfg) {
  if (cfg == NULL) {
    return;
  }
  free(cfg->psn_account_id);
  for (int i = 0; i < MAX_NUM_HOSTS; i++) {
    if (cfg->manual_hosts[i] != NULL) {
      host_free(cfg->manual_hosts[i]);
    }
    if (cfg->registered_hosts[i] != NULL) {
      host_free(cfg->registered_hosts[i]);
    }
  }
  free(cfg);
}

const char* serialize_resolution_preset(ChiakiVideoResolutionPreset preset) {
  switch (preset) {
    case CHIAKI_VIDEO_RESOLUTION_PRESET_360p:
      return "360p";
    case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
      return "720p";
    case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p:
      return "1080p";
    case CHIAKI_VIDEO_RESOLUTION_PRESET_540p:
    default:
      return "540p";
  }
}

void serialize_b64(FILE* fp, char* field_name, uint8_t* val, size_t len) {
  bool all_zero = true;
  for (size_t i=0; i < len; i++) {
    if (val[i] != 0) {
      all_zero = false;
      break;
    }
  }
  if (all_zero) {
    return;
  }
  fprintf(fp, "%s = \"", field_name);
  // for (size_t i=0; i < len; i++) {
  //   fprintf(fp, "%02X", val[i]);
  // }
  char b64[get_base64_size(len) + 1];
  memset(b64, 0, get_base64_size(len) + 1);
  chiaki_base64_encode(val, len, b64, get_base64_size(len));
  fprintf(fp, "%s\"\n", b64);
}

void serialize_target(FILE* fp, char* field_name, ChiakiTarget* target) {
  fprintf(fp, "%s = \"", field_name);
  switch (*target) {
    case CHIAKI_TARGET_PS4_UNKNOWN:
      fprintf(fp, "ps4_unknown");
      break;
    case CHIAKI_TARGET_PS4_8:
      fprintf(fp, "ps4_8");
      break;
    case CHIAKI_TARGET_PS4_9:
      fprintf(fp, "ps4_9");
      break;
    case CHIAKI_TARGET_PS4_10:
      fprintf(fp, "ps4_10");
      break;
    case CHIAKI_TARGET_PS5_UNKNOWN:
      fprintf(fp, "ps5_unknown");
      break;
    case CHIAKI_TARGET_PS5_1:
      fprintf(fp, "ps5_1");
      break;
  }
  fprintf(fp, "\"\n");
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
  fprintf(fp, "circle_btn_confirm = %s\n",
          cfg->circle_btn_confirm ? "true" : "false");
  fprintf(fp, "show_latency = %s\n",
          cfg->show_latency ? "true" : "false");
  fprintf(fp, "show_network_indicator = %s\n",
          cfg->show_network_indicator ? "true" : "false");
  fprintf(fp, "stretch_video = %s\n",
          cfg->stretch_video ? "true" : "false");
  fprintf(fp, "force_30fps = %s\n",
          cfg->force_30fps ? "true" : "false");
  fprintf(fp, "send_actual_start_bitrate = %s\n",
          cfg->send_actual_start_bitrate ? "true" : "false");
  fprintf(fp, "clamp_soft_restart_bitrate = %s\n",
          cfg->clamp_soft_restart_bitrate ? "true" : "false");
  fprintf(fp, "show_nav_labels = %s\n",
          cfg->show_nav_labels ? "true" : "false");
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

  for (int i = 0; i < cfg->num_manual_hosts; i++) {
    VitaChiakiHost* host = cfg->manual_hosts[i];
    if (!host) {
      LOGD("config_serialize: manual host slot %d is NULL, skipping", i);
      continue;
    }
    uint8_t* mac = NULL;
    for (int m = 0; m < 6; m++) {
      if (host->server_mac[m] != 0) {
        mac = host->server_mac;
        break;
      }
    }
    if (!mac || !host->hostname) {
      LOGD("config_serialize: manual host slot %d missing data, skipping", i);
      continue;
    }
    fprintf(fp, "\n\n[[manual_hosts]]\n");
    fprintf(fp, "hostname = \"%s\"\n", host->hostname);
    serialize_b64(fp, "server_mac", mac, 6);
  }

  for (int i = 0; i < cfg->num_registered_hosts; i++) {
    VitaChiakiHost *host = cfg->registered_hosts[i];
    ChiakiRegisteredHost* rhost = host ? host->registered_state : NULL;
    if (!host || !rhost) {
      LOGD("config_serialize: registered host slot %d missing data, skipping", i);
      continue;
    }
    fprintf(fp, "\n\n[[registered_hosts]]\n");
    serialize_b64(fp, "server_mac", rhost->server_mac, 6);
    fprintf(fp, "server_nickname = \"%s\"\n", rhost->server_nickname);
    serialize_target(fp, "target", &rhost->target);
    serialize_b64(fp, "rp_key", rhost->rp_key, 0x10);
    fprintf(fp, "rp_key_type = %d\n", rhost->rp_key_type);
    fprintf(fp, "rp_regist_key = \"%s\"\n", rhost->rp_regist_key);
    // fprintf(fp, "ap_bssid = \"%s\"\n", rhost->ap_bssid);
    // fprintf(fp, "ap_key = \"%s\"\n", rhost->ap_key);
    // fprintf(fp, "ap_ssid = \"%s\"\n", rhost->ap_ssid);
    // fprintf(fp, "ap_name = \"%s\"\n", rhost->ap_name);
  }
  bool write_ok = (ferror(fp) == 0) && (fclose(fp) == 0);
  if (!write_ok) {
    LOGE("Failed to flush %s", CFG_FILENAME);
    return false;
  }
  return true;
}
