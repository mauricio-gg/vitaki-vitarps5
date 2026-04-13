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
#include "token_crypto.h"
#include "util.h"

typedef struct bool_setting_spec_t {
  const char *key;
  bool default_value;
  bool *out_value;
} BoolSettingSpec;

typedef struct bool_serialize_spec_t {
  const char *key;
  bool value;
} BoolSerializeSpec;

static void config_set_defaults(VitaChiakiConfig *cfg, bool circle_btn_confirm_default) {
  cfg->psn_account_id = NULL;
  cfg->psn_oauth_access_token = NULL;
  cfg->psn_oauth_refresh_token = NULL;
  cfg->psn_oauth_expires_at_unix = 0;
  cfg->psn_oauth_device_code_url = NULL;
  cfg->psn_oauth_authorize_url = NULL;
  cfg->psn_oauth_token_url = NULL;
  cfg->psn_oauth_client_id = NULL;
  cfg->psn_oauth_client_secret = NULL;
  cfg->psn_oauth_scope = NULL;
  cfg->psn_oauth_redirect_uri = NULL;
  cfg->psn_client_duid = NULL;
  cfg->psn_remoteplay_enabled = false;
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

static void load_custom_map_slot(toml_table_t *custom_map, ControllerMapStorage *map,
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
    CHIAKI_LOGE(&(context.log), "Failed to parse config due to bad general.version, expected %d.",
                CFG_VERSION);
    return false;
  }

  return true;
}

/*
 * TokenLoadResult — three-way outcome for load_token_from_encrypted.
 *
 * TOKEN_LOAD_ABSENT  : the _enc key is not present in the TOML table.
 *                      The caller may fall back to a legacy plaintext key.
 * TOKEN_LOAD_OK      : the _enc key was present and decryption succeeded.
 *                      The output field has been set; no fallback needed.
 * TOKEN_LOAD_FAILED  : the _enc key was present but decryption failed
 *                      (wrong device / tampering).  The output field has
 *                      been cleared.  The caller MUST NOT fall back to
 *                      the legacy plaintext key — re-auth is required.
 */
typedef enum {
  TOKEN_LOAD_ABSENT = 0,
  TOKEN_LOAD_OK     = 1,
  TOKEN_LOAD_FAILED = 2
} TokenLoadResult;

/*
 * load_token_from_encrypted — Decrypt and store one token from a TOML key.
 *
 * @param settings   TOML table to read from.
 * @param toml_key   Key name for the encrypted blob (e.g. "psn_oauth_access_token_enc").
 * @param out_field  Pointer to the config field to populate; cleared on failure.
 * @param kind       Human-readable label used only for log messages (no token data).
 *
 * Returns TOKEN_LOAD_ABSENT  if the key is not present (benign).
 * Returns TOKEN_LOAD_OK      if the key was present and decryption succeeded.
 * Returns TOKEN_LOAD_FAILED  if the key was present but decryption failed;
 *   a warning has already been logged — caller must not fall back to plaintext.
 */
static TokenLoadResult load_token_from_encrypted(toml_table_t *settings, const char *toml_key,
                                                  char **out_field, const char *kind) {
  toml_datum_t datum = toml_string_in(settings, toml_key);
  if (!datum.ok)
    return TOKEN_LOAD_ABSENT;

  char *decrypted = token_crypto_decrypt(datum.u.s, kind);
  free(datum.u.s);

  if (!decrypted) {
    CHIAKI_LOGW(&(context.log), "PSN token blob decrypt failed; clearing (re-auth required)");
    free(*out_field);
    *out_field = NULL;
    return TOKEN_LOAD_FAILED;
  }

  free(*out_field);
  *out_field = decrypted;
  return TOKEN_LOAD_OK;
}

/*
 * parse_basic_settings — Load scalar settings and PSN OAuth tokens from TOML.
 *
 * @param migrated_plaintext_tokens  Set to true if at least one legacy
 *   plaintext token key was consumed (no _enc key was present), signalling that
 *   the caller should re-serialize the config to persist the encrypted form and
 *   drop the legacy plaintext keys from disk.
 */
static void parse_basic_settings(VitaChiakiConfig *cfg, toml_table_t *settings,
                                  bool *migrated_plaintext_tokens) {
  toml_datum_t datum;
  if (!settings)
    return;

  datum = toml_bool_in(settings, "auto_discovery");
  cfg->auto_discovery = datum.ok ? datum.u.b : true;

  datum = toml_string_in(settings, "psn_account_id");
  if (datum.ok)
    cfg->psn_account_id = datum.u.s;

  /*
   * Token loading strategy (per design in #81):
   *  1. If encrypted keys (*_enc) are present, decrypt them and use the result.
   *     A failed decryption clears the field and forces re-authentication.
   *  2. If encrypted keys are absent but legacy plaintext keys exist, load the
   *     plaintext into memory.  The next config_serialize() will migrate these
   *     to encrypted form and omit the plaintext keys from disk.
   *  3. If neither is present, the fields remain NULL (user must authenticate).
   *
   * Both encrypted and plaintext may coexist during migration — encrypted wins.
   */
  TokenLoadResult access_enc_result =
      load_token_from_encrypted(settings, "psn_oauth_access_token_enc",
                                &cfg->psn_oauth_access_token, "access");
  TokenLoadResult refresh_enc_result =
      load_token_from_encrypted(settings, "psn_oauth_refresh_token_enc",
                                &cfg->psn_oauth_refresh_token, "refresh");

  if (access_enc_result == TOKEN_LOAD_ABSENT) {
    /* _enc key not present — fall back to legacy plaintext key for migration. */
    datum = toml_string_in(settings, "psn_oauth_access_token");
    if (datum.ok) {
      free(cfg->psn_oauth_access_token);
      cfg->psn_oauth_access_token = datum.u.s;
      *migrated_plaintext_tokens = true;
    }
  }
  /* TOKEN_LOAD_FAILED: _enc key was present but decrypt failed.
   * Do NOT fall back to plaintext — the field has been cleared and the user
   * must re-authenticate.  Falling back would silently contradict the warning
   * already logged by load_token_from_encrypted. */

  if (refresh_enc_result == TOKEN_LOAD_ABSENT) {
    /* _enc key not present — fall back to legacy plaintext key for migration. */
    datum = toml_string_in(settings, "psn_oauth_refresh_token");
    if (datum.ok) {
      free(cfg->psn_oauth_refresh_token);
      cfg->psn_oauth_refresh_token = datum.u.s;
      *migrated_plaintext_tokens = true;
    }
  }
  /* TOKEN_LOAD_FAILED: same reasoning as access token above — no plaintext fallback. */

  datum = toml_int_in(settings, "psn_oauth_expires_at_unix");
  if (datum.ok && datum.u.i > 0)
    cfg->psn_oauth_expires_at_unix = (uint64_t)datum.u.i;

  datum = toml_string_in(settings, "psn_oauth_device_code_url");
  if (datum.ok)
    cfg->psn_oauth_device_code_url = datum.u.s;

  datum = toml_string_in(settings, "psn_oauth_authorize_url");
  if (datum.ok)
    cfg->psn_oauth_authorize_url = datum.u.s;

  datum = toml_string_in(settings, "psn_oauth_token_url");
  if (datum.ok)
    cfg->psn_oauth_token_url = datum.u.s;

  datum = toml_string_in(settings, "psn_oauth_client_id");
  if (datum.ok)
    cfg->psn_oauth_client_id = datum.u.s;

  datum = toml_string_in(settings, "psn_oauth_client_secret");
  if (datum.ok)
    cfg->psn_oauth_client_secret = datum.u.s;

  datum = toml_string_in(settings, "psn_oauth_scope");
  if (datum.ok)
    cfg->psn_oauth_scope = datum.u.s;

  datum = toml_string_in(settings, "psn_oauth_redirect_uri");
  if (datum.ok)
    cfg->psn_oauth_redirect_uri = datum.u.s;

  datum = toml_string_in(settings, "psn_client_duid");
  if (datum.ok)
    cfg->psn_client_duid = datum.u.s;

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

static void parse_bool_settings_with_migration(VitaChiakiConfig *cfg, toml_table_t *settings,
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
      {"psn_remoteplay_enabled", false, &cfg->psn_remoteplay_enabled},
      {"enable_logging", false, &cfg->logging.enabled},
  };

  size_t bool_settings_count = sizeof(bool_settings) / sizeof(bool_settings[0]);
  for (size_t i = 0; i < bool_settings_count; i++) {
    MigrationSource source = MIGRATION_SOURCE_NONE;
    parse_bool_setting_with_migration(settings, parsed, bool_settings[i].key,
                                      bool_settings[i].default_value, bool_settings[i].out_value,
                                      &source);
    apply_migration_source(source, migrated_legacy_settings, migrated_root_settings);
  }
}

static void persist_migrated_config_if_needed(VitaChiakiConfig *cfg, bool migrated_legacy_settings,
                                              bool migrated_root_settings,
                                              bool migrated_resolution_policy,
                                              bool migrated_plaintext_tokens) {
  if (!(migrated_legacy_settings || migrated_root_settings || migrated_resolution_policy ||
        migrated_plaintext_tokens))
    return;

  if (migrated_plaintext_tokens) {
    LOGD("Migrating legacy plaintext PSN tokens to encrypted storage; rewriting %s", CFG_FILENAME);
  } else if (migrated_root_settings) {
    LOGD("Recovered settings via root-level fallback and rewriting %s", CFG_FILENAME);
  } else if (migrated_resolution_policy) {
    LOGD("Applied Vita resolution policy and rewriting %s", CFG_FILENAME);
  } else {
    LOGD("Recovered misplaced settings from legacy config layout; rewriting %s", CFG_FILENAME);
  }

  if (!config_serialize(cfg)) {
    LOGE("Failed to persist migrated config to %s; using in-memory settings for this session",
         CFG_FILENAME);
  }
}

void config_parse(VitaChiakiConfig *cfg) {
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
  bool migrated_plaintext_tokens = false;
  toml_table_t *settings = toml_table_in(parsed, "settings");

  parse_basic_settings(cfg, settings, &migrated_plaintext_tokens);
  parse_resolution_with_migration(cfg, settings, parsed, &migrated_legacy_settings,
                                  &migrated_root_settings, &migrated_resolution_policy);
  parse_fps_with_migration(cfg, settings, parsed, &migrated_legacy_settings,
                           &migrated_root_settings);

  parse_custom_map_slots(cfg, parsed);
  migrate_legacy_custom_map_if_needed(cfg, parsed);
  normalize_controller_map_id(cfg);

  parse_bool_settings_with_migration(cfg, settings, parsed, circle_btn_confirm_default,
                                     &migrated_legacy_settings, &migrated_root_settings);
  parse_latency_mode_with_migration(cfg, settings, parsed, &migrated_legacy_settings,
                                    &migrated_root_settings);

  // Security: runtime logging overrides are compile-time gated.
  parse_logging_settings(cfg, parsed);

  config_parse_registered_hosts(cfg, parsed);
  config_parse_manual_hosts(cfg, parsed);
  toml_free(parsed);
  persist_migrated_config_if_needed(cfg, migrated_legacy_settings, migrated_root_settings,
                                    migrated_resolution_policy, migrated_plaintext_tokens);

config_done:
  LOGD(
      "Config loaded: latency_mode=%s force_30fps=%s send_actual_start_bitrate=%s "
      "clamp_soft_restart_bitrate=%s",
      serialize_latency_mode(cfg->latency_mode), cfg->force_30fps ? "true" : "false",
      cfg->send_actual_start_bitrate ? "true" : "false",
      cfg->clamp_soft_restart_bitrate ? "true" : "false");
}

void config_free(VitaChiakiConfig *cfg) {
  if (cfg == NULL) {
    return;
  }
  free(cfg->psn_account_id);
  free(cfg->psn_oauth_access_token);
  free(cfg->psn_oauth_refresh_token);
  free(cfg->psn_oauth_device_code_url);
  free(cfg->psn_oauth_authorize_url);
  free(cfg->psn_oauth_token_url);
  free(cfg->psn_oauth_client_id);
  free(cfg->psn_oauth_client_secret);
  free(cfg->psn_oauth_scope);
  free(cfg->psn_oauth_redirect_uri);
  free(cfg->psn_client_duid);
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

bool config_serialize(VitaChiakiConfig *cfg) {
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
  fprintf(fp, "auto_discovery = %s\n", cfg->auto_discovery ? "true" : "false");
  fprintf(fp, "resolution = \"%s\"\n", serialize_resolution_preset(cfg->resolution));
  fprintf(fp, "fps = %d\n", cfg->fps);
  if (cfg->psn_account_id) {
    fprintf(fp, "psn_account_id = \"%s\"\n", cfg->psn_account_id);
  }

#ifdef VITARPS5_PLAINTEXT_TOKEN_STORAGE
  /*
   * Debug escape hatch: VITARPS5_PLAINTEXT_TOKEN_STORAGE=1 writes legacy
   * plaintext keys INSTEAD OF encrypted ones.  Never set this in production.
   * Encrypted keys are intentionally omitted here so the debug output is clean.
   */
  if (cfg->psn_oauth_access_token) {
    fprintf(fp, "psn_oauth_access_token = \"%s\"\n", cfg->psn_oauth_access_token);
  }
  if (cfg->psn_oauth_refresh_token) {
    fprintf(fp, "psn_oauth_refresh_token = \"%s\"\n", cfg->psn_oauth_refresh_token);
  }
  /* Persist expiry alongside plaintext tokens. */
  if (cfg->psn_oauth_expires_at_unix > 0) {
    fprintf(fp, "psn_oauth_expires_at_unix = %llu\n",
            (unsigned long long)cfg->psn_oauth_expires_at_unix);
  }
#else
  /*
   * Production path: persist tokens encrypted-at-rest.
   *
   * Only write encrypted keys when a non-empty token is present.  If a field
   * is NULL or empty (e.g. after logout), the corresponding *_enc key is
   * omitted entirely so no stale ciphertext remains on disk.
   *
   * Expiry is non-sensitive and stored in plaintext alongside the tokens so
   * the auth state can be evaluated without decryption.
   */
  bool tokens_persisted = false;

  if (cfg->psn_oauth_access_token && cfg->psn_oauth_access_token[0] != '\0') {
    char *enc = token_crypto_encrypt(cfg->psn_oauth_access_token, "access");
    if (enc) {
      fprintf(fp, "psn_oauth_access_token_enc = \"%s\"\n", enc);
      free(enc);
      tokens_persisted = true;
    } else {
      CHIAKI_LOGW(&(context.log),
                  "PSN access token encryption failed; token not persisted this session");
    }
  }

  if (cfg->psn_oauth_refresh_token && cfg->psn_oauth_refresh_token[0] != '\0') {
    char *enc = token_crypto_encrypt(cfg->psn_oauth_refresh_token, "refresh");
    if (enc) {
      fprintf(fp, "psn_oauth_refresh_token_enc = \"%s\"\n", enc);
      free(enc);
      tokens_persisted = true;
    } else {
      CHIAKI_LOGW(&(context.log),
                  "PSN refresh token encryption failed; token not persisted this session");
    }
  }

  /* Expiry is persisted only when at least one token was successfully written.
   * A stale expiry with no ciphertext would produce a broken auth state. */
  if (tokens_persisted && cfg->psn_oauth_expires_at_unix > 0) {
    fprintf(fp, "psn_oauth_expires_at_unix = %llu\n",
            (unsigned long long)cfg->psn_oauth_expires_at_unix);
  }
#endif /* VITARPS5_PLAINTEXT_TOKEN_STORAGE */
  if (cfg->psn_oauth_device_code_url) {
    fprintf(fp, "psn_oauth_device_code_url = \"%s\"\n", cfg->psn_oauth_device_code_url);
  }
  if (cfg->psn_oauth_authorize_url) {
    fprintf(fp, "psn_oauth_authorize_url = \"%s\"\n", cfg->psn_oauth_authorize_url);
  }
  if (cfg->psn_oauth_token_url) {
    fprintf(fp, "psn_oauth_token_url = \"%s\"\n", cfg->psn_oauth_token_url);
  }
  if (cfg->psn_oauth_client_id) {
    fprintf(fp, "psn_oauth_client_id = \"%s\"\n", cfg->psn_oauth_client_id);
  }
  if (cfg->psn_oauth_client_secret) {
    fprintf(fp, "psn_oauth_client_secret = \"%s\"\n", cfg->psn_oauth_client_secret);
  }
  if (cfg->psn_oauth_scope) {
    fprintf(fp, "psn_oauth_scope = \"%s\"\n", cfg->psn_oauth_scope);
  }
  if (cfg->psn_oauth_redirect_uri) {
    fprintf(fp, "psn_oauth_redirect_uri = \"%s\"\n", cfg->psn_oauth_redirect_uri);
  }
  if (cfg->psn_client_duid) {
    fprintf(fp, "psn_client_duid = \"%s\"\n", cfg->psn_client_duid);
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
      {"psn_remoteplay_enabled", cfg->psn_remoteplay_enabled},
      {"enable_logging", cfg->logging.enabled},
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
  fprintf(fp, "force_error_logging = %s\n", cfg->logging.force_error_logging ? "true" : "false");
  fprintf(fp, "profile = \"%s\"\n", vita_logging_profile_to_string(cfg->logging.profile));
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
