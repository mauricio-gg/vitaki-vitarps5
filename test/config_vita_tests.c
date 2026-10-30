#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "chiaki/base64.h"
#include "config.h"
#include "context.h"
#include "token_crypto.h"

VitaChiakiContext context = {0};

void chiaki_log(ChiakiLog *log, ChiakiLogLevel level, const char *fmt, ...) {
  (void)log;
  (void)level;
  (void)fmt;
}

/* Stand-in for the real host_free (host.c). The real one frees registered_state and leaves a zeroed
 * husk because of stale pointers elsewhere in the app; no test here holds such pointers, so this one
 * frees the struct. hostname is an inline array in VitaChiakiHost, not a heap pointer. */
void host_free(VitaChiakiHost *host) {
  if (!host)
    return;
  free(host->registered_state);
  free(host);
}

int host_register(VitaChiakiHost *host, int pin) {
  (void)host;
  (void)pin;
  return 0;
}

int host_wakeup(VitaChiakiHost *host) {
  (void)host;
  return 0;
}

int host_stream(VitaChiakiHost *host) {
  (void)host;
  return 0;
}

void host_cancel_stream_request(void) {}

bool mac_addrs_match(MacAddr *a, MacAddr *b) {
  return memcmp(a, b, sizeof(MacAddr)) == 0;
}

bool save_manual_host(VitaChiakiHost *rhost, char *new_hostname) {
  (void)rhost;
  (void)new_hostname;
  return true;
}

void delete_manual_host(VitaChiakiHost *mhost) {
  (void)mhost;
}

void update_context_hosts(void) {}

int count_manual_hosts_of_console(VitaChiakiHost *host) {
  (void)host;
  return 0;
}

void copy_host(VitaChiakiHost *h_dest, VitaChiakiHost *h_src, bool copy_hostname) {
  (void)copy_hostname;
  if (h_dest && h_src)
    memcpy(h_dest, h_src, sizeof(*h_dest));
}

void copy_host_registered_state(ChiakiRegisteredHost *rstate_dest, const ChiakiRegisteredHost *rstate_src) {
  if (rstate_dest && rstate_src)
    memcpy(rstate_dest, rstate_src, sizeof(*rstate_dest));
}

/* Same as util.c: the pairing-key tests need the real decode to compare key bytes. */
void parse_b64(const char *val, uint8_t *dest, size_t len) {
  memset(dest, 0, len);
  if (!val || !val[0] || !dest || len == 0)
    return;
  chiaki_base64_decode(val, strlen(val), dest, &len);
}

void parse_mac(const char *mac_str, uint8_t *mac_dest) {
  (void)mac_str;
  memset(mac_dest, 0, 6);
}

void utf16_to_utf8(const uint16_t *src, uint8_t *dst) {
  (void)src;
  if (dst)
    *dst = '\0';
}

void utf8_to_utf16(const uint8_t *src, uint16_t *dst) {
  (void)src;
  if (dst)
    *dst = 0;
}

size_t get_base64_size(size_t in) {
  return ((4 * in / 3) + 3) & ~3U;
}

int init_msg_dialog(const char *msg) {
  (void)msg;
  return 0;
}

int get_msg_dialog_result(void) {
  return 1;
}

void vita_logging_config_set_defaults(VitaLoggingConfig *cfg) {
  if (!cfg)
    return;
  memset(cfg, 0, sizeof(*cfg));
  cfg->enabled = false;
  cfg->force_error_logging = true;
  cfg->profile = VITA_LOG_PROFILE_ERRORS;
  cfg->queue_depth = VITA_LOG_DEFAULT_QUEUE_DEPTH;
  strncpy(cfg->path, VITA_LOG_DEFAULT_PATH, sizeof(cfg->path) - 1);
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
  (void)profile;
  return 0;
}

void vita_log_module_init(const VitaLoggingConfig *cfg) {
  (void)cfg;
}

void vita_log_module_shutdown(void) {}

void vita_log_submit_line(ChiakiLogLevel level, const char *line) {
  (void)level;
  (void)line;
}

bool vita_log_should_write_level(ChiakiLogLevel level) {
  (void)level;
  return false;
}

const VitaLoggingConfig *vita_log_get_active_config(void) {
  return NULL;
}

void controller_map_storage_set_defaults(ControllerMapStorage *storage) {
  if (!storage)
    return;
  memset(storage, 0, sizeof(*storage));
}

int sceRegMgrGetKeyInt(const char *category, const char *name, int *out_value) {
  (void)category;
  (void)name;
  if (out_value)
    *out_value = 1;
  return 0;
}

void hexdump(const uint8_t *data, size_t len) {
  (void)data;
  (void)len;
}

static void reset_config_file(void) {
  remove(CFG_FILENAME);
}

static void write_config_text(const char *text) {
  FILE *fp = fopen(CFG_FILENAME, "w");
  assert(fp != NULL);
  size_t len = strlen(text);
  assert(fwrite(text, 1, len, fp) == len);
  assert(fclose(fp) == 0);
}

static char *read_config_text(void) {
  FILE *fp = fopen(CFG_FILENAME, "r");
  assert(fp != NULL);
  assert(fseek(fp, 0, SEEK_END) == 0);
  long size = ftell(fp);
  assert(size >= 0);
  assert(fseek(fp, 0, SEEK_SET) == 0);
  char *buf = malloc((size_t)size + 1);
  assert(buf != NULL);
  size_t read = fread(buf, 1, (size_t)size, fp);
  assert(read == (size_t)size);
  buf[size] = '\0';
  assert(fclose(fp) == 0);
  return buf;
}

static int count_occurrences(const char *haystack, const char *needle) {
  int count = 0;
  const char *cursor = haystack;
  size_t needle_len = strlen(needle);
  while ((cursor = strstr(cursor, needle)) != NULL) {
    count++;
    cursor += needle_len;
  }
  return count;
}

static void init_cfg(VitaChiakiConfig *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  config_parse(cfg);
}

/* catches: a Latency Mode step whose saved name the loader does not know (it would fall back to
 * Max after a restart), and the one-time Max migration (#420) dragging a saved choice, the new
 * Extreme step included, to another value on the next load. */
static void test_latency_mode_roundtrip_every_step(void) {
  for (int mode = 0; mode < VITA_LATENCY_MODE_COUNT; mode++) {
    reset_config_file();

    VitaChiakiConfig cfg;
    init_cfg(&cfg);
    cfg.latency_mode = (VitaChiakiLatencyMode)mode;
    assert(config_serialize(&cfg));

    VitaChiakiConfig loaded;
    init_cfg(&loaded);
    assert(loaded.latency_mode == (VitaChiakiLatencyMode)mode);
  }
}

static void test_legacy_section_migration(void) {
  reset_config_file();
  write_config_text(
      "[general]\n"
      "version = 1\n"
      "\n"
      "[settings]\n"
      "auto_discovery = true\n"
      "controller_map_id = 201\n"
      "\n"
      "[controller_custom_map_1]\n"
      "valid = false\n"
      "in_l2 = 0\n"
      "in_r2 = 0\n"
      "resolution = \"720p\"\n"
      "fps = 60\n"
      "show_latency = true\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_720p);
  assert(cfg.fps == CHIAKI_VIDEO_FPS_PRESET_60);
  assert(cfg.show_latency == true);

  char *rewritten = read_config_text();
  assert(strstr(rewritten, "[settings]") != NULL);
  assert(strstr(rewritten, "resolution = \"720p\"") != NULL);
  assert(strstr(rewritten, "fps = 60") != NULL);
  assert(strstr(rewritten, "show_latency = true") != NULL);
  assert(count_occurrences(rewritten, "resolution = \"720p\"") == 1);
  free(rewritten);
}

static void test_root_level_fallback_migration(void) {
  reset_config_file();
  write_config_text(
      "resolution = \"1080p\"\n"
      "fps = 60\n"
      "\n"
      "[general]\n"
      "version = 1\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_1080p);
  assert(cfg.fps == CHIAKI_VIDEO_FPS_PRESET_60);

  char *rewritten = read_config_text();
  assert(strstr(rewritten, "[settings]") != NULL);
  assert(strstr(rewritten, "resolution = \"1080p\"") != NULL);
  assert(strstr(rewritten, "fps = 60") != NULL);
  assert(count_occurrences(rewritten, "resolution = \"1080p\"") == 1);
  free(rewritten);
}

static void test_legacy_bool_and_latency_migration(void) {
  reset_config_file();
  write_config_text(
      "[general]\n"
      "version = 1\n"
      "\n"
      "[settings]\n"
      "controller_map_id = 201\n"
      "\n"
      "[controller_custom_map_2]\n"
      "valid = false\n"
      "in_l2 = 0\n"
      "in_r2 = 0\n"
      "show_network_indicator = false\n"
      "show_stream_exit_hint = false\n"
      "send_actual_start_bitrate = false\n"
      "latency_mode = \"max\"\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.show_network_indicator == false);
  assert(cfg.show_stream_exit_hint == false);
  assert(cfg.send_actual_start_bitrate == false);
  assert(cfg.latency_mode == VITA_LATENCY_MODE_MAX);

  char *rewritten = read_config_text();
  assert(strstr(rewritten, "[settings]") != NULL);
  assert(strstr(rewritten, "show_network_indicator = false") != NULL);
  assert(strstr(rewritten, "show_stream_exit_hint = false") != NULL);
  assert(strstr(rewritten, "send_actual_start_bitrate = false") != NULL);
  assert(strstr(rewritten, "latency_mode = \"max\"") != NULL);
  free(rewritten);
}

/* catches: pre-[settings] configs whose bools sit at the file's root losing those settings on load
 * (the keys must be before any table header, otherwise TOML puts them in [general]). */
static void test_root_level_bool_migration(void) {
  reset_config_file();
  write_config_text(
      "stretch_video = true\n"
      "clamp_soft_restart_bitrate = false\n"
      "\n"
      "[general]\n"
      "version = 1\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.stretch_video == true);
  assert(cfg.clamp_soft_restart_bitrate == false);

  char *rewritten = read_config_text();
  assert(strstr(rewritten, "[settings]") != NULL);
  assert(strstr(rewritten, "stretch_video = true") != NULL);
  assert(strstr(rewritten, "clamp_soft_restart_bitrate = false") != NULL);
  free(rewritten);
}

static void test_invalid_fps_falls_back_to_30(void) {
  reset_config_file();
  write_config_text(
      "[general]\n"
      "version = 1\n"
      "\n"
      "[settings]\n"
      "controller_map_id = 201\n"
      "fps = 42\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.fps == CHIAKI_VIDEO_FPS_PRESET_30);
  assert(config_serialize(&cfg));

  char *rewritten = read_config_text();
  assert(strstr(rewritten, "fps = 30") != NULL);
  free(rewritten);
}

static void test_resolution_roundtrip(void) {
  const struct {
    ChiakiVideoResolutionPreset preset;
    const char *label;
  } cases[] = {
      {CHIAKI_VIDEO_RESOLUTION_PRESET_360p, "360p"},
      {CHIAKI_VIDEO_RESOLUTION_PRESET_540p, "540p"},
      {CHIAKI_VIDEO_RESOLUTION_PRESET_720p, "720p"},
      {CHIAKI_VIDEO_RESOLUTION_PRESET_1080p, "1080p"},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    reset_config_file();

    VitaChiakiConfig cfg;
    init_cfg(&cfg);
    cfg.resolution = cases[i].preset;
    assert(config_serialize(&cfg));

    VitaChiakiConfig loaded;
    init_cfg(&loaded);
    assert(loaded.resolution == cases[i].preset);

    char *saved = read_config_text();
    char expected_line[32];
    snprintf(expected_line, sizeof(expected_line), "resolution = \"%s\"", cases[i].label);
    assert(strstr(saved, expected_line) != NULL);
    free(saved);
  }
}

static void test_registered_hosts_require_required_fields(void) {
  reset_config_file();
  write_config_text(
      "[general]\n"
      "version = 1\n"
      "\n"
      "[[registered_hosts]]\n"
      "target = \"ps5_1\"\n"
      "rp_key_type = 2\n"
      "server_nickname = \"invalid\"\n"
      "\n"
      "[[registered_hosts]]\n"
      "server_mac = \"AQIDBAUG\"\n"
      "server_nickname = \"valid\"\n"
      "target = \"ps5_1\"\n"
      "rp_key = \"AQIDBAUGBwgJCgsMDQ4PEA==\"\n"
      "rp_key_type = 2\n"
      "rp_regist_key = \"0011223344556677\"\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.num_registered_hosts == 1);
  assert(cfg.registered_hosts[0] != NULL);
  assert(cfg.registered_hosts[0]->registered_state != NULL);
  assert(cfg.registered_hosts[0]->target == CHIAKI_TARGET_PS5_1);
  assert(cfg.registered_hosts[0]->registered_state->rp_regist_key[0] != '\0');
}

/* catches: a config saved before these keys existed loading with blur or hints wrong, the new
 * background keys disturbing values that were already there, or an existing user's saved Waves blur
 * being lost when the Glyphs keys are absent. */
static void test_old_config_gets_new_field_defaults(void) {
  reset_config_file();
  write_config_text(
      "[general]\n"
      "version = 1\n"
      "\n"
      "[settings]\n"
      "controller_map_id = 201\n"
      "fps = 60\n"
      "show_latency = true\n"
      "stretch_video = true\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.background == VITA_BACKGROUND_WAVES);
  assert(cfg.background_blur == VITA_BACKGROUND_BLUR_NONE);
  assert(cfg.background_blur_glyphs == VITA_BACKGROUND_BLUR_NONE);
  assert(cfg.theme == VITA_THEME_OCEAN);
  assert(cfg.show_button_hints == true);
  assert(cfg.fps == CHIAKI_VIDEO_FPS_PRESET_60);
  assert(cfg.show_latency == true);
  assert(cfg.stretch_video == true);
}

/* catches: the removed Show Only Paired key lingering in the config file forever (written back
 * on every save), or an old file that still carries it failing to load its other settings. */
static void test_removed_show_only_paired_key_is_dropped(void) {
  reset_config_file();
  write_config_text(
      "[general]\n"
      "version = 1\n"
      "\n"
      "[settings]\n"
      "controller_map_id = 201\n"
      "show_only_paired = true\n"
      "show_latency = true\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.show_latency == true);
  assert(config_serialize(&cfg));

  char *rewritten = read_config_text();
  assert(strstr(rewritten, "show_only_paired") == NULL);
  assert(strstr(rewritten, "show_latency = true") != NULL);
  free(rewritten);
}

/* catches: the removed Force 30 FPS key breaking load of other settings or being written back. */
static void test_removed_force_30fps_key_is_ignored_and_dropped(void) {
  reset_config_file();
  write_config_text(
      "[general]\n"
      "version = 1\n"
      "\n"
      "[settings]\n"
      "controller_map_id = 201\n"
      "force_30fps = true\n"
      "show_latency = true\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.show_latency == true);
  assert(config_serialize(&cfg));

  char *rewritten = read_config_text();
  assert(strstr(rewritten, "force_30fps") == NULL);
  assert(strstr(rewritten, "show_latency = true") != NULL);
  free(rewritten);
}

/* catches: a typed account ID or the choice between Vita and Manual being lost on save or load, so
 * pairing after a restart silently uses the wrong ID. */
static void test_manual_account_id_and_choice_survive_save_and_load(void) {
  reset_config_file();
  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  cfg.psn_account_id = strdup("EjRWeJq83vA=");
  cfg.psn_account_id_manual = strdup("782rkHhWNBI=");
  cfg.psn_account_id_source = PSN_ACCOUNT_ID_SOURCE_MANUAL;
  assert(config_serialize(&cfg));

  VitaChiakiConfig loaded;
  init_cfg(&loaded);
  assert(strcmp(loaded.psn_account_id, "EjRWeJq83vA=") == 0);
  assert(strcmp(loaded.psn_account_id_manual, "782rkHhWNBI=") == 0);
  assert(loaded.psn_account_id_source == PSN_ACCOUNT_ID_SOURCE_MANUAL);
  PsnAccountIdSource used;
  assert(strcmp(config_psn_account_id_in_use(&loaded, &used), "782rkHhWNBI=") == 0);
  assert(used == PSN_ACCOUNT_ID_SOURCE_MANUAL);

  // An older config without the new keys defaults to the Vita's ID.
  reset_config_file();
  VitaChiakiConfig fresh;
  init_cfg(&fresh);
  assert(fresh.psn_account_id_manual == NULL);
  assert(fresh.psn_account_id_source == PSN_ACCOUNT_ID_SOURCE_VITA);
}

/* catches: a wrong default or a config without the key loading Motion Deadband on, which would ship
 * the deadband (dropped fine gyro motion) to every user; and a missing writer entry losing the
 * user's choice to turn it on at the next start. */
static void test_motion_deadband_defaults_off_and_survives_save_and_load(void) {
  reset_config_file();
  write_config_text(
      "[general]\nversion = 1\n\n[settings]\nshow_network_indicator = false\n");
  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.show_network_indicator == false);
  assert(cfg.motion_deadband == false);

  cfg.motion_deadband = true;
  assert(config_serialize(&cfg));
  VitaChiakiConfig loaded;
  init_cfg(&loaded);
  assert(loaded.motion_deadband == true);
}

/* catches: a config without the key turning the on-screen mic button off, or the player's choice to
 * turn it off (kept while the microphone is off) coming back on at the next start. */
static void test_show_mic_button_defaults_on_and_survives_save_and_load(void) {
  reset_config_file();
  write_config_text(
      "[general]\nversion = 1\n\n[settings]\nshow_network_indicator = false\n");
  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.show_mic_button == true);

  cfg.show_mic_button = false;
  assert(config_serialize(&cfg));
  VitaChiakiConfig loaded;
  init_cfg(&loaded);
  assert(loaded.show_mic_button == false);
}

/* catches: a non-default blur level, theme or hints-off choice being lost on save or load, so the
 * setting reverts after a restart. */
static void test_new_fields_survive_save_and_load(void) {
  reset_config_file();
  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  cfg.background_blur = VITA_BACKGROUND_BLUR_STRONG;
  cfg.theme = VITA_THEME_GRAPHITE;
  cfg.show_button_hints = false;
  assert(config_serialize(&cfg));

  VitaChiakiConfig loaded;
  init_cfg(&loaded);
  assert(loaded.background_blur == VITA_BACKGROUND_BLUR_STRONG);
  assert(loaded.theme == VITA_THEME_GRAPHITE);
  assert(loaded.show_button_hints == false);
}

/* catches: the Glyphs choice or its blur being lost on save or load, or the two backgrounds
 * sharing one blur so changing Glyphs overwrites what Waves remembered. */
static void test_background_and_per_background_blur_survive_save_and_load(void) {
  reset_config_file();
  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  cfg.background = VITA_BACKGROUND_GLYPHS;
  cfg.background_blur = VITA_BACKGROUND_BLUR_STRONG;
  cfg.background_blur_glyphs = VITA_BACKGROUND_BLUR_SOFT;
  assert(config_serialize(&cfg));

  VitaChiakiConfig loaded;
  init_cfg(&loaded);
  assert(loaded.background == VITA_BACKGROUND_GLYPHS);
  assert(loaded.background_blur == VITA_BACKGROUND_BLUR_STRONG);
  assert(loaded.background_blur_glyphs == VITA_BACKGROUND_BLUR_SOFT);
  assert(config_background_blur(&loaded) == VITA_BACKGROUND_BLUR_SOFT);

  config_set_background_blur(&loaded, VITA_BACKGROUND_BLUR_DARK);
  assert(loaded.background_blur_glyphs == VITA_BACKGROUND_BLUR_DARK);
  assert(loaded.background_blur == VITA_BACKGROUND_BLUR_STRONG);

  loaded.background = VITA_BACKGROUND_WAVES;
  assert(config_background_blur(&loaded) == VITA_BACKGROUND_BLUR_STRONG);
}

/* catches: a hand-edited or corrupt blur value outside 0..3 being trusted, which would index
 * past the background levels. */
static void test_out_of_range_blur_is_rejected(void) {
  const int bad_values[] = {-1, 4, 99};
  for (size_t i = 0; i < sizeof(bad_values) / sizeof(bad_values[0]); i++) {
    char text[160];
    snprintf(text, sizeof(text),
             "[general]\nversion = 1\n\n[settings]\ncontroller_map_id = 201\nbackground_blur = %d\n",
             bad_values[i]);
    reset_config_file();
    write_config_text(text);

    VitaChiakiConfig cfg;
    init_cfg(&cfg);
    assert(cfg.background_blur == VITA_BACKGROUND_BLUR_NONE);
  }
}

/* catches: a hand-edited or corrupt background or Glyphs blur value being trusted, which would
 * select a background that does not exist or index past the blur levels. */
static void test_out_of_range_background_keys_are_rejected(void) {
  const int bad_backgrounds[] = {-1, 2, 99};
  const int bad_blurs[] = {-1, 4, 99};
  for (size_t i = 0; i < sizeof(bad_backgrounds) / sizeof(bad_backgrounds[0]); i++) {
    char text[200];
    snprintf(text, sizeof(text),
             "[general]\nversion = 1\n\n[settings]\ncontroller_map_id = 201\n"
             "background = %d\nbackground_blur_glyphs = %d\n",
             bad_backgrounds[i], bad_blurs[i]);
    reset_config_file();
    write_config_text(text);

    VitaChiakiConfig cfg;
    init_cfg(&cfg);
    assert(cfg.background == VITA_BACKGROUND_WAVES);
    assert(cfg.background_blur_glyphs == VITA_BACKGROUND_BLUR_NONE);
  }
}

/* catches: a hand-edited or corrupt theme value outside 0..4 being trusted, which would index
 * past the theme table. */
static void test_out_of_range_theme_is_rejected(void) {
  const int bad_values[] = {-1, 5, 99};
  for (size_t i = 0; i < sizeof(bad_values) / sizeof(bad_values[0]); i++) {
    char text[160];
    snprintf(text, sizeof(text),
             "[general]\nversion = 1\n\n[settings]\ncontroller_map_id = 201\ntheme = %d\n",
             bad_values[i]);
    reset_config_file();
    write_config_text(text);

    VitaChiakiConfig cfg;
    init_cfg(&cfg);
    assert(cfg.theme == VITA_THEME_OCEAN);
  }
}

/* catches: a console's chosen room icon being lost on save or load (so every console reverts to
 * the TV after a restart), or one console's choice landing on another console. */
static void test_room_icons_survive_save_and_load(void) {
  const uint8_t mac_a[6] = {0x00, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e};
  const uint8_t mac_b[6] = {0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa};
  const uint8_t mac_other[6] = {0x00, 0x1a, 0x2b, 0x3c, 0x4d, 0x5f};

  reset_config_file();
  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(room_icons_set(&cfg.room_icons, mac_a, ROOM_ICON_BEDROOM));
  assert(room_icons_set(&cfg.room_icons, mac_b, ROOM_ICON_ANOTHER_PLACE));
  assert(config_serialize(&cfg));

  VitaChiakiConfig loaded;
  init_cfg(&loaded);
  assert(room_icons_get(&loaded.room_icons, mac_a) == ROOM_ICON_BEDROOM);
  assert(room_icons_get(&loaded.room_icons, mac_b) == ROOM_ICON_ANOTHER_PLACE);
  assert(room_icons_get(&loaded.room_icons, mac_other) == ROOM_ICON_TV);
}

/* catches: a hand-edited or corrupt room_icons entry being trusted: an icon outside the art
 * (which would index past the textures), a malformed or all-zero MAC, or a repeated MAC
 * overriding the first. The one good entry must still load. */
static void test_bad_room_icon_entries_are_ignored(void) {
  const uint8_t good_mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
  reset_config_file();
  write_config_text(
      "[general]\nversion = 1\n\n[settings]\ncontroller_map_id = 201\n"
      "\n[[room_icons]]\nmac = \"010203040506\"\nicon = 4\n"
      "\n[[room_icons]]\nmac = \"0a0b0c0d0e0f\"\nicon = 99\n"
      "\n[[room_icons]]\nmac = \"0a0b0c0d0e0f\"\nicon = -1\n"
      "\n[[room_icons]]\nmac = \"0a0b0c0d0e0f\"\nicon = 0\n"
      "\n[[room_icons]]\nmac = \"0102\"\nicon = 2\n"
      "\n[[room_icons]]\nmac = \"zz0203040506\"\nicon = 2\n"
      "\n[[room_icons]]\nmac = \"000000000000\"\nicon = 2\n"
      "\n[[room_icons]]\nmac = \"010203040506\"\nicon = 3\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.room_icons.count == 1);
  assert(room_icons_get(&cfg.room_icons, good_mac) == ROOM_ICON_OFFICE);
}

/* catches: a console with no MAC being given a stored icon (it has no identity, so the choice
 * would leak onto every other MAC-less console); an out-of-range choice being stored; choosing
 * the TV leaving a stale entry that comes back after a restart; and a full table overwriting or
 * corrupting existing choices. */
static void test_room_icon_set_rules(void) {
  const uint8_t no_mac[6] = {0};
  const uint8_t mac[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
  RoomIconTable table = {0};

  assert(!room_icons_set(&table, no_mac, ROOM_ICON_DORM));
  assert(room_icons_get(&table, no_mac) == ROOM_ICON_TV);
  assert(!room_icons_set(&table, mac, ROOM_ICON_COUNT));
  assert(!room_icons_set(&table, mac, -1));
  assert(table.count == 0);

  assert(room_icons_set(&table, mac, ROOM_ICON_DORM));
  assert(room_icons_set(&table, mac, ROOM_ICON_TV));
  assert(table.count == 0);
  assert(room_icons_get(&table, mac) == ROOM_ICON_TV);

  for (int i = 0; i < MAX_ROOM_ICON_ENTRIES; i++) {
    const uint8_t fill[6] = {0xaa, 0, 0, 0, 0, (uint8_t)(i + 1)};
    assert(room_icons_set(&table, fill, ROOM_ICON_OFFICE));
  }
  assert(!room_icons_set(&table, mac, ROOM_ICON_DORM));
  assert(room_icons_get(&table, mac) == ROOM_ICON_TV);
  const uint8_t first[6] = {0xaa, 0, 0, 0, 0, 1};
  assert(room_icons_set(&table, first, ROOM_ICON_BEDROOM));
  assert(room_icons_get(&table, first) == ROOM_ICON_BEDROOM);
  assert(table.count == MAX_ROOM_ICON_ENTRIES);
}

static const uint8_t DEVICE_ID_A[16] = {0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8,
                                        0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0};
static const uint8_t DEVICE_ID_B[16] = {0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8,
                                        0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0};

/* Raw pairing-key bytes. The registration key is deliberately 16 non-NUL bytes: a full buffer
 * with no terminator, as the console registration produces. */
static const uint8_t TEST_RP_KEY[16] = {0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
                                        0x98, 0xA9, 0xBA, 0xCB, 0xDC, 0xED, 0xFE, 0x0F};
static const char TEST_RP_KEY_B64[] = "ECEyQ1RldoeYqbrL3O3+Dw==";
static const char TEST_REGIST_KEY_FULL[16] = {'Q', 'r', 'S', 't', 'U', 'v', 'W', 'x',
                                              'Y', 'z', '0', '1', '2', '3', '4', '5'};

/* Registers one console with the test pairing keys in cfg (not yet saved). */
static void add_test_registered_host(VitaChiakiConfig *cfg) {
  VitaChiakiHost *host = calloc(1, sizeof(*host));
  ChiakiRegisteredHost *rstate = calloc(1, sizeof(*rstate));
  assert(host != NULL && rstate != NULL);
  host->type = REGISTERED;
  host->registered_state = rstate;
  host->target = CHIAKI_TARGET_PS5_1;
  const uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
  memcpy(host->server_mac, mac, sizeof(mac));
  memcpy(rstate->server_mac, mac, sizeof(mac));
  snprintf(rstate->server_nickname, sizeof(rstate->server_nickname), "living-room");
  rstate->target = CHIAKI_TARGET_PS5_1;
  rstate->rp_key_type = 2;
  memcpy(rstate->rp_key, TEST_RP_KEY, sizeof(TEST_RP_KEY));
  memcpy(rstate->rp_regist_key, TEST_REGIST_KEY_FULL, sizeof(TEST_REGIST_KEY_FULL));
  cfg->registered_hosts[cfg->num_registered_hosts++] = host;
}

/* catches: pairing keys written to chiaki.toml as plain text (enough to impersonate the Vita to
 * the console), or a save/load round trip that corrupts a key, including a registration key that
 * fills its whole 16-byte buffer with no NUL terminator. */
static void test_pairing_keys_are_encrypted_and_survive_save_and_load(void) {
  token_crypto_set_test_device_id(DEVICE_ID_A);
  reset_config_file();
  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  add_test_registered_host(&cfg);
  assert(config_serialize(&cfg));

  char *saved = read_config_text();
  assert(strstr(saved, "rp_key_enc = ") != NULL);
  assert(strstr(saved, "rp_regist_key_enc = ") != NULL);
  assert(strstr(saved, "rp_key =") == NULL);
  assert(strstr(saved, "rp_regist_key =") == NULL);
  assert(strstr(saved, TEST_RP_KEY_B64) == NULL);
  assert(strstr(saved, "QrStUvWxYz012345") == NULL);
  free(saved);

  VitaChiakiConfig loaded;
  init_cfg(&loaded);
  assert(loaded.num_registered_hosts == 1);
  ChiakiRegisteredHost *rstate = loaded.registered_hosts[0]->registered_state;
  assert(memcmp(rstate->rp_key, TEST_RP_KEY, sizeof(TEST_RP_KEY)) == 0);
  assert(memcmp(rstate->rp_regist_key, TEST_REGIST_KEY_FULL, sizeof(TEST_REGIST_KEY_FULL)) == 0);
  assert(rstate->rp_key_type == 2);
}

/* catches: users losing their console pairing on upgrade (a plain-text host not loading), or the
 * plain-text keys staying in the file because the migration rewrite never ran. */
static void test_plaintext_pairing_keys_migrate_to_encrypted(void) {
  token_crypto_set_test_device_id(DEVICE_ID_A);
  reset_config_file();
  write_config_text(
      "[general]\n"
      "version = 1\n"
      "\n"
      "[[registered_hosts]]\n"
      "server_mac = \"AQIDBAUG\"\n"
      "server_nickname = \"legacy\"\n"
      "target = \"ps5_1\"\n"
      "rp_key = \"ECEyQ1RldoeYqbrL3O3+Dw==\"\n"
      "rp_key_type = 2\n"
      "rp_regist_key = \"abcd1234\"\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.num_registered_hosts == 1);
  ChiakiRegisteredHost *rstate = cfg.registered_hosts[0]->registered_state;
  assert(memcmp(rstate->rp_key, TEST_RP_KEY, sizeof(TEST_RP_KEY)) == 0);
  assert(strcmp(rstate->rp_regist_key, "abcd1234") == 0);

  char *rewritten = read_config_text();
  assert(strstr(rewritten, "rp_key_enc = ") != NULL);
  assert(strstr(rewritten, "rp_regist_key_enc = ") != NULL);
  assert(strstr(rewritten, "rp_key =") == NULL);
  assert(strstr(rewritten, "rp_regist_key =") == NULL);
  free(rewritten);

  VitaChiakiConfig reloaded;
  init_cfg(&reloaded);
  assert(reloaded.num_registered_hosts == 1);
  assert(memcmp(reloaded.registered_hosts[0]->registered_state->rp_key, TEST_RP_KEY,
                sizeof(TEST_RP_KEY)) == 0);
}

/* catches: a host whose encrypted keys will not decrypt (memory card moved to another Vita)
 * silently loading from plain-text keys that sit in the same file. */
static void test_undecryptable_pairing_keys_drop_host_without_plaintext_fallback(void) {
  token_crypto_set_test_device_id(DEVICE_ID_A);
  reset_config_file();
  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  add_test_registered_host(&cfg);
  assert(config_serialize(&cfg));

  char *saved = read_config_text();
  const char *plain_keys = "rp_key = \"ECEyQ1RldoeYqbrL3O3+Dw==\"\nrp_regist_key = \"abcd1234\"\n";
  char *with_plain = malloc(strlen(saved) + strlen(plain_keys) + 2);
  assert(with_plain != NULL);
  sprintf(with_plain, "%s\n%s", saved, plain_keys);
  write_config_text(with_plain);
  free(saved);
  free(with_plain);

  token_crypto_set_test_device_id(DEVICE_ID_B);
  VitaChiakiConfig loaded;
  init_cfg(&loaded);
  assert(loaded.num_registered_hosts == 0);
  token_crypto_set_test_device_id(DEVICE_ID_A);
}

/* catches: legacy plain-text PSN tokens (e.g. from a testing build) staying on disk in plain text
 * after upgrade, or being lost instead of kept in memory and re-saved encrypted. */
static void test_plaintext_psn_tokens_migrate_to_encrypted(void) {
  token_crypto_set_test_device_id(DEVICE_ID_A);
  reset_config_file();
  write_config_text(
      "[general]\n"
      "version = 1\n"
      "\n"
      "[settings]\n"
      "psn_auth_provenance = 1\n"
      "psn_oauth_access_token = \"plain-access-token\"\n"
      "psn_oauth_refresh_token = \"plain-refresh-token\"\n"
      "psn_oauth_expires_at_unix = 4102444800\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.psn_oauth_access_token != NULL);
  assert(strcmp(cfg.psn_oauth_access_token, "plain-access-token") == 0);
  assert(cfg.psn_oauth_refresh_token != NULL);
  assert(strcmp(cfg.psn_oauth_refresh_token, "plain-refresh-token") == 0);

  char *rewritten = read_config_text();
  assert(strstr(rewritten, "psn_oauth_access_token_enc = ") != NULL);
  assert(strstr(rewritten, "psn_oauth_refresh_token_enc = ") != NULL);
  assert(strstr(rewritten, "psn_oauth_access_token =") == NULL);
  assert(strstr(rewritten, "psn_oauth_refresh_token =") == NULL);
  assert(strstr(rewritten, "plain-access-token") == NULL);
  assert(strstr(rewritten, "plain-refresh-token") == NULL);
  free(rewritten);

  VitaChiakiConfig reloaded;
  init_cfg(&reloaded);
  assert(reloaded.psn_oauth_access_token != NULL);
  assert(strcmp(reloaded.psn_oauth_access_token, "plain-access-token") == 0);
}

void run_packet_path_tests(void);
void run_json_escape_tests(void);
void run_token_crypto_tests(void);

int main(void) {
  token_crypto_set_test_device_id(DEVICE_ID_A);
  test_latency_mode_roundtrip_every_step();
  test_legacy_section_migration();
  test_root_level_fallback_migration();
  test_legacy_bool_and_latency_migration();
  test_root_level_bool_migration();
  test_invalid_fps_falls_back_to_30();
  test_resolution_roundtrip();
  test_registered_hosts_require_required_fields();
  test_old_config_gets_new_field_defaults();
  test_removed_show_only_paired_key_is_dropped();
  test_removed_force_30fps_key_is_ignored_and_dropped();
  test_new_fields_survive_save_and_load();
  test_motion_deadband_defaults_off_and_survives_save_and_load();
  test_show_mic_button_defaults_on_and_survives_save_and_load();
  test_manual_account_id_and_choice_survive_save_and_load();
  test_out_of_range_blur_is_rejected();
  test_out_of_range_theme_is_rejected();
  test_out_of_range_background_keys_are_rejected();
  test_background_and_per_background_blur_survive_save_and_load();
  test_room_icons_survive_save_and_load();
  test_bad_room_icon_entries_are_ignored();
  test_room_icon_set_rules();
  test_pairing_keys_are_encrypted_and_survive_save_and_load();
  test_plaintext_pairing_keys_migrate_to_encrypted();
  test_undecryptable_pairing_keys_drop_host_without_plaintext_fallback();
  test_plaintext_psn_tokens_migrate_to_encrypted();
  run_packet_path_tests();
  run_json_escape_tests();
  run_token_crypto_tests();
  reset_config_file();
  puts("vitarps5 config tests passed");
  return 0;
}
